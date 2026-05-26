# robot_teacher — Робот-педагог на ROS2

Образовательный робот, который в реальном времени:
- детектирует лицо пользователя через веб-камеру
- распознаёт 8 жестов руки (правила + контурный анализ OpenCV)
- записывает голос и распознаёт речь через Groq Whisper API
- ведёт диалог через LLM (Groq llama-3.1-8b-instant)
- отображает всё в веб-интерфейсе (камера, диалог, состояния, жесты)

Реализован на C++ с тремя независимыми ROS2-нодами.
Протестирован на Ubuntu 24.04 + ROS2 Jazzy (нативно и через WSL2 на Windows 11).

---

## Архитектура

```
┌──────────────────────┐   gesture (String)      ┌──────────────────────┐
│                      │ ───────────────────────► │                      │
│  perception_node     │   face_present (String)  │                      │
│                      │ ───────────────────────► │   dialog_node        │
│  • V4L2 захват       │   camera_jpeg (String)   │                      │
│    кадров с камеры   │ ───────────────────────► │  • конечный автомат  │
│  • Haar face detect  │                          │    6 состояний       │
│  • YCrCb skin mask   │                          │  • история беседы    │
│  • contour gesture   │   listen_command ◄───────│  • Groq LLM API      │
│    classifier        │                          │  • fallback без ключа│
│  • base64 JPEG →     │                          │                      │
│    ROS топик         │                          └──────────────────────┘
│                      │                                    │
│  Потоки:             │                            dialog_output (String)
│  capture_thread_     │                                    │
│  process_thread_     │                            ┌───────▼──────────┐
└──────────────────────┘                            │  robot_ui_server  │
                                                    │  (Python, порт   │
┌──────────────────────┐   speech_text (String)     │  8888)           │
│                      │ ───────────────────────►   │                  │
│  hearing_node        │                            │  SSE → браузер   │
│                      │                            │  MJPEG стрим     │
│  • arecord → PulseAudio                           └──────────────────┘
│  • WAV → Groq        │
│    Whisper API       │
│  • таймаут молчания 6с│
│                      │
│  Потоки:             │
│  audio_thread_       │
│  stt_thread_         │
└──────────────────────┘
```

### Топики ROS2

| Топик | Тип | Направление | Описание |
|---|---|---|---|
| `robot_teacher/gesture` | `std_msgs/String` | perception → dialog | ID жеста |
| `robot_teacher/face_present` | `std_msgs/String` | perception → dialog | `detected` / `lost` |
| `robot_teacher/camera_jpeg` | `std_msgs/String` | perception → UI | Base64 JPEG кадр |
| `robot_teacher/speech_text` | `std_msgs/String` | hearing → dialog | Распознанная фраза |
| `robot_teacher/listen_command` | `std_msgs/String` | dialog → hearing | `start` / `stop` |
| `robot_teacher/dialog_output` | `std_msgs/String` | dialog → UI/экран | Ответ Ботика |

---

## Жесты

| Жест | ID | Реакция |
|---|---|---|
| Большой палец вверх | `thumb_up` | Подтверждение, следующая тема |
| Большой палец вниз | `thumb_down` | Смена темы / другой вариант |
| Указательный палец в камеру | `point` | Рассказ о роботе |
| Поднятая рука | `raised_hand` | **Активирует голосовой режим (STT)** |
| Открытая ладонь | `open_palm` | Пауза / возобновление |
| Помах рукой | `wave` | Приветствие / прощание |
| Знак OK *(бонус)* | `ok` | Подтверждение |
| Скрещенные руки *(бонус)* | `crossed_arms` | Несогласие |

---

## Состояния сессии

```
         face detected
IDLE ──────────────────► GREETING
                              │ LLM приветствие
                              ▼
                         DIALOG ◄──────────────────────┐
                         │    │                        │
              open_palm  │    │ raised_hand            │ ответ LLM
                         ▼    ▼                        │
                       PAUSED LISTENING ───────────────┘
                         │         │ face lost
                         │         ▼
                         └──► BYE ──► IDLE (через 4 сек)
```

---

## Зависимости

### Ubuntu 24.04 (нативно)

```bash
# 1. ROS2 Jazzy
sudo apt install -y software-properties-common curl
sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) \
  signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] \
  http://packages.ros.org/ros2/ubuntu \
  $(. /etc/os-release && echo $UBUNTU_CODENAME) main" \
  | sudo tee /etc/apt/sources.list.d/ros2.list
sudo apt update
sudo apt install -y ros-jazzy-desktop

# 2. Colcon
sudo apt install -y python3-colcon-common-extensions

# 3. Зависимости C++ нод
sudo apt install -y \
  libopencv-dev \
  libcurl4-openssl-dev \
  portaudio19-dev \
  nlohmann-json3-dev \
  ros-jazzy-cv-bridge \
  ros-jazzy-image-transport

# 4. Аудио
sudo apt install -y \
  pulseaudio \
  pulseaudio-module-bluetooth \
  alsa-utils

# 5. Python (для UI сервера)
pip install opencv-python --break-system-packages

# 6. Активировать ROS2
echo "source /opt/ros/jazzy/setup.bash" >> ~/.bashrc
source ~/.bashrc
```

### Windows 11 + WSL2

```powershell
# PowerShell от администратора

# 1. Установить Chocolatey (если нет)
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# 2. Установить WSL2 + Ubuntu 24.04
wsl --install -d Ubuntu-24.04

# 3. Установить VcXsrv (для GUI окна OpenCV, опционально)
choco install -y vcxsrv

# 4. Установить usbipd (для проброса USB камеры)
choco install -y usbipd

# 5. Пробросить камеру в WSL (запускать после каждого перезапуска WSL)
# Сначала найти BUSID камеры:
usbipd list
# Затем пробросить (замените 1-6 на ваш BUSID):
usbipd bind --busid 1-6
usbipd attach --wsl --busid 1-6

# 6. Пробросить порт UI сервера в Windows
$wslIP = (wsl hostname -I).Trim().Split()[0]
netsh interface portproxy add v4tov4 listenport=8888 listenaddress=0.0.0.0 connectport=8888 connectaddress=$wslIP
New-NetFirewallRule -DisplayName "WSL_8888" -Direction Inbound -Protocol TCP -LocalPort 8888 -Action Allow
```

После этого внутри WSL выполнить те же команды, что для Ubuntu (раздел выше).

---

## Сборка

```bash
cd ~/robot_teacher_ws
colcon build --packages-select robot_teacher
source install/setup.bash
```

---

## API ключи

Система работает в трёх режимах:

| Режим | Ключи | Возможности |
|---|---|---|
| Полный | `GROQ_API_KEY` | LLM + STT через Groq (бесплатно) |
| Только LLM | `ANTHROPIC_API_KEY` или `OPENAI_API_KEY` | LLM без голоса |
| Без ключей | — | Фиксированные ответы, жесты работают |

Получить бесплатный Groq ключ: https://console.groq.com → API Keys

```bash
# Добавить ключи навсегда
echo 'export GROQ_API_KEY="gsk_..."' >> ~/.bashrc
echo 'export PULSE_SERVER=unix:/run/user/1000/pulse/native' >> ~/.bashrc
source ~/.bashrc
```

---

## Запуск

### Полный запуск (2 терминала)

**Терминал 1 — ROS2 ноды:**
```bash
pulseaudio --start --exit-idle-time=-1 2>/dev/null
source /opt/ros/jazzy/setup.bash
source ~/robot_teacher_ws/install/setup.bash
ros2 launch robot_teacher robot_teacher_launch.py show_debug_window:=false
```

**Терминал 2 — UI сервер:**
```bash
source /opt/ros/jazzy/setup.bash
source ~/robot_teacher_ws/install/setup.bash
python3 ~/robot_teacher_ws/robot_ui_server.py
```

Открыть в браузере:
- Нативно: `http://localhost:8888`
- WSL2: `http://<WSL_IP>:8888` (IP из `hostname -I`)

### Параметры запуска

```bash
# Другая камера (индекс 2)
ros2 launch robot_teacher robot_teacher_launch.py camera_id:=2

# OpenCV debug окно (только нативный Linux с X11)
ros2 launch robot_teacher robot_teacher_launch.py show_debug_window:=true

# OpenAI вместо Groq для LLM
ros2 launch robot_teacher robot_teacher_launch.py llm_provider:=openai

# Язык STT
ros2 launch robot_teacher robot_teacher_launch.py language:=en
```

### Запуск нод по отдельности (отладка)

```bash
# Терминал 1
ros2 run robot_teacher perception_node --ros-args -p show_debug_window:=false

# Терминал 2
ros2 run robot_teacher hearing_node

# Терминал 3
ros2 run robot_teacher dialog_node
```

---

## Мониторинг

```bash
# Ответы робота
ros2 topic echo /robot_teacher/dialog_output

# Детекция лица
ros2 topic echo /robot_teacher/face_present

# Жесты
ros2 topic echo /robot_teacher/gesture

# Частота публикации кадров
ros2 topic hz /robot_teacher/camera_jpeg

# Граф нод
ros2 run rqt_graph rqt_graph
```

---

## Сценарий тестирования (по ТЗ)

1. Войдите в кадр → робот поздоровается и задаст вводный вопрос
2. Покажите **👍** → подтверждение, переход к следующей теме
3. Покажите **👎** → смена темы
4. Укажите пальцем на камеру **👉** → рассказ о роботе
5. Поднимите руку **✋** → «Слушаю тебя, говори!» → задайте вопрос голосом
6. Покажите открытую ладонь **🤚** → пауза; снова ладонь → возобновление
7. Помашите рукой **👋** → ответное приветствие
8. Выйдите из кадра → прощание → через 4 сек возврат в IDLE

---

## Структура проекта

```
robot_teacher_ws/
├── robot_ui_server.py              # UI сервер (Python, порт 8888)
└── src/
    └── robot_teacher/
        ├── CMakeLists.txt
        ├── package.xml
        ├── README.md               # Этот файл
        ├── TROUBLESHOOTING.md      # Решение проблем
        ├── include/
        │   └── robot_teacher/
        │       └── common.hpp      # Топики и константы жестов
        ├── src/
        │   ├── perception_node.cpp # Камера → лицо → жесты → кадры
        │   ├── hearing_node.cpp    # Микрофон → arecord → Groq Whisper
        │   └── dialog_node.cpp     # LLM → конечный автомат → ответы
        └── launch/
            └── robot_teacher_launch.py
```

---

## Архитектурные решения

### Почему Groq, а не Anthropic/OpenAI
Groq предоставляет бесплатный tier без карты. Поддерживает как LLM (llama-3.1-8b-instant), так и Whisper STT — один ключ для всего.

### Почему rule-based детекция жестов, а не ML
Нет зависимости от ONNX/TFLite/MediaPipe. Работает на любом ноутбуке без GPU через стандартный OpenCV. Для 8 жестов точности достаточно при нормальном освещении.

### Почему STT активируется жестом, а не непрерывно
Минимизирует расходы API и педагогически естественна — аналог поднятой руки в классе.

### Почему arecord вместо PortAudio для захвата аудио
PortAudio не видит PulseAudio устройства в WSL2. arecord через `pulse` backend работает стабильно.

### Почему кадры публикуются через ROS топик
Позволяет UI серверу получать видео без второго захвата камеры. Linux V4L2 не поддерживает два одновременных reader на одном устройстве.

# TROUBLESHOOTING — Решение проблем

---

## Сборка

### `TOPIC_CAMERA_JPEG was not declared in this scope`
Константа не добавлена в `common.hpp`.
```bash
python3 << 'PY'
path = "src/robot_teacher/include/robot_teacher/common.hpp"
with open(path) as f: c = f.read()
if "TOPIC_CAMERA_JPEG" not in c:
    c = c.replace(
        'constexpr const char* TOPIC_GESTURE',
        'constexpr const char* TOPIC_CAMERA_JPEG   = "robot_teacher/camera_jpeg";\nconstexpr const char* TOPIC_GESTURE'
    )
    open(path,"w").write(c)
    print("Fixed")
else:
    print("Already there")
PY
colcon build --packages-select robot_teacher
```

### `history_snap = history_` — ошибка типа deque/vector
```bash
sed -i 's/history_snap = history_;/history_snap.assign(history_.begin(), history_.end());/' \
  src/robot_teacher/src/dialog_node.cpp
colcon build --packages-select robot_teacher
```

### `VisualStudioVersion is not set`
Вы собираете на Windows в обычном PowerShell. Используйте WSL2 или Developer Command Prompt.

### `Address already in use` при запуске UI сервера
```bash
pkill -f robot_ui_server.py
sleep 2
python3 robot_ui_server.py
```

---

## Камера

### `Cannot open camera 0` / `Device is busy`
Камеру держит другой процесс.
```bash
sudo fuser -k /dev/video0 /dev/video1
sleep 2
# Перезапустить робота
```

### `select() timeout` — камера открылась но не даёт кадры
Камера поддерживает только MJPEG, а не YUV. Проверьте:
```bash
v4l2-ctl -d /dev/video0 --list-formats-ext
```
В коде должна быть строка `CAP_PROP_FOURCC = MJPG`. Если нет:
```bash
grep "FOURCC" src/robot_teacher/src/perception_node.cpp
# Должно быть: cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
```

### Камера не найдена в WSL2 (`/dev/video0` нет)
Нужно пробросить через usbipd. В PowerShell от администратора:
```powershell
usbipd list                          # найти BUSID камеры
usbipd bind --busid 1-6             # замените 1-6 на ваш BUSID
usbipd attach --wsl --busid 1-6
```
Проверить в WSL:
```bash
ls /dev/video*
```

### Камера отваливается после переподключения WSL
usbipd не сохраняет attach между сессиями. Добавьте в скрипт запуска:
```powershell
# PowerShell, запускать при каждом старте WSL
$wslIP = (wsl hostname -I).Trim().Split()[0]
usbipd attach --wsl --busid 1-6
```

---

## Аудио / Микрофон

### `arecord failed (code 256)` — микрофон недоступен
PulseAudio не запущен.
```bash
unset PULSE_SERVER           # убрать старую переменную если была
pulseaudio --start --exit-idle-time=-1
sleep 2
arecord -D pulse -f S16_LE -r 16000 -d 3 /tmp/test.wav && echo OK
```

### `User-configured server at tcp:... refusing to start`
Старая переменная `PULSE_SERVER` указывает на несуществующий сервер.
```bash
sed -i '/PULSE_SERVER=tcp/d' ~/.bashrc
unset PULSE_SERVER
pulseaudio --start --exit-idle-time=-1
```

### `No default controller available` (Bluetooth)
Bluetooth адаптер отвалился от WSL.
```powershell
# PowerShell от администратора:
usbipd detach --busid 1-14
usbipd attach --wsl --busid 1-14
```
```bash
sudo systemctl restart bluetooth
bluetoothctl power on
```

### Bluetooth наушники подключаются но микрофон не работает
Наушники работают в A2DP (только воспроизведение). Переключить профиль:
```bash
# Найти имя карты
pactl list cards short
# Переключить на HFP (с микрофоном)
pactl set-card-profile bluez_card.XX_XX_XX_XX_XX_XX handsfree_head_unit
pactl list sources short   # должна появиться строка bluez_source.*
```

### `no soundcards found` даже после настройки
Попробуйте встроенный микрофон ноутбука через WSLg (без Bluetooth):
```bash
pactl info | grep "Server Name"   # должно быть pulseaudio
arecord -D pulse -f S16_LE -r 16000 -d 3 /tmp/test.wav
```
WSLg автоматически пробрасывает встроенный микрофон через RDP.

---

## LLM / API

### `No LLM API keys found — Dialog will use canned responses`
Ключ не передан в ноду. Добавьте в `.bashrc`:
```bash
echo 'export GROQ_API_KEY="gsk_..."' >> ~/.bashrc
source ~/.bashrc
# Перезапустить робота
```

### `(нет ответа от LLM)` — периодически
Groq rate limit на бесплатном тарифе (~30 req/min). Подождите минуту или:
- Создайте второй аккаунт Groq
- Используйте Anthropic Claude: `export ANTHROPIC_API_KEY="sk-ant-..."`

### `Incorrect API key provided: sk-...`
Ключ OpenAI не установлен или неверный. STT работает через Groq ключ — убедитесь что `GROQ_API_KEY` установлен правильно.

### `STT returned empty text` — голос не распознаётся
1. Проверьте что микрофон пишет звук:
```bash
arecord -D pulse -f S16_LE -r 16000 -d 3 /tmp/test.wav
aplay /tmp/test.wav
```
2. Говорите громче и чётче — Whisper чувствителен к качеству аудио
3. Whisper иногда галлюцинирует при тишине (`Продолжение следует...`) — это нормально

---

## ROS2

### `ros2 node list` зависает
ROS2 daemon завис.
```bash
pkill -9 -f "_ros2_daemon"
rm -rf /tmp/ros2_*
ros2 daemon start
ros2 node list
```

### Ноды запустились но топики не публикуются
Не активировано окружение ROS2.
```bash
source /opt/ros/jazzy/setup.bash
source ~/robot_teacher_ws/install/setup.bash
ros2 topic list
```

### `perception_node` не завершается при Ctrl+C (бесконечный SIGINT)
Нода застряла в `cv::waitKey`. Принудительно убить:
```bash
pkill -9 -f perception_node
sudo fuser -k /dev/video0
```
**Никогда не используйте Ctrl+Z** — это замораживает процесс, оставляя камеру занятой.

---

## UI / Браузер

### Страница не открывается на Windows (WSL2)
Нужно пробросить порт из WSL в Windows:
```powershell
# PowerShell от администратора:
$wslIP = (wsl hostname -I).Trim().Split()[0]
netsh interface portproxy add v4tov4 listenport=8888 listenaddress=0.0.0.0 connectport=8888 connectaddress=$wslIP
New-NetFirewallRule -DisplayName "WSL_8888" -Direction Inbound -Protocol TCP -LocalPort 8888 -Action Allow
```
Затем открыть: `http://127.0.0.1:8888`

### События SSE не приходят (диалог и жесты не обновляются)
UI сервер запущен однопоточно или без ROS окружения.
```bash
# Убедитесь что запускаете так:
source /opt/ros/jazzy/setup.bash
source ~/robot_teacher_ws/install/setup.bash
python3 robot_ui_server.py
# В выводе должно быть: [INFO] [robot_ui_bridge]: UI bridge started
```

### Камера чёрная в браузере
UI получает кадры через ROS топик `camera_jpeg`. Проверьте:
```bash
ros2 topic hz /robot_teacher/camera_jpeg
# Должно быть ~15 Hz
```
Если пусто — perception_node не публикует кадры. Пересоберите проект.

### `ERR_CONNECTION_REFUSED` на `/events`
UI сервер упал. Перезапустите:
```bash
pkill -f robot_ui_server.py
sleep 2
python3 robot_ui_server.py
```

---

## Жесты

### Жест `raised_hand` распознаётся постоянно (даже без руки)
Детектор путает тёмный фон с кожей. Улучшите освещение или добавьте белый фон за собой.

### Жест не распознаётся
- Держите руку на расстоянии 40-80 см от камеры
- Убедитесь что рука хорошо освещена
- Жест нужно держать 5+ кадров (дебаунс)
- Тёмные рукава могут мешать skin segmentation

### Жест срабатывает слишком часто (флуд)
В коде есть кулдаун 1 секунда после публикации. Если всё равно флудит — увеличьте значение в `perception_node.cpp`:
```cpp
std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // увеличьте до 2000
```

---

## Общее

### После прощания робот не реагирует на новое появление лица
Нода зависла в состоянии BYE. Подождите 4 секунды — должен автоматически перейти в IDLE. Если нет — перезапустите ноды.

### Система падает при отсутствии ключа LLM
Не должна — предусмотрен fallback с фиксированными ответами. Если падает — проверьте версию сборки (`colcon build` был выполнен после последних изменений кода?).

### Высокая загрузка CPU
Обработка кадров (детекция лиц + жестов) нагружает CPU. Снизить FPS захвата:
В `perception_node.cpp` измените:
```cpp
cap.set(cv::CAP_PROP_FPS, 15);  // вместо 30
```
И пересоберите.

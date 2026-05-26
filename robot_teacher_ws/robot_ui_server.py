#!/usr/bin/env python3
"""
robot_ui_server.py — веб-дашборд для робота-педагога

Запуск:
    source /opt/ros/jazzy/setup.bash
    source ~/robot_teacher_ws/install/setup.bash
    python3 robot_ui_server.py

Открыть в браузере:
    Нативно:  http://localhost:8888
    WSL2:     http://<WSL_IP>:8888  (IP из: hostname -I)

Читает ROS2 топики и стримит данные в браузер через SSE.
Получает кадры с камеры через топик camera_jpeg (base64 JPEG).
"""

import json
import time
import threading
import base64
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn
from urllib.parse import urlparse

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

# ─────────────────────────────────────────────────────────────
#  Общее состояние (защищено мьютексом)
# ─────────────────────────────────────────────────────────────
state = {
    "session_state": "IDLE",
    "face_present":  False,
    "gesture":       "",
    "last_speech":   "",
}
state_lock = threading.Lock()

# Список SSE-клиентов (браузерных соединений)
sse_clients = []
sse_lock    = threading.Lock()

# Последний JPEG кадр от камеры
camera_frame_lock = threading.Lock()
latest_jpeg = b""


# ─────────────────────────────────────────────────────────────
#  ROS2 bridge — подписывается на все топики робота
# ─────────────────────────────────────────────────────────────
class UIBridgeNode(Node):
    def __init__(self):
        super().__init__("robot_ui_bridge")
        self.create_subscription(String, "robot_teacher/face_present",  self._on_face,    10)
        self.create_subscription(String, "robot_teacher/gesture",       self._on_gesture, 10)
        self.create_subscription(String, "robot_teacher/speech_text",   self._on_speech,  10)
        self.create_subscription(String, "robot_teacher/dialog_output", self._on_dialog,  10)
        # Кадры с камеры публикует perception_node как base64 JPEG
        self.create_subscription(String, "robot_teacher/camera_jpeg",   self._on_camera,  1)
        self.get_logger().info("UI bridge started")

    def _on_camera(self, msg):
        """Декодирует base64 JPEG и сохраняет для MJPEG стрима."""
        global latest_jpeg
        try:
            with camera_frame_lock:
                latest_jpeg = base64.b64decode(msg.data)
        except Exception:
            pass

    def _on_face(self, msg):
        with state_lock:
            state["face_present"] = (msg.data == "detected")
        push_event("face", msg.data)

    def _on_gesture(self, msg):
        # Таблица: ID жеста → эмодзи иконка
        icons = {
            "thumb_up":    "👍",
            "thumb_down":  "👎",
            "point":       "👉",
            "raised_hand": "✋",
            "open_palm":   "🤚",
            "wave":        "👋",
            "ok":          "👌",
            "crossed_arms":"🙅",
        }
        icon  = icons.get(msg.data, "🤖")
        label = msg.data.replace("_", " ").title()
        with state_lock:
            state["gesture"] = msg.data
        push_event("gesture", json.dumps({"icon": icon, "label": label}))

    def _on_speech(self, msg):
        # Фильтруем системные заглушки
        if not msg.data or msg.data.startswith("("):
            return
        with state_lock:
            state["last_speech"] = msg.data
        push_event("speech", msg.data)

    def _on_dialog(self, msg):
        # Обновляем состояние сессии по содержимому ответа
        with state_lock:
            if "🎤" in msg.data:
                state["session_state"] = "LISTENING"
            elif "⏸" in msg.data:
                state["session_state"] = "PAUSED"
            elif state["face_present"]:
                state["session_state"] = "DIALOG"
        push_event("dialog", msg.data)


def push_event(event_type: str, data: str):
    """Рассылает SSE событие всем подключённым браузерам."""
    msg = f"event: {event_type}\ndata: {data}\n\n".encode()
    with sse_lock:
        dead = []
        for client in sse_clients:
            try:
                client.wfile.write(msg)
                client.wfile.flush()
            except Exception:
                dead.append(client)
        for d in dead:
            sse_clients.remove(d)


# ─────────────────────────────────────────────────────────────
#  HTML / CSS / JS — весь фронтенд в одной строке
# ─────────────────────────────────────────────────────────────
HTML = """<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ботик UI</title>
<link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Unbounded:wght@300;600;900&display=swap" rel="stylesheet">
<style>
:root{--bg:#090c10;--panel:#0d1117;--border:#1e2630;--accent:#00e5ff;--accent2:#7b2fff;--green:#00ff88;--yellow:#ffd600;--red:#ff3d5a;--text:#c9d8e8;--dim:#4a5568;--mono:"Share Tech Mono",monospace;--display:"Unbounded",sans-serif}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:var(--mono);height:100vh;display:grid;grid-template-rows:56px 1fr;overflow:hidden}
header{display:flex;align-items:center;gap:16px;padding:0 24px;border-bottom:1px solid var(--border);background:var(--panel)}
.logo{font-family:var(--display);font-weight:900;font-size:18px;color:var(--accent)}.logo span{color:var(--accent2)}
.sep{flex:1}
.pill{display:flex;align-items:center;gap:8px;font-size:12px;padding:4px 12px;border-radius:20px;border:1px solid var(--border);text-transform:uppercase;letter-spacing:1px;transition:all .3s}
.pill .dot{width:8px;height:8px;border-radius:50%;background:var(--dim);transition:all .3s}
.pill.active .dot{background:var(--green);box-shadow:0 0 8px var(--green)}
.pill.listen .dot{background:var(--accent);box-shadow:0 0 8px var(--accent);animation:pulse 1s infinite}
.pill.paused .dot{background:var(--yellow);box-shadow:0 0 8px var(--yellow)}
.pill.bye .dot{background:var(--red);box-shadow:0 0 8px var(--red)}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
main{display:grid;grid-template-columns:380px 1fr 280px;gap:1px;background:var(--border);overflow:hidden}
.panel{background:var(--panel);display:flex;flex-direction:column;overflow:hidden}
.ph{padding:12px 16px;font-family:var(--display);font-size:10px;font-weight:600;letter-spacing:2px;text-transform:uppercase;color:var(--dim);border-bottom:1px solid var(--border);flex-shrink:0}

/* Камера */
.cw{flex:1;display:flex;align-items:center;justify-content:center;background:#000;position:relative;overflow:hidden}
#cam{width:100%;height:100%;object-fit:cover}
.corner{position:absolute;width:20px;height:20px;border-color:var(--accent);border-style:solid;opacity:.5}
.tl{top:8px;left:8px;border-width:2px 0 0 2px}
.tr{top:8px;right:8px;border-width:2px 2px 0 0}
.bl{bottom:8px;left:8px;border-width:0 0 2px 2px}
.br{bottom:8px;right:8px;border-width:0 2px 2px 0}
#fb{padding:8px 16px;display:flex;align-items:center;gap:10px;font-size:12px;border-top:1px solid var(--border);flex-shrink:0}
#fi{font-size:20px;transition:all .3s;filter:grayscale(1)}
#fi.on{filter:none}
#fs{color:var(--dim);font-size:11px}

/* Блок текущего жеста */
#cur-gesture{padding:10px 16px;display:flex;align-items:center;gap:12px;border-top:1px solid var(--border);flex-shrink:0;min-height:56px;background:rgba(0,229,255,.02);transition:background .3s}
#cur-gesture.active{background:rgba(0,229,255,.06)}
#cg-icon{font-size:28px;transition:all .3s;filter:grayscale(1)}
#cg-icon.on{filter:none;animation:gestPop .2s ease}
@keyframes gestPop{0%{transform:scale(.7)}60%{transform:scale(1.2)}100%{transform:scale(1)}}
#cg-meta{flex:1}
#cg-title{font-size:9px;letter-spacing:1.5px;text-transform:uppercase;color:var(--dim);font-family:var(--display);font-weight:600}
#cg-label{font-size:13px;color:var(--dim);margin-top:3px;transition:color .3s}
#cg-label.on{color:var(--accent)}
#cg-bar{width:4px;height:36px;border-radius:2px;background:var(--border);overflow:hidden;position:relative}
#cg-fill{position:absolute;bottom:0;left:0;right:0;height:0%;background:var(--accent);transition:height .3s ease;border-radius:2px}

/* Диалог */
#ds{flex:1;overflow-y:auto;padding:16px;display:flex;flex-direction:column;gap:12px;scroll-behavior:smooth}
#ds::-webkit-scrollbar{width:4px}
#ds::-webkit-scrollbar-thumb{background:var(--border)}
.bubble{max-width:85%;padding:10px 14px;border-radius:12px;font-size:13px;line-height:1.6;animation:bi .2s ease}
.bubble.user{align-self:flex-end;background:rgba(0,229,255,.08);border:1px solid rgba(0,229,255,.2);color:var(--accent)}
.bubble.bot{align-self:flex-start;background:rgba(123,47,255,.08);border:1px solid rgba(123,47,255,.2);color:#c4b5fd}
.bubble .who{font-size:10px;letter-spacing:1px;text-transform:uppercase;opacity:.5;margin-bottom:4px}
@keyframes bi{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}
#typing{display:none;align-self:flex-start;padding:10px 14px;border-radius:12px;background:rgba(123,47,255,.08);border:1px solid rgba(123,47,255,.2);gap:4px;align-items:center}
#typing.show{display:flex}
.td{width:6px;height:6px;border-radius:50%;background:var(--accent2);animation:tb 1.2s infinite}
.td:nth-child(2){animation-delay:.2s}.td:nth-child(3){animation-delay:.4s}
@keyframes tb{0%,60%,100%{transform:translateY(0)}30%{transform:translateY(-6px)}}
#stt{padding:10px 16px;border-top:1px solid var(--border);font-size:11px;color:var(--dim);display:flex;align-items:center;gap:8px;flex-shrink:0;min-height:40px}
#st{color:var(--accent);font-style:italic}

/* Правая панель: состояние + жесты */
.sg{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-top:10px}
.sn{padding:8px 10px;border-radius:8px;font-size:11px;text-align:center;letter-spacing:.5px;border:1px solid var(--border);color:var(--dim);transition:all .3s;text-transform:uppercase}
.sn.active{border-color:var(--accent);color:var(--accent);background:rgba(0,229,255,.06);box-shadow:0 0 12px rgba(0,229,255,.1)}
.sn.active.ls{border-color:var(--accent2);color:var(--accent2);background:rgba(123,47,255,.06)}
.sn.active.ps{border-color:var(--yellow);color:var(--yellow)}
.sn.active.by{border-color:var(--red);color:var(--red)}
#gp{padding:16px;flex:1;overflow:hidden;display:flex;flex-direction:column}
.gl{margin-top:10px;display:flex;flex-direction:column;gap:6px;overflow:hidden}
.ge{display:flex;align-items:center;gap:10px;padding:8px 10px;border-radius:8px;background:rgba(255,255,255,.02);border:1px solid var(--border);animation:gi .2s ease;font-size:12px}
@keyframes gi{from{opacity:0;transform:translateX(8px)}to{opacity:1;transform:translateX(0)}}
.gi2{font-size:18px}.gt{margin-left:auto;color:var(--dim);font-size:10px}
</style>
</head>
<body>
<header>
  <div class="logo">БОТИК<span>.</span>UI</div>
  <div class="sep"></div>
  <div class="pill" id="pill"><div class="dot"></div><span id="sl">IDLE</span></div>
</header>
<main>

  <!-- Левая панель: камера + текущий жест -->
  <div class="panel">
    <div class="ph">📷 Камера</div>
    <div class="cw">
      <img id="cam" src="/stream" alt="cam">
      <div class="corner tl"></div><div class="corner tr"></div>
      <div class="corner bl"></div><div class="corner br"></div>
    </div>
    <div id="fb">
      <span id="fi">😶</span>
      <span id="fs">Лицо не обнаружено</span>
    </div>
    <div id="cur-gesture">
      <span id="cg-icon">🤖</span>
      <div id="cg-meta">
        <div id="cg-title">Текущий жест</div>
        <div id="cg-label">—</div>
      </div>
      <div id="cg-bar"><div id="cg-fill"></div></div>
    </div>
  </div>

  <!-- Центральная панель: диалог -->
  <div class="panel">
    <div class="ph">💬 Диалог</div>
    <div id="ds">
      <div id="hint" style="text-align:center;color:var(--dim);font-size:12px;margin-top:40px">
        Встаньте перед камерой
      </div>
      <div id="typing">
        <div class="td"></div><div class="td"></div><div class="td"></div>
      </div>
    </div>
    <div id="stt"><span>🎤</span><span id="st">—</span></div>
  </div>

  <!-- Правая панель: состояния + лог жестов -->
  <div class="panel">
    <div style="padding:16px;border-bottom:1px solid var(--border);flex-shrink:0">
      <div style="font-family:var(--display);font-size:10px;font-weight:600;letter-spacing:2px;color:var(--dim);text-transform:uppercase">⚡ Состояние</div>
      <div class="sg">
        <div class="sn"        id="s0">Idle</div>
        <div class="sn"        id="s1">Greeting</div>
        <div class="sn"        id="s2">Dialog</div>
        <div class="sn ls"     id="s3">Listening</div>
        <div class="sn ps"     id="s4">Paused</div>
        <div class="sn by"     id="s5">Bye</div>
      </div>
    </div>
    <div id="gp">
      <div style="font-family:var(--display);font-size:10px;font-weight:600;letter-spacing:2px;color:var(--dim);text-transform:uppercase">✋ История жестов</div>
      <div class="gl" id="gl"></div>
    </div>
  </div>

</main>
<script>
// ── Состояния ──────────────────────────────────────────────
const sns = {IDLE:s0,GREETING:s1,DIALOG:s2,LISTENING:s3,PAUSED:s4,BYE:s5};
let cs = "IDLE";

function setState(s) {
  cs = s.toUpperCase();
  Object.values(sns).forEach(n => n.classList.remove("active"));
  if (sns[cs]) sns[cs].classList.add("active");
  sl.textContent = cs;
  pill.className = "pill";
  if (["DIALOG","GREETING"].includes(cs)) pill.classList.add("active");
  else if (cs === "LISTENING") pill.classList.add("listen");
  else if (cs === "PAUSED")    pill.classList.add("paused");
  else if (cs === "BYE")       pill.classList.add("bye");
}

// ── Диалог ─────────────────────────────────────────────────
function addBubble(role, text) {
  const h = document.getElementById("hint");
  if (h) h.remove();
  ds.appendChild(typing);
  const d = document.createElement("div");
  d.className = `bubble ${role}`;
  d.innerHTML = `<div class="who">${role==="user"?"Вы":"Ботик"}</div>${text}`;
  ds.insertBefore(d, typing);
  ds.scrollTop = ds.scrollHeight;
}

// ── Текущий жест (крупный блок под камерой) ────────────────
let gestureTimer = null;

function showCurrentGesture(icon, label) {
  const cgIcon   = document.getElementById("cg-icon");
  const cgLabel  = document.getElementById("cg-label");
  const cgFill   = document.getElementById("cg-fill");
  const cgBlock  = document.getElementById("cur-gesture");

  // Сбросить анимацию для перезапуска
  cgIcon.classList.remove("on");
  void cgIcon.offsetWidth; // reflow

  cgIcon.textContent  = icon;
  cgLabel.textContent = label;
  cgIcon.classList.add("on");
  cgLabel.classList.add("on");
  cgFill.style.height = "100%";
  cgBlock.classList.add("active");

  clearTimeout(gestureTimer);
  gestureTimer = setTimeout(() => {
    cgIcon.classList.remove("on");
    cgLabel.classList.remove("on");
    cgFill.style.height = "0%";
    cgBlock.classList.remove("active");
    cgLabel.textContent = "—";
    cgIcon.textContent  = "🤖";
  }, 2000);
}

// ── История жестов (правая панель) ─────────────────────────
function addGestureLog(icon, label) {
  const now = new Date();
  const ts  = now.getHours().toString().padStart(2,"0") + ":" +
              now.getMinutes().toString().padStart(2,"0") + ":" +
              now.getSeconds().toString().padStart(2,"0");
  const e = document.createElement("div");
  e.className = "ge";
  e.innerHTML = `<span class="gi2">${icon}</span><span>${label}</span><span class="gt">${ts}</span>`;
  gl.prepend(e);
  while (gl.children.length > 8) gl.removeChild(gl.lastChild);
}

// ── SSE — получение событий от робота ──────────────────────
const es = new EventSource("/events");

es.addEventListener("face", e => {
  const detected = e.data === "detected";
  fi.textContent    = detected ? "😊" : "😶";
  fi.className      = detected ? "on" : "";
  fs.textContent    = detected ? "Лицо обнаружено" : "Лицо не обнаружено";
  if (detected && cs === "IDLE") setState("GREETING");
  if (!detected && !["IDLE","BYE"].includes(cs)) setState("BYE");
});

es.addEventListener("gesture", e => {
  const g = JSON.parse(e.data);
  showCurrentGesture(g.icon, g.label);
  addGestureLog(g.icon, g.label);
  if      (g.label.toLowerCase().includes("raised")) setState("LISTENING");
  else if (g.label.toLowerCase().includes("open"))   setState("PAUSED");
  else if (cs !== "PAUSED") setState("DIALOG");
});

es.addEventListener("speech", e => {
  st.textContent = e.data;
  addBubble("user", e.data);
  typing.classList.add("show");
  ds.scrollTop = ds.scrollHeight;
});

es.addEventListener("dialog", e => {
  typing.classList.remove("show");
  const t = e.data;
  if      (t.includes("🎤")) { setState("LISTENING"); st.textContent = "Слушаю..."; }
  else if (t.includes("⏸")) setState("PAUSED");
  else setState("DIALOG");
  addBubble("bot", t);
});

es.onerror = () => console.log("SSE reconnecting...");
</script>
</body>
</html>"""


# ─────────────────────────────────────────────────────────────
#  HTTP обработчик запросов
# ─────────────────────────────────────────────────────────────
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass  # отключить стандартный лог запросов

    def do_GET(self):
        path = urlparse(self.path).path

        if path in ("/", "/index.html"):
            # Главная страница
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(HTML.encode())

        elif path == "/events":
            # SSE endpoint — держит соединение открытым, рассылает события
            self.send_response(200)
            self.send_header("Content-Type",  "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection",    "keep-alive")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            with sse_lock:
                sse_clients.append(self)
            try:
                while True:
                    time.sleep(15)
                    # Heartbeat — не даёт прокси закрыть соединение
                    self.wfile.write(b": heartbeat\n\n")
                    self.wfile.flush()
            except Exception:
                with sse_lock:
                    if self in sse_clients:
                        sse_clients.remove(self)

        elif path == "/stream":
            # MJPEG стрим — кадры с камеры через ROS топик
            self.send_response(200)
            self.send_header("Content-Type",  "multipart/x-mixed-replace; boundary=frame")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            try:
                while True:
                    with camera_frame_lock:
                        jpg = latest_jpeg
                    if jpg:
                        self.wfile.write(
                            b"--frame\r\n"
                            b"Content-Type: image/jpeg\r\n\r\n" +
                            jpg + b"\r\n"
                        )
                        self.wfile.flush()
                    time.sleep(1 / 15)
            except Exception:
                pass

        else:
            self.send_response(404)
            self.end_headers()


# ─────────────────────────────────────────────────────────────
#  Многопоточный HTTP сервер
# ─────────────────────────────────────────────────────────────
class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    """Каждый запрос обрабатывается в отдельном потоке.
    Критично для SSE: одно длинное соединение не блокирует остальные запросы."""
    daemon_threads = True


# ─────────────────────────────────────────────────────────────
#  Точка входа
# ─────────────────────────────────────────────────────────────
def ros_thread():
    """Запускает ROS2 ноду в фоновом потоке."""
    rclpy.init()
    node = UIBridgeNode()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()


def main():
    # Запустить ROS2 bridge в фоне
    t = threading.Thread(target=ros_thread, daemon=True)
    t.start()

    # Запустить HTTP сервер
    port = 8888
    server = ThreadedHTTPServer(("0.0.0.0", port), Handler)
    print(f"UI: http://localhost:{port}")
    print(f"    WSL2: http://$(hostname -I | awk '{{print $1}}'):{port}")
    print("    Ctrl+C для остановки")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nUI сервер остановлен.")


if __name__ == "__main__":
    main()

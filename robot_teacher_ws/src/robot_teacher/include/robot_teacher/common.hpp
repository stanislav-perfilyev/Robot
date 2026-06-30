#pragma once

#include <string>
#include <cstdint>

// ============================================================
//  Topic names  (single source of truth)
// ============================================================
constexpr const char* TOPIC_GESTURE       = "robot_teacher/gesture";
constexpr const char* TOPIC_FACE_PRESENT  = "robot_teacher/face_present";
constexpr const char* TOPIC_SPEECH_TEXT   = "robot_teacher/speech_text";
constexpr const char* TOPIC_LISTEN_CMD    = "robot_teacher/listen_command";
constexpr const char* TOPIC_DIALOG_OUT    = "robot_teacher/dialog_output";

// ============================================================
//  Gesture IDs  (published as std_msgs/String data field)
// ============================================================
constexpr const char* GESTURE_THUMB_UP    = "thumb_up";      // Yes / понятно
constexpr const char* GESTURE_THUMB_DOWN  = "thumb_down";    // No / не нравится
constexpr const char* GESTURE_POINT       = "point";         // Расскажи о себе
constexpr const char* GESTURE_RAISED_HAND = "raised_hand";   // Вопрос (→ STT)
constexpr const char* GESTURE_OPEN_PALM   = "open_palm";     // Стоп / пауза
constexpr const char* GESTURE_WAVE        = "wave";          // Привет / пока
// Bonus gestures
constexpr const char* GESTURE_OK          = "ok";            // OK sign
constexpr const char* GESTURE_CROSSED     = "crossed_arms";  // Несогласие / нет

// ============================================================
//  Listen command values
// ============================================================
constexpr const char* LISTEN_START = "start";
constexpr const char* LISTEN_STOP  = "stop";

// ============================================================
//  Face presence values
// ============================================================
constexpr const char* FACE_DETECTED = "detected";
constexpr const char* FACE_LOST     = "lost";

// Copyright 2026 Candidate
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#include <string>
#include <cstdint>

// ============================================================
//  Topic names  (single source of truth)
// ============================================================
constexpr const char * TOPIC_GESTURE = "robot_teacher/gesture";
constexpr const char * TOPIC_FACE_PRESENT = "robot_teacher/face_present";
constexpr const char * TOPIC_SPEECH_TEXT = "robot_teacher/speech_text";
constexpr const char * TOPIC_LISTEN_CMD = "robot_teacher/listen_command";
constexpr const char * TOPIC_DIALOG_OUT = "robot_teacher/dialog_output";

// ============================================================
//  Gesture IDs  (published as std_msgs/String data field)
// ============================================================
constexpr const char * GESTURE_THUMB_UP = "thumb_up";        // Yes / понятно
constexpr const char * GESTURE_THUMB_DOWN = "thumb_down";    // No / не нравится
constexpr const char * GESTURE_POINT = "point";              // Расскажи о себе
constexpr const char * GESTURE_RAISED_HAND = "raised_hand";   // Вопрос (→ STT)
constexpr const char * GESTURE_OPEN_PALM = "open_palm";      // Стоп / пауза
constexpr const char * GESTURE_WAVE = "wave";                // Привет / пока
// Bonus gestures
constexpr const char * GESTURE_OK = "ok";                    // OK sign
constexpr const char * GESTURE_CROSSED = "crossed_arms";     // Несогласие / нет

// ============================================================
//  Listen command values
// ============================================================
constexpr const char * LISTEN_START = "start";
constexpr const char * LISTEN_STOP = "stop";

// ============================================================
//  Face presence values
// ============================================================
constexpr const char * FACE_DETECTED = "detected";
constexpr const char * FACE_LOST = "lost";

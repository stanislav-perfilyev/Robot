#pragma once

#include <string>

namespace robot_teacher {

/// Dialog session states. Drives what gestures/speech mean at any given
/// moment (e.g. RAISED_HAND only starts STT while in DIALOG/GREETING).
enum class SessionState {
    IDLE,       ///< no face
    GREETING,   ///< face appeared, greeting in progress
    DIALOG,     ///< normal gesture/voice interaction
    LISTENING,  ///< waiting for voice input (STT active)
    PAUSED,     ///< open-palm stop
    BYE         ///< face left, farewell in progress
};

/// Human-readable name for a SessionState, used in log lines.
[[nodiscard]] const char* stateName(SessionState s);

/**
 * Escapes a string for safe embedding inside a JSON string literal:
 * quotes, backslashes, and the common whitespace control characters
 * (\\n \\r \\t). Does not escape other control characters or attempt
 * full JSON-string validation — sufficient for the LLM prompt text
 * this project sends, which is plain conversational Russian/English.
 */
[[nodiscard]] std::string jsonEscape(const std::string& s);

/**
 * Extracts the assistant's reply text from a Claude or OpenAI chat
 * completion JSON response body using a minimal hand-rolled scan
 * (avoids pulling in a full JSON library for a single field).
 *
 * @param body         Raw HTTP response body.
 * @param is_anthropic true → look for `"text":"..."` (Anthropic content
 *                     block format); false → look for `"content":"..."`
 *                     (OpenAI chat completion format).
 * @return Decoded reply text, or empty string if the key wasn't found.
 */
[[nodiscard]] std::string parseContent(const std::string& body, bool is_anthropic);

}  // namespace robot_teacher

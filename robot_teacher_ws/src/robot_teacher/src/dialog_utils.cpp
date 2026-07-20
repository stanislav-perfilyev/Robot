#include "robot_teacher/dialog_utils.hpp"

namespace robot_teacher {

const char* stateName(SessionState s)
{
    switch (s) {
        case SessionState::IDLE:      return "IDLE";
        case SessionState::GREETING:  return "GREETING";
        case SessionState::DIALOG:    return "DIALOG";
        case SessionState::LISTENING: return "LISTENING";
        case SessionState::PAUSED:    return "PAUSED";
        case SessionState::BYE:       return "BYE";
    }
    return "?";
}

std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += static_cast<char>(c);
    }
    return out;
}

std::string parseContent(const std::string& body, bool is_anthropic)
{
    // Anthropic: "text":"..."  inside content array
    // OpenAI:    "content":"..."
    const std::string key = is_anthropic ? "\"text\":" : "\"content\":";
    auto pos = body.find(key);
    if (pos == std::string::npos) return "";
    pos += key.size();
    // skip whitespace
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\n')) ++pos;
    if (pos >= body.size() || body[pos] != '"') return "";
    ++pos;
    std::string result;
    while (pos < body.size()) {
        if (body[pos] == '\\' && pos + 1 < body.size()) {
            char next = body[pos + 1];
            if (next == '"')  { result += '"';  pos += 2; continue; }
            if (next == '\\') { result += '\\'; pos += 2; continue; }
            if (next == 'n')  { result += '\n'; pos += 2; continue; }
            if (next == 't')  { result += '\t'; pos += 2; continue; }
            result += body[pos]; pos++;
        } else if (body[pos] == '"') {
            break;
        } else {
            result += body[pos]; pos++;
        }
    }
    return result;
}

}  // namespace robot_teacher

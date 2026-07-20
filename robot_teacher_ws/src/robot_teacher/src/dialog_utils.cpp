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

#include "robot_teacher/dialog_utils.hpp"

namespace robot_teacher
{

const char * stateName(SessionState s)
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

std::string jsonEscape(const std::string & s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    if      (c == '"') {out += "\\\"";} else if (c == '\\') {out += "\\\\";} else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {out += "\\r";} else if (c == '\t') {out += "\\t";} else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

std::string parseContent(const std::string & body, bool is_anthropic)
{
    // Anthropic: "text":"..."  inside content array
    // OpenAI:    "content":"..."
  const std::string key = is_anthropic ? "\"text\":" : "\"content\":";
  auto pos = body.find(key);
  if (pos == std::string::npos) {return "";}
  pos += key.size();
    // skip whitespace
  while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\n')) {++pos;}
  if (pos >= body.size() || body[pos] != '"') {return "";}
  ++pos;
  std::string result;
  while (pos < body.size()) {
    if (body[pos] == '\\' && pos + 1 < body.size()) {
      char next = body[pos + 1];
      if (next == '"') {result += '"';  pos += 2; continue;}
      if (next == '\\') {result += '\\'; pos += 2; continue;}
      if (next == 'n') {result += '\n'; pos += 2; continue;}
      if (next == 't') {result += '\t'; pos += 2; continue;}
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

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

#include <cstddef>

#include "robot_teacher/wav_utils.hpp"

namespace robot_teacher
{

std::vector<uint8_t> buildWav(
  const std::vector<int16_t> & samples,
  uint32_t sample_rate,
  uint16_t channels)
{
  const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
  const uint32_t file_bytes = 36 + data_bytes;

  std::vector<uint8_t> wav;
  wav.reserve(44 + data_bytes);

  auto push4 = [&](uint32_t v) {
      wav.push_back(v & 0xFF);
      wav.push_back((v >> 8) & 0xFF);
      wav.push_back((v >> 16) & 0xFF);
      wav.push_back((v >> 24) & 0xFF);
    };
  auto push2 = [&](uint16_t v) {
      wav.push_back(v & 0xFF);
      wav.push_back((v >> 8) & 0xFF);
    };
  auto pushStr = [&](const char * s, size_t n) {
      for (size_t i = 0; i < n; ++i) {
        wav.push_back(static_cast<uint8_t>(s[i]));
      }
    };

  pushStr("RIFF", 4);
  push4(file_bytes);
  pushStr("WAVE", 4);
  pushStr("fmt ", 4);
  push4(16);              // chunk size
  push2(1);                // PCM
  push2(channels);
  push4(sample_rate);
  push4(sample_rate * channels * 2);    // byte rate
  push2(static_cast<uint16_t>(channels * 2));    // block align
  push2(16);                                      // bits per sample
  pushStr("data", 4);
  push4(data_bytes);

  const auto * raw = reinterpret_cast<const uint8_t *>(samples.data());
  wav.insert(wav.end(), raw, raw + data_bytes);
  return wav;
}

}  // namespace robot_teacher

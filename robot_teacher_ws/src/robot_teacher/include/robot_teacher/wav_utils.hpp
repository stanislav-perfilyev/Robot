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

#include <cstdint>
#include <vector>

namespace robot_teacher
{

/**
 * Builds a minimal 16-bit PCM WAV file (44-byte canonical header + raw
 * sample data) from a buffer of signed 16-bit samples.
 *
 * Used by hearing_node to package captured microphone audio for upload
 * to the Whisper transcription API, which requires a WAV/multipart body.
 *
 * @param samples     Raw PCM samples, interleaved if channels > 1.
 * @param sample_rate Sample rate in Hz (default 16000, Whisper's preferred rate).
 * @param channels    Channel count (default 1 = mono).
 * @return Byte buffer containing a complete, valid WAV file.
 */
[[nodiscard]] std::vector<uint8_t> buildWav(
  const std::vector<int16_t> & samples,
  uint32_t sample_rate = 16000,
  uint16_t channels = 1);

}  // namespace robot_teacher

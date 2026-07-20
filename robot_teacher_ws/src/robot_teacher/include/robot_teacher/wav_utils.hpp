#pragma once

#include <cstdint>
#include <vector>

namespace robot_teacher {

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
    const std::vector<int16_t>& samples,
    uint32_t sample_rate = 16000,
    uint16_t channels    = 1);

}  // namespace robot_teacher

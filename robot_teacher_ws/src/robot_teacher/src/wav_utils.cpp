#include "robot_teacher/wav_utils.hpp"

#include <cstddef>

namespace robot_teacher {

std::vector<uint8_t> buildWav(
    const std::vector<int16_t>& samples,
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
    auto pushStr = [&](const char* s, size_t n) {
        for (size_t i = 0; i < n; ++i) wav.push_back(static_cast<uint8_t>(s[i]));
    };

    pushStr("RIFF", 4);
    push4(file_bytes);
    pushStr("WAVE", 4);
    pushStr("fmt ", 4);
    push4(16);            // chunk size
    push2(1);              // PCM
    push2(channels);
    push4(sample_rate);
    push4(sample_rate * channels * 2);  // byte rate
    push2(static_cast<uint16_t>(channels * 2));  // block align
    push2(16);                                    // bits per sample
    pushStr("data", 4);
    push4(data_bytes);

    const auto* raw = reinterpret_cast<const uint8_t*>(samples.data());
    wav.insert(wav.end(), raw, raw + data_bytes);
    return wav;
}

}  // namespace robot_teacher

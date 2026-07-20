#include <gtest/gtest.h>

#include "robot_teacher/wav_utils.hpp"

using robot_teacher::buildWav;

namespace {
constexpr size_t kWavHeaderBytes = 44;

std::string chunkId(const std::vector<uint8_t>& wav, size_t offset) {
    return std::string(wav.begin() + static_cast<long>(offset),
                        wav.begin() + static_cast<long>(offset) + 4);
}

uint32_t readLE32(const std::vector<uint8_t>& wav, size_t offset) {
    return static_cast<uint32_t>(wav[offset]) |
           (static_cast<uint32_t>(wav[offset + 1]) << 8) |
           (static_cast<uint32_t>(wav[offset + 2]) << 16) |
           (static_cast<uint32_t>(wav[offset + 3]) << 24);
}

uint16_t readLE16(const std::vector<uint8_t>& wav, size_t offset) {
    return static_cast<uint16_t>(wav[offset] | (wav[offset + 1] << 8));
}
}  // namespace

TEST(WavUtils, HeaderMagicBytesArePresent) {
    const auto wav = buildWav({1, 2, 3, 4});
    EXPECT_EQ(chunkId(wav, 0), "RIFF");
    EXPECT_EQ(chunkId(wav, 8), "WAVE");
    EXPECT_EQ(chunkId(wav, 12), "fmt ");
    EXPECT_EQ(chunkId(wav, 36), "data");
}

TEST(WavUtils, TotalSizeIsHeaderPlusSampleBytes) {
    const std::vector<int16_t> samples(100, 0);
    const auto wav = buildWav(samples);
    EXPECT_EQ(wav.size(), kWavHeaderBytes + samples.size() * sizeof(int16_t));
}

TEST(WavUtils, EmptySamplesProducesHeaderOnly) {
    const auto wav = buildWav({});
    EXPECT_EQ(wav.size(), kWavHeaderBytes);
    // RIFF chunk size = 36 + data_bytes = 36 + 0
    EXPECT_EQ(readLE32(wav, 4), 36u);
    // data chunk size = 0
    EXPECT_EQ(readLE32(wav, 40), 0u);
}

TEST(WavUtils, FmtChunkEncodesPcmMonoDefaults) {
    const auto wav = buildWav({1, 2, 3});
    EXPECT_EQ(readLE32(wav, 16), 16u);   // fmt chunk size
    EXPECT_EQ(readLE16(wav, 20), 1u);    // PCM
    EXPECT_EQ(readLE16(wav, 22), 1u);    // channels = 1 (default)
    EXPECT_EQ(readLE32(wav, 24), 16000u);  // sample_rate default
    EXPECT_EQ(readLE16(wav, 34), 16u);   // bits per sample
}

TEST(WavUtils, RespectsCustomSampleRateAndChannels) {
    const auto wav = buildWav({1, 2, 3, 4}, 44100, 2);
    EXPECT_EQ(readLE32(wav, 24), 44100u);          // sample_rate
    EXPECT_EQ(readLE16(wav, 22), 2u);              // channels
    EXPECT_EQ(readLE32(wav, 28), 44100u * 2 * 2);  // byte rate
    EXPECT_EQ(readLE16(wav, 32), 4u);              // block align = channels*2
}

TEST(WavUtils, SampleDataIsCopiedVerbatim) {
    const std::vector<int16_t> samples = {-32768, -1, 0, 1, 32767};
    const auto wav = buildWav(samples);
    const auto* raw = reinterpret_cast<const int16_t*>(wav.data() + kWavHeaderBytes);
    for (size_t i = 0; i < samples.size(); ++i) {
        EXPECT_EQ(raw[i], samples[i]) << "sample index " << i;
    }
}

// main() is supplied by GTest::gtest_main, linked automatically by
// ament_add_gtest() — do not define one here (would be a duplicate symbol).

/**
 * hearing_node.cpp
 *
 * ROS2 node: Слух
 * - Слушает команду LISTEN_START/STOP от dialog_node
 * - При получении START: записывает аудио с микрофона (PortAudio),
 *   применяет таймаут молчания 5–7 с
 * - Отправляет WAV-буфер в OpenAI Whisper API (или заглушку без ключа)
 * - Публикует распознанную фразу в TOPIC_SPEECH_TEXT
 *
 * Потоки:
 *   audio_thread_  — захват PortAudio (независимо от ROS spin)
 *   stt_thread_    — HTTP-запрос к Whisper API (не блокирует остальное)
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include <curl/curl.h>

#ifdef HAVE_PORTAUDIO
  #include <portaudio.h>
#endif

#include "robot_teacher/common.hpp"

using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────
//  WAV helpers
// ─────────────────────────────────────────────────────────────
static std::vector<uint8_t> buildWav(
    const std::vector<int16_t>& samples,
    uint32_t sample_rate = 16000,
    uint16_t channels    = 1)
{
    uint32_t data_bytes  = samples.size() * sizeof(int16_t);
    uint32_t file_bytes  = 36 + data_bytes;

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
        for (size_t i = 0; i < n; ++i) wav.push_back(s[i]);
    };

    pushStr("RIFF", 4);
    push4(file_bytes);
    pushStr("WAVE", 4);
    pushStr("fmt ", 4);
    push4(16);            // chunk size
    push2(1);             // PCM
    push2(channels);
    push4(sample_rate);
    push4(sample_rate * channels * 2);  // byte rate
    push2(channels * 2);                // block align
    push2(16);                          // bits per sample
    pushStr("data", 4);
    push4(data_bytes);

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(samples.data());
    wav.insert(wav.end(), raw, raw + data_bytes);
    return wav;
}

// ─────────────────────────────────────────────────────────────
//  CURL write callback
// ─────────────────────────────────────────────────────────────
static size_t curlWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// ─────────────────────────────────────────────────────────────
//  HearingNode
// ─────────────────────────────────────────────────────────────
class HearingNode : public rclcpp::Node {
public:
    HearingNode() : Node("hearing_node") {
        declare_parameter("sample_rate",       16000);
        declare_parameter("silence_timeout_s", 6.0);
        declare_parameter("max_record_s",      30.0);
        declare_parameter("whisper_model",     "whisper-1");
        declare_parameter("language",          "ru");

        sample_rate_       = get_parameter("sample_rate").as_int();
        silence_timeout_s_ = get_parameter("silence_timeout_s").as_double();
        max_record_s_      = get_parameter("max_record_s").as_double();
        whisper_model_     = get_parameter("whisper_model").as_string();
        language_          = get_parameter("language").as_string();

        openai_key_ = getenv("OPENAI_API_KEY") ? getenv("OPENAI_API_KEY") : "";
        if (openai_key_.empty()) {
            RCLCPP_WARN(get_logger(),
                "OPENAI_API_KEY not set — STT will return placeholder text");
        }

        pub_speech_ = create_publisher<std_msgs::msg::String>(TOPIC_SPEECH_TEXT, 10);
        sub_listen_ = create_subscription<std_msgs::msg::String>(
            TOPIC_LISTEN_CMD, 10,
            [this](std_msgs::msg::String::SharedPtr msg) {
                onListenCommand(msg->data);
            });

        curl_global_init(CURL_GLOBAL_DEFAULT);

#ifdef HAVE_PORTAUDIO
        Pa_Initialize();
#endif

        RCLCPP_INFO(get_logger(), "HearingNode started (sample_rate=%d)", sample_rate_);
    }

    ~HearingNode() {
        listening_ = false;
        if (audio_thread_.joinable()) audio_thread_.join();
        if (stt_thread_.joinable())   stt_thread_.join();
#ifdef HAVE_PORTAUDIO
        Pa_Terminate();
#endif
        curl_global_cleanup();
    }

private:
    // ── Command handler ───────────────────────────────────────
    void onListenCommand(const std::string& cmd) {
        if (cmd == LISTEN_START && !listening_) {
            RCLCPP_INFO(get_logger(), "Starting audio capture...");
            listening_ = true;
            // Detach previous threads safely
            if (audio_thread_.joinable()) audio_thread_.join();
            audio_thread_ = std::thread(&HearingNode::recordAudio, this);
        } else if (cmd == LISTEN_STOP && listening_) {
            RCLCPP_INFO(get_logger(), "Stopping audio capture (external stop)");
            listening_ = false;
        }
    }

    // ── Thread: audio capture ────────────────────────────────
    void recordAudio() {
        std::vector<int16_t> samples;
        samples.reserve(sample_rate_ * static_cast<int>(max_record_s_));

#ifdef HAVE_PORTAUDIO
        PaStream* stream = nullptr;
        PaStreamParameters params{};
        params.device                    = Pa_GetDefaultInputDevice();
        params.channelCount              = 1;
        params.sampleFormat              = paInt16;
        params.suggestedLatency          =
            Pa_GetDeviceInfo(params.device)->defaultLowInputLatency;
        params.hostApiSpecificStreamInfo = nullptr;

        PaError err = Pa_OpenStream(&stream, &params, nullptr,
            sample_rate_, 256, paClipOff, nullptr, nullptr);
        if (err != paNoError) {
            RCLCPP_ERROR(get_logger(), "PortAudio open error: %s", Pa_GetErrorText(err));
            publishFallback("(микрофон недоступен)");
            listening_ = false;
            return;
        }
        Pa_StartStream(stream);

        constexpr int CHUNK = 256;
        int16_t chunk_buf[CHUNK];
        auto    last_sound_time  = std::chrono::steady_clock::now();
        auto    record_start     = std::chrono::steady_clock::now();
        const double SILENCE_RMS = 200.0;   // amplitude threshold

        while (listening_) {
            Pa_ReadStream(stream, chunk_buf, CHUNK);

            // RMS energy
            double rms = 0;
            for (int i = 0; i < CHUNK; ++i) rms += (double)chunk_buf[i] * chunk_buf[i];
            rms = std::sqrt(rms / CHUNK);

            samples.insert(samples.end(), chunk_buf, chunk_buf + CHUNK);

            auto now = std::chrono::steady_clock::now();
            if (rms > SILENCE_RMS) last_sound_time = now;

            double elapsed_silence =
                std::chrono::duration<double>(now - last_sound_time).count();
            double elapsed_total   =
                std::chrono::duration<double>(now - record_start).count();

            if (elapsed_silence > silence_timeout_s_ ||
                elapsed_total   > max_record_s_)
            {
                RCLCPP_INFO(get_logger(),
                    "Stopping recording (silence=%.1fs / total=%.1fs)",
                    elapsed_silence, elapsed_total);
                listening_ = false;
            }
        }

        Pa_StopStream(stream);
        Pa_CloseStream(stream);
#else
        // No PortAudio — simulate 2-second silence then return placeholder
        RCLCPP_WARN(get_logger(), "PortAudio not available — using placeholder STT");
        std::this_thread::sleep_for(2s);
        publishFallback("(PortAudio не установлен, заглушка)");
        listening_ = false;
        return;
#endif

        if (samples.empty()) {
            publishFallback("");
            return;
        }

        // Run STT asynchronously so we don't block the capture pipeline
        if (stt_thread_.joinable()) stt_thread_.join();
        auto wav = buildWav(samples, sample_rate_);
        stt_thread_ = std::thread(&HearingNode::runSTT, this, std::move(wav));
    }

    // ── Thread: Whisper API call ──────────────────────────────
    void runSTT(std::vector<uint8_t> wav_data) {
        if (openai_key_.empty()) {
            std::this_thread::sleep_for(300ms);
            publishFallback("(нет ключа OpenAI, STT недоступен)");
            return;
        }

        // Write WAV to temp file (Whisper API requires multipart upload)
        const std::string tmp_path = "/tmp/robot_teacher_audio.wav";
        {
            std::ofstream f(tmp_path, std::ios::binary);
            f.write(reinterpret_cast<const char*>(wav_data.data()), wav_data.size());
        }

        CURL* curl = curl_easy_init();
        if (!curl) { publishFallback(""); return; }

        // Modern curl_mime API (replaces deprecated curl_formadd since libcurl 7.56)
        curl_mime*     mime = curl_mime_init(curl);
        curl_mimepart* part;

        // Field: file
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_filedata(part, tmp_path.c_str());
        curl_mime_filename(part, "audio.wav");
        curl_mime_type(part, "audio/wav");

        // Field: model
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "model");
        curl_mime_data(part, whisper_model_.c_str(), CURL_ZERO_TERMINATED);

        // Field: language
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "language");
        curl_mime_data(part, language_.c_str(), CURL_ZERO_TERMINATED);

        std::string auth_header = "Authorization: Bearer " + openai_key_;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, auth_header.c_str());

        std::string response_body;
        curl_easy_setopt(curl, CURLOPT_URL,
            "https://api.openai.com/v1/audio/transcriptions");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    headers);
        curl_easy_setopt(curl, CURLOPT_MIMEPOST,      mime);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &response_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,       30L);

        CURLcode res = curl_easy_perform(curl);
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            RCLCPP_ERROR(get_logger(), "STT curl error: %s", curl_easy_strerror(res));
            publishFallback("");
            return;
        }

        // Parse JSON: {"text": "..."}
        std::string text;
        auto pos = response_body.find("\"text\"");
        if (pos != std::string::npos) {
            auto q1 = response_body.find('"', pos + 7);
            auto q2 = response_body.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
                text = response_body.substr(q1 + 1, q2 - q1 - 1);
        }

        if (text.empty()) {
            RCLCPP_WARN(get_logger(), "STT returned empty text. Response: %s",
                response_body.c_str());
        }
        publishFallback(text.empty() ? "" : text);
    }

    void publishFallback(const std::string& text) {
        auto msg = std_msgs::msg::String{};
        msg.data = text;
        pub_speech_->publish(msg);
        RCLCPP_INFO(get_logger(), "STT result: '%s'", text.c_str());
    }

    // ── Members ───────────────────────────────────────────────
    int         sample_rate_;
    double      silence_timeout_s_;
    double      max_record_s_;
    std::string whisper_model_;
    std::string language_;
    std::string openai_key_;

    std::atomic<bool> listening_{false};
    std::thread       audio_thread_;
    std::thread       stt_thread_;

    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr     pub_speech_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr  sub_listen_;
};

// ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<HearingNode>());
    rclcpp::shutdown();
    return 0;
}

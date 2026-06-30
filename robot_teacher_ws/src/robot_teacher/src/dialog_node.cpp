/**
 * dialog_node.cpp
 *
 * ROS2 node: Диалог
 * - Подписывается на gesture, face_present, speech_text
 * - Управляет конечным автоматом сессии (IDLE / GREETING / DIALOG / LISTENING / BYE)
 * - Формирует запросы к Anthropic Claude API (или OpenAI) с контекстом беседы
 * - Публикует ответ в TOPIC_DIALOG_OUT (текст на экране)
 * - Отправляет команды hearing_node (start/stop STT)
 *
 * Асинхронность: LLM-запрос выполняется в отдельном потоке;
 * основной поток не блокируется.
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include <curl/curl.h>

#include "robot_teacher/common.hpp"

using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────
//  Session state machine
// ─────────────────────────────────────────────────────────────
enum class SessionState {
    IDLE,       // no face
    GREETING,   // face appeared, greeting in progress
    DIALOG,     // normal gesture/voice interaction
    LISTENING,  // waiting for voice input (STT active)
    PAUSED,     // open-palm stop
    BYE         // face left, farewell in progress
};

static const char* stateName(SessionState s) {
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

// ─────────────────────────────────────────────────────────────
//  CURL helpers
// ─────────────────────────────────────────────────────────────
static size_t curlWriteCb(char* ptr, size_t size, size_t nmemb, void* ud) {
    auto* s = static_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

// Parse "content" field from Claude/OpenAI JSON response (minimal, no dep)
static std::string parseContent(const std::string& body, bool is_anthropic) {
    // Anthropic: "text":"..."  inside content array
    // OpenAI:    "content":"..."
    std::string key = is_anthropic ? "\"text\":" : "\"content\":";
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

// ─────────────────────────────────────────────────────────────
//  DialogNode
// ─────────────────────────────────────────────────────────────
class DialogNode : public rclcpp::Node {
public:
    explicit DialogNode() : Node("dialog_node"), state_(SessionState::IDLE) {
        declare_parameter("llm_provider",  "anthropic");  // anthropic | openai
        declare_parameter("anthropic_model", "claude-sonnet-4-20250514");
        declare_parameter("openai_model",    "gpt-4o-mini");
        declare_parameter("max_history",     20);

        llm_provider_     = get_parameter("llm_provider").as_string();
        anthropic_model_  = get_parameter("anthropic_model").as_string();
        openai_model_     = get_parameter("openai_model").as_string();
        max_history_      = get_parameter("max_history").as_int();

        anthropic_key_ = getenv("ANTHROPIC_API_KEY") ? getenv("ANTHROPIC_API_KEY") : "";
        openai_key_    = getenv("OPENAI_API_KEY")    ? getenv("OPENAI_API_KEY")    : "";

        if (anthropic_key_.empty() && openai_key_.empty()) {
            RCLCPP_WARN(get_logger(),
                "No LLM API keys found (ANTHROPIC_API_KEY / OPENAI_API_KEY). "
                "Dialog will use canned responses.");
        }

        // Publishers
        pub_dialog_ = create_publisher<std_msgs::msg::String>(TOPIC_DIALOG_OUT, 10);
        pub_listen_ = create_publisher<std_msgs::msg::String>(TOPIC_LISTEN_CMD, 10);

        // Subscriptions
        sub_gesture_ = create_subscription<std_msgs::msg::String>(
            TOPIC_GESTURE, 10,
            [this](std_msgs::msg::String::SharedPtr m){ onGesture(m->data); });

        sub_face_ = create_subscription<std_msgs::msg::String>(
            TOPIC_FACE_PRESENT, 10,
            [this](std_msgs::msg::String::SharedPtr m){ onFace(m->data); });

        sub_speech_ = create_subscription<std_msgs::msg::String>(
            TOPIC_SPEECH_TEXT, 10,
            [this](std_msgs::msg::String::SharedPtr m){ onSpeech(m->data); });

        curl_global_init(CURL_GLOBAL_DEFAULT);
        RCLCPP_INFO(get_logger(), "DialogNode started (provider=%s)", llm_provider_.c_str());
    }

    ~DialogNode() {
        if (llm_thread_.joinable()) llm_thread_.join();
        curl_global_cleanup();
    }

private:
    // ─────────────────────────────────────────────────────────
    //  Event handlers (called from ROS callback threads)
    // ─────────────────────────────────────────────────────────
    void onFace(const std::string& status) {
        RCLCPP_INFO(get_logger(), "Face: %s (state=%s)", status.c_str(), stateName(state_));

        if (status == FACE_DETECTED && state_ == SessionState::IDLE) {
            transitionTo(SessionState::GREETING);
            history_.clear();
            asyncLLM("system_event:face_detected",
                "Пользователь вошёл в кадр. Поприветствуй его тепло и задай один "
                "короткий вводный вопрос (например, чем хочет заниматься сегодня). "
                "Ответ до 2 предложений.");
        }

        if (status == FACE_LOST &&
            state_ != SessionState::IDLE &&
            state_ != SessionState::BYE)
        {
            // Stop STT if listening
            if (state_ == SessionState::LISTENING) sendListenCmd(LISTEN_STOP);
            transitionTo(SessionState::BYE);
            asyncLLM("system_event:face_lost",
                "Пользователь вышел из кадра. Попрощайся коротко и тепло.");
        }
    }

    void onGesture(const std::string& gesture) {
        if (state_ == SessionState::IDLE || state_ == SessionState::BYE) return;

        RCLCPP_INFO(get_logger(), "Gesture: %s (state=%s)", gesture.c_str(), stateName(state_));

        // ── OPEN_PALM: pause/resume ───────────────────────────
        if (gesture == GESTURE_OPEN_PALM) {
            if (state_ == SessionState::PAUSED) {
                transitionTo(SessionState::DIALOG);
                displayLocal("Продолжаем! Что-нибудь ещё хочешь сказать или спросить?");
            } else {
                if (state_ == SessionState::LISTENING) sendListenCmd(LISTEN_STOP);
                transitionTo(SessionState::PAUSED);
                displayLocal("⏸ Пауза. Помаши рукой или подними палец, чтобы продолжить.");
            }
            return;
        }

        // ── WAVE: greeting / farewell ─────────────────────────
        if (gesture == GESTURE_WAVE) {
            asyncLLM("gesture:" + gesture,
                "Пользователь помахал рукой. Ответь коротким дружелюбным приветствием "
                "или прощанием, смотря по контексту разговора.");
            transitionTo(SessionState::DIALOG);
            return;
        }

        // ── RAISED_HAND: voice mode ───────────────────────────
        if (gesture == GESTURE_RAISED_HAND) {
            if (state_ != SessionState::LISTENING) {
                transitionTo(SessionState::LISTENING);
                displayLocal("🎤 Слушаю тебя, говори!");
                sendListenCmd(LISTEN_START);
            }
            return;
        }

        if (state_ == SessionState::PAUSED) return;

        // ── Other gestures → LLM ──────────────────────────────
        std::string prompt;
        if (gesture == GESTURE_THUMB_UP)
            prompt = "Пользователь показал большой палец вверх (согласие/понял). "
                     "Подтверди и предложи следующую тему или вопрос.";
        else if (gesture == GESTURE_THUMB_DOWN)
            prompt = "Пользователь показал большой палец вниз (не нравится/не понял). "
                     "Извинись, предложи другой подход или объяснение.";
        else if (gesture == GESTURE_POINT)
            prompt = "Пользователь указывает на тебя — хочет узнать о тебе. "
                     "Коротко расскажи, что ты робот-педагог, как работаешь.";
        else if (gesture == GESTURE_OK)
            prompt = "Пользователь показал знак OK. Прими это как подтверждение "
                     "и продолжи диалог логично.";
        else if (gesture == GESTURE_CROSSED)
            prompt = "Пользователь скрестил руки (несогласие/протест). "
                     "Мягко спроси, что именно не устраивает, и предложи альтернативу.";
        else
            prompt = "Пользователь показал жест '" + gesture + "'. Отреагируй уместно.";

        asyncLLM("gesture:" + gesture, prompt);
        transitionTo(SessionState::DIALOG);
    }

    void onSpeech(const std::string& text) {
        if (state_ != SessionState::LISTENING) return;

        transitionTo(SessionState::DIALOG);

        if (text.empty()) {
            displayLocal("Не расслышал. Попробуй ещё раз или используй жесты.");
            return;
        }

        RCLCPP_INFO(get_logger(), "Speech recognized: '%s'", text.c_str());
        asyncLLM("voice:" + text,
            "Пользователь произнёс голосом: \"" + text + "\". "
            "Ответь развёрнуто и по существу (2–4 предложения).");
    }

    // ─────────────────────────────────────────────────────────
    //  State transitions
    // ─────────────────────────────────────────────────────────
    void transitionTo(SessionState next) {
        RCLCPP_INFO(get_logger(), "State: %s → %s",
            stateName(state_), stateName(next));
        state_ = next;
        if (next == SessionState::BYE) {
            // After farewell, go IDLE (timer stored as member — avoids immediate destruction)
            bye_timer_ = create_wall_timer(4s, [this]() {
                bye_timer_.reset();  // self-cancel (one-shot)
                if (state_ == SessionState::BYE) {
                    transitionTo(SessionState::IDLE);
                    history_.clear();
                }
            });
        }
    }

    // ─────────────────────────────────────────────────────────
    //  Async LLM call
    // ─────────────────────────────────────────────────────────
    void asyncLLM(const std::string& event, const std::string& instruction) {
        // Guard: don't pile up requests
        if (llm_busy_.exchange(true)) {
            RCLCPP_WARN(get_logger(), "LLM busy, dropping event: %s", event.c_str());
            return;
        }

        // Build history snapshot (deque → vector via range constructor)
        std::vector<std::pair<std::string,std::string>> history_snap;
        {
            std::lock_guard<std::mutex> lk(history_mtx_);
            history_snap.assign(history_.begin(), history_.end());
        }

        if (llm_thread_.joinable()) llm_thread_.join();
        llm_thread_ = std::thread([this, instruction, history_snap]() {
            std::string reply = callLLM(instruction, history_snap);
            if (reply.empty()) reply = "(нет ответа от LLM)";

            // Add to history
            {
                std::lock_guard<std::mutex> lk(history_mtx_);
                history_.push_back({"user", instruction});
                history_.push_back({"assistant", reply});
                // Trim
                while (static_cast<int>(history_.size()) > max_history_ * 2)
                    history_.pop_front();
            }

            displayLocal(reply);
            llm_busy_ = false;
        });
    }

    // ─────────────────────────────────────────────────────────
    //  LLM HTTP call (sync, runs in llm_thread_)
    // ─────────────────────────────────────────────────────────
    std::string callLLM(
        const std::string& instruction,
        const std::vector<std::pair<std::string,std::string>>& history)
    {
        // System prompt for robot-pedagog persona
        const std::string system_prompt =
            "Ты — дружелюбный робот-педагог по имени Ботик. "
            "Говоришь кратко, понятно, с теплотой. "
            "Целевая аудитория — дети и студенты. "
            "Все ответы — на русском языке. "
            "Жесты рук описываются в событиях gesture:*, голосовые вопросы — voice:*.";

        bool use_anthropic =
            (llm_provider_ == "anthropic" && !anthropic_key_.empty()) ||
            (openai_key_.empty() && !anthropic_key_.empty());

        if (anthropic_key_.empty() && openai_key_.empty()) {
            // Fallback canned responses
            return fallbackResponse(instruction);
        }

        if (use_anthropic) {
            return callAnthropic(system_prompt, instruction, history);
        } else {
            return callOpenAI(system_prompt, instruction, history);
        }
    }

    std::string callAnthropic(
        const std::string& system,
        const std::string& instruction,
        const std::vector<std::pair<std::string,std::string>>& history)
    {
        // Build messages JSON
        std::ostringstream msgs;
        msgs << "[";
        bool first = true;
        for (const auto& [role, content] : history) {
            if (!first) msgs << ",";
            msgs << "{\"role\":\"" << role << "\","
                 << "\"content\":\"" << jsonEscape(content) << "\"}";
            first = false;
        }
        // Add current instruction as user
        if (!first) msgs << ",";
        msgs << "{\"role\":\"user\","
             << "\"content\":\"" << jsonEscape(instruction) << "\"}";
        msgs << "]";

        std::ostringstream body;
        body << "{"
             << "\"model\":\"" << anthropic_model_ << "\","
             << "\"max_tokens\":512,"
             << "\"system\":\"" << jsonEscape(system) << "\","
             << "\"messages\":" << msgs.str()
             << "}";

        std::string body_str = body.str();
        std::string response;

        CURL* curl = curl_easy_init();
        if (!curl) return "";

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("x-api-key: " + anthropic_key_).c_str());
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        headers = curl_slist_append(headers, "content-type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER,   headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS,   body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_str.size());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,    &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,      30L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            RCLCPP_ERROR(get_logger(), "Anthropic curl error: %s", curl_easy_strerror(res));
            return "";
        }

        return parseContent(response, true);
    }

    std::string callOpenAI(
        const std::string& system,
        const std::string& instruction,
        const std::vector<std::pair<std::string,std::string>>& history)
    {
        std::ostringstream msgs;
        msgs << "[";
        msgs << "{\"role\":\"system\",\"content\":\"" << jsonEscape(system) << "\"}";
        for (const auto& [role, content] : history) {
            msgs << ",{\"role\":\"" << role << "\","
                 << "\"content\":\"" << jsonEscape(content) << "\"}";
        }
        msgs << ",{\"role\":\"user\","
             << "\"content\":\"" << jsonEscape(instruction) << "\"}";
        msgs << "]";

        std::ostringstream body;
        body << "{"
             << "\"model\":\"" << openai_model_ << "\","
             << "\"max_tokens\":512,"
             << "\"messages\":" << msgs.str()
             << "}";

        std::string body_str = body.str();
        std::string response;

        CURL* curl = curl_easy_init();
        if (!curl) return "";

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + openai_key_).c_str());
        headers = curl_slist_append(headers, "content-type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER,   headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS,   body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_str.size());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,    &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,      30L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            RCLCPP_ERROR(get_logger(), "OpenAI curl error: %s", curl_easy_strerror(res));
            return "";
        }

        return parseContent(response, false);
    }

    // Canned responses when no API keys
    std::string fallbackResponse(const std::string& instruction) {
        if (instruction.find("face_detected") != std::string::npos)
            return "Привет! Я Ботик, твой робот-педагог. Чем займёмся сегодня?";
        if (instruction.find("face_lost") != std::string::npos)
            return "До свидания! Было приятно пообщаться. 👋";
        if (instruction.find("thumb_up") != std::string::npos)
            return "Отлично, рад что понятно! Двигаемся дальше.";
        if (instruction.find("thumb_down") != std::string::npos)
            return "Понял, попробуем иначе. Что именно непонятно?";
        if (instruction.find("point") != std::string::npos)
            return "Я — Ботик! Распознаю жесты и голос, отвечаю на вопросы. "
                   "Управляй мной жестами или голосом.";
        if (instruction.find("ok") != std::string::npos)
            return "Отлично! Продолжаем.";
        if (instruction.find("crossed") != std::string::npos)
            return "Понял, что-то не так. Расскажи подробнее — подниму руку и спрошу голосом.";
        if (instruction.find("wave") != std::string::npos)
            return "Привет-привет! Рад тебя видеть!";
        if (instruction.find("voice:") != std::string::npos) {
            auto q = instruction.find('"');
            if (q != std::string::npos) {
                auto q2 = instruction.find('"', q + 1);
                std::string phrase = instruction.substr(q + 1, q2 - q - 1);
                return "Ты сказал: «" + phrase + "». Интересный вопрос! "
                       "(Нет ключа LLM — ответ заглушка.)";
            }
        }
        return "Получил сигнал, обрабатываю... (режим заглушки, ключа LLM нет)";
    }

    // ─────────────────────────────────────────────────────────
    //  Utilities
    // ─────────────────────────────────────────────────────────
    void displayLocal(const std::string& text) {
        auto msg = std_msgs::msg::String{};
        msg.data = text;
        pub_dialog_->publish(msg);
        RCLCPP_INFO(get_logger(), "[Ботик] %s", text.c_str());
    }

    void sendListenCmd(const std::string& cmd) {
        auto msg = std_msgs::msg::String{};
        msg.data = cmd;
        pub_listen_->publish(msg);
    }

    // ── Members ───────────────────────────────────────────────
    SessionState state_;
    rclcpp::TimerBase::SharedPtr bye_timer_;

    std::string llm_provider_;
    std::string anthropic_model_;
    std::string openai_model_;
    int         max_history_;

    std::string anthropic_key_;
    std::string openai_key_;

    std::mutex history_mtx_;
    std::deque<std::pair<std::string,std::string>> history_;

    std::atomic<bool> llm_busy_{false};
    std::thread       llm_thread_;

    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr    pub_dialog_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr    pub_listen_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_gesture_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_face_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_speech_;
};

// ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<DialogNode>());
    rclcpp::shutdown();
    return 0;
}

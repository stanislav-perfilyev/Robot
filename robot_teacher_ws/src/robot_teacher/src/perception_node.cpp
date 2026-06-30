/**
 * perception_node.cpp
 *
 * ROS2 node: Восприятие
 * - Захватывает кадры с веб-камеры (отдельный поток)
 * - Детектирует лицо (Haar cascade) → публикует face_present
 * - Классифицирует жесты руки (MediaPipe-lite через OpenCV DNN / fallback skin-blob)
 *   → публикует gesture
 *
 * Потоки:
 *   capture_thread_  — захват кадров (не блокирует ROS)
 *   process_thread_  — детекция/классификация (CPU-intensive, отдельно)
 *   Главный поток    — rclcpp::spin (таймеры, публикации)
 */

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/bool.hpp"

#include <opencv2/opencv.hpp>

#include "robot_teacher/common.hpp"

using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────
//  HandLandmarks helper (very lightweight rule-based classifier)
//  Works with raw OpenCV contour analysis — no external model needed.
// ─────────────────────────────────────────────────────────────
struct FingerState {
    bool thumb = false;
    bool index = false;
    bool middle = false;
    bool ring = false;
    bool pinky = false;
    int  extended_count = 0;
};

class GestureClassifier {
public:
    /**
     * Analyse a skin-segmented binary mask (hand region) and return
     * a gesture label string.  Returns empty string if inconclusive.
     */
    std::string classify(const cv::Mat& skin_mask, const cv::Mat& frame) {
        // Find contours
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(skin_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        if (contours.empty()) return "";

        // Pick largest contour
        size_t largest = 0;
        double max_area = 0;
        for (size_t i = 0; i < contours.size(); ++i) {
            double a = cv::contourArea(contours[i]);
            if (a > max_area) { max_area = a; largest = i; }
        }

        if (max_area < 3000) return "";  // too small

        const auto& cnt = contours[largest];

        // Convex hull + defects
        std::vector<int> hull_idx;
        cv::convexHull(cnt, hull_idx, false, false);

        std::vector<cv::Vec4i> defects;
        if (hull_idx.size() > 3) {
            cv::convexityDefects(cnt, hull_idx, defects);
        }

        // Count significant defects (= gaps between fingers)
        int finger_gaps = 0;
        for (const auto& d : defects) {
            float depth = d[3] / 256.0f;
            if (depth > 20.0f) finger_gaps++;
        }

        // Bounding box aspect ratio
        cv::Rect bb = cv::boundingRect(cnt);
        double aspect = static_cast<double>(bb.width) / bb.height;

        // Hull convex poly for palm direction
        std::vector<cv::Point> hull_pts;
        cv::convexHull(cnt, hull_pts);
        double hull_area = cv::contourArea(hull_pts);
        double solidity = (hull_area > 0) ? (max_area / hull_area) : 0;

        // Motion history (wave detection)
        cv::Moments m = cv::moments(cnt);
        cv::Point2f centroid(0, 0);
        if (m.m00 > 0) {
            centroid = cv::Point2f(m.m10 / m.m00, m.m01 / m.m00);
        }
        centroid_history_.push_back(centroid);
        if (centroid_history_.size() > 15) centroid_history_.pop_front();

        // ── Rule-based classification ──────────────────────────
        // WAVE: centroid moves horizontally > threshold over history
        if (centroid_history_.size() >= 10) {
            float dx = centroid_history_.back().x - centroid_history_.front().x;
            if (std::abs(dx) > 60.0f) {
                centroid_history_.clear();
                return GESTURE_WAVE;
            }
        }

        // OPEN_PALM: high solidity, many fingers (4+ gaps), wide
        if (solidity > 0.75 && finger_gaps >= 3 && aspect > 0.6) {
            return GESTURE_OPEN_PALM;
        }

        // RAISED_HAND: tall shape, moderate solidity (arm visible)
        if (aspect < 0.6 && bb.height > bb.width * 1.4 && solidity > 0.55) {
            return GESTURE_RAISED_HAND;
        }

        // THUMB_UP: 0-1 defects, tall, narrow thumb region
        if (finger_gaps <= 1 && aspect < 0.7 && solidity > 0.80) {
            // Thumb direction: centroid above mid-y of bounding box → up
            if (centroid.y < bb.y + bb.height * 0.45) {
                return GESTURE_THUMB_UP;
            } else {
                return GESTURE_THUMB_DOWN;
            }
        }

        // POINT: 1 defect, elongated upward  
        if (finger_gaps == 1 && aspect < 0.55) {
            return GESTURE_POINT;
        }

        // OK: small circular shape + 1 finger extended
        if (finger_gaps == 1 && solidity > 0.72 && aspect > 0.7 && aspect < 1.3) {
            return GESTURE_OK;
        }

        // CROSSED_ARMS: very wide bounding box across full frame
        if (bb.width > frame.cols * 0.5 && aspect > 2.5) {
            return GESTURE_CROSSED;
        }

        return "";
    }

private:
    std::deque<cv::Point2f> centroid_history_;
};

// ─────────────────────────────────────────────────────────────
//  PerceptionNode
// ─────────────────────────────────────────────────────────────
class PerceptionNode : public rclcpp::Node {
public:
    PerceptionNode() : Node("perception_node") {
        // Declare params
        declare_parameter("camera_id", 0);
        declare_parameter("cascade_path", "");
        declare_parameter("show_debug_window", false);

        camera_id_     = get_parameter("camera_id").as_int();
        show_debug_   = get_parameter("show_debug_window").as_bool();

        // Publishers
        pub_gesture_      = create_publisher<std_msgs::msg::String>(TOPIC_GESTURE, 10);
        pub_face_present_ = create_publisher<std_msgs::msg::String>(TOPIC_FACE_PRESENT, 10);

        // Load face cascade
        std::string cascade_path = get_parameter("cascade_path").as_string();
        if (cascade_path.empty()) {
            // OpenCV default location
            cascade_path = cv::samples::findFileOrKeep(
                "haarcascades/haarcascade_frontalface_default.xml");
        }
        if (!face_cascade_.load(cascade_path)) {
            RCLCPP_WARN(get_logger(),
                "Could not load face cascade from '%s'. Trying OpenCV default.",
                cascade_path.c_str());
            face_cascade_.load(
                "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml");
        }

        running_ = true;
        capture_thread_  = std::thread(&PerceptionNode::captureLoop,  this);
        process_thread_  = std::thread(&PerceptionNode::processLoop,  this);

        RCLCPP_INFO(get_logger(), "PerceptionNode started (camera %d)", camera_id_);
    }

    ~PerceptionNode() {
        running_ = false;
        frame_cv_.notify_all();
        if (capture_thread_.joinable())  capture_thread_.join();
        if (process_thread_.joinable())  process_thread_.join();
        RCLCPP_INFO(get_logger(), "PerceptionNode stopped");
    }

private:
    // ── Thread: capture ──────────────────────────────────────
    void captureLoop() {
        cv::VideoCapture cap(camera_id_);
        if (!cap.isOpened()) {
            RCLCPP_ERROR(get_logger(), "Cannot open camera %d", camera_id_);
            return;
        }
        cap.set(cv::CAP_PROP_FRAME_WIDTH,  640);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        cap.set(cv::CAP_PROP_FPS, 30);

        while (running_) {
            cv::Mat frame;
            if (!cap.read(frame) || frame.empty()) {
                std::this_thread::sleep_for(10ms);
                continue;
            }
            {
                std::lock_guard<std::mutex> lk(frame_mtx_);
                latest_frame_ = frame.clone();
                new_frame_    = true;
            }
            frame_cv_.notify_one();
        }
    }

    // ── Thread: process ──────────────────────────────────────
    void processLoop() {
        constexpr int DEBOUNCE_FRAMES = 5;   // gesture must persist N frames
        std::string   prev_gesture;
        int           gesture_count = 0;
        bool          face_was_present = false;

        while (running_) {
            cv::Mat frame;
            {
                std::unique_lock<std::mutex> lk(frame_mtx_);
                frame_cv_.wait_for(lk, 100ms, [this]{ return new_frame_.load(); });
                if (!new_frame_) continue;
                frame     = latest_frame_.clone();
                new_frame_ = false;
            }

            // ── Face detection ─────────────────────────────
            cv::Mat gray;
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            cv::equalizeHist(gray, gray);

            std::vector<cv::Rect> faces;
            if (!face_cascade_.empty()) {
                face_cascade_.detectMultiScale(
                    gray, faces,
                    1.1, 4, 0,
                    cv::Size(80, 80));
            }

            bool face_now = !faces.empty();
            if (face_now != face_was_present) {
                face_was_present = face_now;
                auto msg = std_msgs::msg::String{};
                msg.data = face_now ? FACE_DETECTED : FACE_LOST;
                pub_face_present_->publish(msg);
                RCLCPP_INFO(get_logger(), "Face %s", msg.data.c_str());
            }

            // ── Skin segmentation ─────────────────────────
            // Use YCrCb skin detection (robust under varied lighting)
            cv::Mat ycrcb;
            cv::cvtColor(frame, ycrcb, cv::COLOR_BGR2YCrCb);
            cv::Mat skin_mask;
            cv::inRange(ycrcb,
                cv::Scalar(0, 133, 77),
                cv::Scalar(255, 173, 127),
                skin_mask);

            // Exclude face regions from hand detection
            for (const auto& f : faces) {
                cv::Rect expanded(f.x - 10, f.y - 10, f.width + 20, f.height + 20);
                expanded &= cv::Rect(0, 0, frame.cols, frame.rows);
                skin_mask(expanded) = 0;
            }

            // Morphological cleanup
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, {7, 7});
            cv::morphologyEx(skin_mask, skin_mask, cv::MORPH_OPEN,  kernel);
            cv::morphologyEx(skin_mask, skin_mask, cv::MORPH_CLOSE, kernel);

            // ── Gesture classification ─────────────────────
            std::string gesture = classifier_.classify(skin_mask, frame);

            if (gesture == prev_gesture && !gesture.empty()) {
                gesture_count++;
            } else {
                prev_gesture  = gesture;
                gesture_count = 1;
            }

            if (gesture_count == DEBOUNCE_FRAMES && !gesture.empty()) {
                auto msg = std_msgs::msg::String{};
                msg.data = gesture;
                pub_gesture_->publish(msg);
                RCLCPP_INFO(get_logger(), "Gesture: %s", gesture.c_str());
                gesture_count = 0;  // reset to avoid repeat floods
            }

            // ── Optional debug window ─────────────────────
            if (show_debug_) {
                for (const auto& f : faces) {
                    cv::rectangle(frame, f, {0, 255, 0}, 2);
                }
                std::string label = gesture.empty() ? "---" : gesture;
                cv::putText(frame, label, {10, 30},
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, {0, 200, 255}, 2);
                cv::imshow("robot_teacher | perception", frame);
                cv::waitKey(1);
            }
        }
    }

    // ── Members ───────────────────────────────────────────────
    int  camera_id_;
    bool show_debug_;

    cv::CascadeClassifier face_cascade_;
    GestureClassifier     classifier_;

    std::mutex              frame_mtx_;
    std::condition_variable frame_cv_;
    cv::Mat                 latest_frame_;
    std::atomic<bool>       new_frame_{false};
    std::atomic<bool>       running_{false};

    std::thread capture_thread_;
    std::thread process_thread_;

    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_gesture_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_face_present_;
};

// ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<PerceptionNode>());
    rclcpp::shutdown();
    return 0;
}

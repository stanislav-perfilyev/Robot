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
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/string.hpp"

#include "robot_teacher/common.hpp"
#include "robot_teacher/gesture_classifier.hpp"

using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────
//  PerceptionNode
// ─────────────────────────────────────────────────────────────
class PerceptionNode : public rclcpp::Node {
public:
  PerceptionNode()
  : Node("perception_node")
  {
        // Declare params
    declare_parameter("camera_id", 0);
    declare_parameter("cascade_path", "");
    declare_parameter("show_debug_window", false);

    camera_id_ = get_parameter("camera_id").as_int();
    show_debug_ = get_parameter("show_debug_window").as_bool();

        // Publishers
    pub_gesture_ = create_publisher<std_msgs::msg::String>(TOPIC_GESTURE, 10);
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
    capture_thread_ = std::thread(&PerceptionNode::captureLoop, this);
    process_thread_ = std::thread(&PerceptionNode::processLoop, this);

    RCLCPP_INFO(get_logger(), "PerceptionNode started (camera %d)", camera_id_);
  }

  ~PerceptionNode()
  {
    running_ = false;
    frame_cv_.notify_all();
    if (capture_thread_.joinable()) {capture_thread_.join();}
    if (process_thread_.joinable()) {process_thread_.join();}
    RCLCPP_INFO(get_logger(), "PerceptionNode stopped");
  }

private:
    // ── Thread: capture ──────────────────────────────────────
  void captureLoop()
  {
    cv::VideoCapture cap(camera_id_);
    if (!cap.isOpened()) {
      RCLCPP_ERROR(get_logger(), "Cannot open camera %d", camera_id_);
      return;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
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
        new_frame_ = true;
      }
      frame_cv_.notify_one();
    }
  }

    // ── Thread: process ──────────────────────────────────────
  void processLoop()
  {
    constexpr int DEBOUNCE_FRAMES = 5;       // gesture must persist N frames
    std::string   prev_gesture;
    int           gesture_count = 0;
    bool          face_was_present = false;

    while (running_) {
      cv::Mat frame;
      {
        std::unique_lock<std::mutex> lk(frame_mtx_);
        frame_cv_.wait_for(lk, 100ms, [this]{return new_frame_.load();});
        if (!new_frame_) {continue;}
        frame = latest_frame_.clone();
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
      for (const auto & f : faces) {
        cv::Rect expanded(f.x - 10, f.y - 10, f.width + 20, f.height + 20);
        expanded &= cv::Rect(0, 0, frame.cols, frame.rows);
        skin_mask(expanded) = 0;
      }

            // Morphological cleanup
      cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, {7, 7});
      cv::morphologyEx(skin_mask, skin_mask, cv::MORPH_OPEN, kernel);
      cv::morphologyEx(skin_mask, skin_mask, cv::MORPH_CLOSE, kernel);

            // ── Gesture classification ─────────────────────
      std::string gesture = classifier_.classify(skin_mask, frame);

      if (gesture == prev_gesture && !gesture.empty()) {
        gesture_count++;
      } else {
        prev_gesture = gesture;
        gesture_count = 1;
      }

      if (gesture_count == DEBOUNCE_FRAMES && !gesture.empty()) {
        auto msg = std_msgs::msg::String{};
        msg.data = gesture;
        pub_gesture_->publish(msg);
        RCLCPP_INFO(get_logger(), "Gesture: %s", gesture.c_str());
        gesture_count = 0;          // reset to avoid repeat floods
      }

            // ── Optional debug window ─────────────────────
      if (show_debug_) {
        for (const auto & f : faces) {
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

  cv::CascadeClassifier            face_cascade_;
  robot_teacher::GestureClassifier classifier_;

  std::mutex              frame_mtx_;
  std::condition_variable frame_cv_;
  cv::Mat                 latest_frame_;
  std::atomic<bool> new_frame_{false};
  std::atomic<bool> running_{false};

  std::thread capture_thread_;
  std::thread process_thread_;

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_gesture_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_face_present_;
};

// ─────────────────────────────────────────────────────────────
int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PerceptionNode>());
  rclcpp::shutdown();
  return 0;
}

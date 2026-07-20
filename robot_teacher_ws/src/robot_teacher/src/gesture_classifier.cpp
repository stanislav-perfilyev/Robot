#include "robot_teacher/gesture_classifier.hpp"
#include "robot_teacher/common.hpp"

#include <cmath>

namespace robot_teacher {

std::string GestureClassifier::classify(const cv::Mat& skin_mask, const cv::Mat& frame)
{
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
        centroid = cv::Point2f(static_cast<float>(m.m10 / m.m00), static_cast<float>(m.m01 / m.m00));
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

}  // namespace robot_teacher

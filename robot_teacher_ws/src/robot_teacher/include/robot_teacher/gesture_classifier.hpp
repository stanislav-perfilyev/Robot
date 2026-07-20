#pragma once

#include <deque>
#include <string>

#include <opencv2/opencv.hpp>

namespace robot_teacher {

/**
 * Rule-based hand-gesture classifier operating on a skin-segmented binary
 * mask (contour shape, convexity defects, aspect ratio, solidity) plus a
 * short centroid history for motion-based gestures (wave).
 *
 * Stateful: retains centroid history between calls, so one instance
 * should be used per continuous perception stream (not shared/reused
 * across independent frame sources).
 */
class GestureClassifier {
public:
    /**
     * Analyse a skin-segmented binary mask (hand region) and return a
     * gesture label string (see robot_teacher/common.hpp GESTURE_* constants).
     * Returns an empty string if no gesture could be confidently classified.
     */
    [[nodiscard]] std::string classify(const cv::Mat& skin_mask, const cv::Mat& frame);

private:
    std::deque<cv::Point2f> centroid_history_;
};

}  // namespace robot_teacher

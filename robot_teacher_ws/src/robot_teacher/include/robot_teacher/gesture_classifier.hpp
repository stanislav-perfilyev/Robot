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

#include <deque>
#include <string>

#include <opencv2/opencv.hpp>

namespace robot_teacher
{

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
  [[nodiscard]] std::string classify(const cv::Mat & skin_mask, const cv::Mat & frame);

private:
  std::deque<cv::Point2f> centroid_history_;
};

}  // namespace robot_teacher

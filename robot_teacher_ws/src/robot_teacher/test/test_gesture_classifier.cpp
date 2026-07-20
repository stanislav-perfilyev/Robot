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

#include <gtest/gtest.h>

#include <opencv2/opencv.hpp>

#include "robot_teacher/common.hpp"
#include "robot_teacher/gesture_classifier.hpp"

using robot_teacher::GestureClassifier;

namespace
{

/// Builds a single-channel binary mask with a filled rectangle "blob" —
/// deliberately convex (no finger-like defects), so it exercises the
/// aspect-ratio / solidity branches of classify() deterministically
/// without needing to synthesize realistic finger contours.
cv::Mat makeRectMask(int frame_cols, int frame_rows, cv::Rect blob)
{
  cv::Mat mask = cv::Mat::zeros(frame_rows, frame_cols, CV_8UC1);
  cv::rectangle(mask, blob, cv::Scalar(255), cv::FILLED);
  return mask;
}

cv::Mat makeCircleMask(int frame_cols, int frame_rows, cv::Point center, int radius)
{
  cv::Mat mask = cv::Mat::zeros(frame_rows, frame_cols, CV_8UC1);
  cv::circle(mask, center, radius, cv::Scalar(255), cv::FILLED);
  return mask;
}

}  // namespace

TEST(GestureClassifier, EmptyMaskYieldsNoGesture) {
    GestureClassifier gc;
    cv::Mat mask = cv::Mat::zeros(240, 320, CV_8UC1);
    cv::Mat frame = cv::Mat::zeros(240, 320, CV_8UC3);
    EXPECT_EQ(gc.classify(mask, frame), "");
}

TEST(GestureClassifier, BlobBelowAreaThresholdYieldsNoGesture) {
    GestureClassifier gc;
    cv::Mat frame = cv::Mat::zeros(240, 320, CV_8UC3);
    // 20x20 = 400 px, well below the 3000px minimum-area cutoff.
    cv::Mat mask = makeRectMask(320, 240, cv::Rect(50, 50, 20, 20));
    EXPECT_EQ(gc.classify(mask, frame), "");
}

TEST(GestureClassifier, TallNarrowSolidBlobIsRaisedHand) {
    GestureClassifier gc;
    cv::Mat frame = cv::Mat::zeros(400, 320, CV_8UC3);
    // width=50, height=200 → aspect=0.25<0.6, height>width*1.4, solid rectangle.
    cv::Mat mask = makeRectMask(320, 400, cv::Rect(100, 50, 50, 200));
    EXPECT_EQ(gc.classify(mask, frame), GESTURE_RAISED_HAND);
}

TEST(GestureClassifier, WideFlatBlobSpanningFrameIsCrossedArms) {
    GestureClassifier gc;
    // Frame wide enough that a 300px-wide blob exceeds 50% of frame.cols (400).
    cv::Mat frame = cv::Mat::zeros(300, 400, CV_8UC3);
    // width=300, height=80 → aspect=3.75>2.5, bb.width(300) > frame.cols*0.5(200).
    cv::Mat mask = makeRectMask(400, 300, cv::Rect(20, 100, 300, 80));
    EXPECT_EQ(gc.classify(mask, frame), GESTURE_CROSSED);
}

TEST(GestureClassifier, StationaryBlobDoesNotTriggerWave) {
    GestureClassifier gc;
    cv::Mat frame = cv::Mat::zeros(300, 400, CV_8UC3);
    cv::Mat mask = makeCircleMask(400, 300, cv::Point(200, 150), 35);

    std::string last;
    for (int i = 0; i < 12; ++i) {
    last = gc.classify(mask, frame);
    }
    EXPECT_NE(last, GESTURE_WAVE);
}

TEST(GestureClassifier, HorizontalMotionOverHistoryTriggersWave) {
    GestureClassifier gc;
    cv::Mat frame = cv::Mat::zeros(300, 900, CV_8UC3);

    std::string result;
    for (int i = 0; i < 10; ++i) {
        // Move the blob 80px to the right each call — total displacement
        // over 10 samples (720px) comfortably exceeds the 60px wave threshold.
    cv::Mat mask = makeCircleMask(900, 300, cv::Point(60 + i * 80, 150), 35);
    result = gc.classify(mask, frame);
    }
    EXPECT_EQ(result, GESTURE_WAVE);
}

// main() is supplied by GTest::gtest_main, linked automatically by
// ament_add_gtest() — do not define one here (would be a duplicate symbol).

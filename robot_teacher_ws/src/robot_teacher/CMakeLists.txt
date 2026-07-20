cmake_minimum_required(VERSION 3.8)
project(robot_teacher)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(OpenCV REQUIRED)

# Find CURL for HTTP requests (LLM API, STT API)
find_package(CURL REQUIRED)

# Find nlohmann_json for JSON parsing
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  # Try pkg-config fallback
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(NLOHMANN_JSON nlohmann_json)
endif()

# Find PortAudio for microphone input
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO portaudio-2.0)

# ========== Utility library (unit-testable, framework-independent logic) ==========
# WAV packaging, JSON escaping/parsing, session-state naming and gesture
# classification are pure/near-pure logic extracted out of the node .cpp
# files so they can be exercised by gtest without spinning up rclcpp nodes.
add_library(robot_teacher_utils STATIC
  src/wav_utils.cpp
  src/curl_utils.cpp
  src/dialog_utils.cpp
  src/gesture_classifier.cpp
)
target_include_directories(robot_teacher_utils PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(robot_teacher_utils PUBLIC
  ${OpenCV_LIBS}
)

# ========== Perception Node ==========
add_executable(perception_node
  src/perception_node.cpp
)
target_include_directories(perception_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(perception_node
  robot_teacher_utils
  ${OpenCV_LIBS}
)
ament_target_dependencies(perception_node
  rclcpp
  std_msgs
  sensor_msgs
  cv_bridge
  image_transport
)

# ========== Hearing Node ==========
add_executable(hearing_node
  src/hearing_node.cpp
)
target_include_directories(hearing_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(hearing_node
  robot_teacher_utils
  CURL::libcurl
)
if(PORTAUDIO_FOUND)
  target_include_directories(hearing_node PUBLIC ${PORTAUDIO_INCLUDE_DIRS})
  target_link_libraries(hearing_node ${PORTAUDIO_LIBRARIES})
  target_compile_definitions(hearing_node PRIVATE HAVE_PORTAUDIO)
endif()
ament_target_dependencies(hearing_node
  rclcpp
  std_msgs
)

# ========== Dialog Node ==========
add_executable(dialog_node
  src/dialog_node.cpp
)
target_include_directories(dialog_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dialog_node
  robot_teacher_utils
  CURL::libcurl
)
ament_target_dependencies(dialog_node
  rclcpp
  std_msgs
)

install(TARGETS
  perception_node
  hearing_node
  dialog_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  launch
  DESTINATION share/${PROJECT_NAME}
)

# ========== Tests ==========
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_wav_utils test/test_wav_utils.cpp)
  target_link_libraries(test_wav_utils robot_teacher_utils)
  target_include_directories(test_wav_utils PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )

  ament_add_gtest(test_dialog_utils test/test_dialog_utils.cpp)
  target_link_libraries(test_dialog_utils robot_teacher_utils)
  target_include_directories(test_dialog_utils PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )

  ament_add_gtest(test_gesture_classifier test/test_gesture_classifier.cpp)
  target_link_libraries(test_gesture_classifier robot_teacher_utils ${OpenCV_LIBS})
  target_include_directories(test_gesture_classifier PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    ${OpenCV_INCLUDE_DIRS}
  )
endif()

ament_package()

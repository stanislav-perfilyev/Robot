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

# ========== Perception Node ==========
add_executable(perception_node
  src/perception_node.cpp
)
target_include_directories(perception_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(perception_node
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
  config
  DESTINATION share/${PROJECT_NAME}
)

ament_package()

cmake_minimum_required(VERSION 3.16)
project(asr_node LANGUAGES CXX)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(high_level_reasoning_interface REQUIRED)

set(ASR_NODE_LOG_DIR "${CMAKE_CURRENT_BINARY_DIR}/logs")
set(ASR_NODE_LOG_FILE "${ASR_NODE_LOG_DIR}/asr_logs.txt")
file(MAKE_DIRECTORY "${ASR_NODE_LOG_DIR}")

add_subdirectory(include/asr_node)

add_executable(asr_node main.cpp)
target_link_libraries(asr_node asr_node_headers)
target_compile_definitions(asr_node PRIVATE
  ASR_LOG_FILE_PATH=\"${ASR_NODE_LOG_FILE}\"
)
ament_target_dependencies(asr_node
  rclcpp
  rclcpp_action
  high_level_reasoning_interface
)
set_target_properties(asr_node PROPERTIES
  BUILD_RPATH "${ASR_NODE_RUNTIME_RPATH}"
  INSTALL_RPATH "${ASR_NODE_RUNTIME_RPATH}"
)

install(TARGETS asr_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_export_include_directories(include)
ament_export_dependencies(rclcpp rclcpp_action high_level_reasoning_interface)
ament_package()

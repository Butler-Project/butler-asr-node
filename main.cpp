#include <memory>
#include <rclcpp/rclcpp.hpp>

#include "asr_node/asr_node.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<asr_node::AsrNode>());
  rclcpp::shutdown();
  return 0;
}

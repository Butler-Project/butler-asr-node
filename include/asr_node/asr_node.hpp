#ifndef ASR_NODE_ASR_NODE_HPP
#define ASR_NODE_ASR_NODE_HPP

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "builder/manager_builder.h"
#include "high_level_reasoning_interface/action/agent_chat_interaction.hpp"
#include "manager/manager.h"

namespace asr_node
{

class AsrNode : public rclcpp::Node
{
public:
  using AgentChatInteraction =
    high_level_reasoning_interface::action::AgentChatInteraction;
  using AgentChatGoalHandle = rclcpp_action::ClientGoalHandle<AgentChatInteraction>;

  explicit AsrNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : rclcpp::Node("asr_node", options),
    manager_{
      asr::builder::ManagerBuilder<>{}
        .with_vad_poll_interval(std::chrono::milliseconds{100})
        .with_final_transcription_callback([this](std::string_view text) {
          on_final_transcription(text);
        })
        .build_config()
    }
  {
    configure_action_parameters();
    initialize_action_client();
    manager_.start();
    initialize_timers();
    RCLCPP_INFO(get_logger(), "ASR node started");
    RCLCPP_INFO(
      get_logger(),
      "Forwarding ASR text to action: %s",
      agent_chat_action_name_.c_str());
  }

  ~AsrNode() override
  {
    vad_timer_.reset();
    chat_retry_timer_.reset();
    manager_.stop();
    manager_.log_summary();
  }

private:
  // -------- Node lifecycle --------
  void configure_action_parameters()
  {
    agent_chat_action_name_ =
      declare_parameter<std::string>("agent_chat_action_name", "agent_chat");
    chat_model_ = declare_parameter<std::string>("chat_model", "");
  }

  void initialize_action_client()
  {
    chat_client_ = rclcpp_action::create_client<AgentChatInteraction>(
      this,
      agent_chat_action_name_);
  }

  void initialize_timers()
  {
    vad_timer_ = create_wall_timer(
      std::chrono::milliseconds{100},
      [this]() {
        process_audio_once();
      });

    chat_retry_timer_ = create_wall_timer(
      std::chrono::seconds{1},
      [this]() {
        send_next_chat_goal();
      });
  }

  // -------- ASR ingestion --------
  void process_audio_once()
  {
    (void)manager_.process_available();
  }

  void on_final_transcription(std::string_view text)
  {
    const auto prompt = std::string{text};
    if (prompt.empty()) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock{chat_mutex_};
      pending_prompts_.push_back(prompt);
    }

    RCLCPP_INFO(get_logger(), "ASR text queued for agent chat action: \"%s\"", prompt.c_str());
    send_next_chat_goal();
  }

  // -------- Action dispatch --------
  void send_next_chat_goal()
  {
    if (!chat_client_) {
      return;
    }

    if (!can_dispatch_goal()) {
      return;
    }

    if (!chat_client_->action_server_is_ready()) {
      RCLCPP_WARN_THROTTLE(
        get_logger(),
        *get_clock(),
        5000,
        "Waiting for action server: %s",
        agent_chat_action_name_.c_str());
      return;
    }

    auto prompt = take_next_prompt();
    if (prompt.empty()) return;

    auto goal = AgentChatInteraction::Goal{};
    goal.prompt = prompt;
    goal.model = chat_model_;

    (void)chat_client_->async_send_goal(goal, make_send_goal_options());
    RCLCPP_INFO(get_logger(), "Sent ASR text to agent chat action: \"%s\"", prompt.c_str());
  }

  bool can_dispatch_goal()
  {
    std::lock_guard<std::mutex> lock{chat_mutex_};
    return !chat_goal_in_flight_ && !pending_prompts_.empty();
  }

  std::string take_next_prompt()
  {
    std::lock_guard<std::mutex> lock{chat_mutex_};
    if (chat_goal_in_flight_ || pending_prompts_.empty()) {
      return {};
    }

    auto prompt = std::move(pending_prompts_.front());
    pending_prompts_.pop_front();
    chat_goal_in_flight_ = true;
    return prompt;
  }

  rclcpp_action::Client<AgentChatInteraction>::SendGoalOptions make_send_goal_options()
  {
    auto send_goal_options =
      rclcpp_action::Client<AgentChatInteraction>::SendGoalOptions{};

    send_goal_options.goal_response_callback =
      [this](const AgentChatGoalHandle::SharedPtr & goal_handle) {
        on_goal_response(goal_handle);
      };

    send_goal_options.feedback_callback =
      [this](
        AgentChatGoalHandle::SharedPtr,
        const std::shared_ptr<const AgentChatInteraction::Feedback> feedback) {
        on_goal_feedback(feedback);
      };

    send_goal_options.result_callback =
      [this](const AgentChatGoalHandle::WrappedResult & result) {
        handle_chat_result(result);
      };

    return send_goal_options;
  }

  void on_goal_response(const AgentChatGoalHandle::SharedPtr & goal_handle)
  {
    if (!goal_handle) {
      RCLCPP_ERROR(get_logger(), "Agent chat action goal was rejected");
      finish_current_chat_goal();
      send_next_chat_goal();
      return;
    }

    RCLCPP_INFO(get_logger(), "Agent chat action goal accepted");
  }

  void on_goal_feedback(const std::shared_ptr<const AgentChatInteraction::Feedback> & feedback)
  {
    if (feedback && !feedback->current_status.empty()) {
      RCLCPP_INFO(
        get_logger(),
        "Agent chat action feedback: %s",
        feedback->current_status.c_str());
    }
  }

  // -------- Action result --------
  void handle_chat_result(const AgentChatGoalHandle::WrappedResult & wrapped_result)
  {
    switch (wrapped_result.code) {
      case rclcpp_action::ResultCode::SUCCEEDED:
        log_successful_chat_result(wrapped_result);
        break;
      case rclcpp_action::ResultCode::ABORTED:
        RCLCPP_ERROR(get_logger(), "Agent chat action aborted");
        break;
      case rclcpp_action::ResultCode::CANCELED:
        RCLCPP_WARN(get_logger(), "Agent chat action canceled");
        break;
      default:
        RCLCPP_ERROR(get_logger(), "Agent chat action returned an unknown result code");
        break;
    }

    finish_current_chat_goal();
    send_next_chat_goal();
  }

  void log_successful_chat_result(const AgentChatGoalHandle::WrappedResult & wrapped_result)
  {
    if (!wrapped_result.result) {
      RCLCPP_ERROR(get_logger(), "Agent chat action succeeded with an empty result");
      return;
    }

    const auto & result = *wrapped_result.result;
    if (!result.success) {
      RCLCPP_ERROR(get_logger(), "Agent chat action failed: %s", result.error.c_str());
      return;
    }

    RCLCPP_INFO(get_logger(), "Agent chat action response: %s", result.response.c_str());
    if (result.command_string != "none") {
      RCLCPP_INFO(
        get_logger(),
        "Agent chat action command: %s execution_status: %s",
        result.command_string.c_str(),
        result.execution_status.c_str());
    }
  }

  void finish_current_chat_goal()
  {
    std::lock_guard<std::mutex> lock{chat_mutex_};
    chat_goal_in_flight_ = false;
  }

  // -------- State --------
  std::string agent_chat_action_name_{"agent_chat"};
  std::string chat_model_{};
  rclcpp_action::Client<AgentChatInteraction>::SharedPtr chat_client_{};
  asr::manager::Manager<> manager_;
  rclcpp::TimerBase::SharedPtr vad_timer_{};
  rclcpp::TimerBase::SharedPtr chat_retry_timer_{};
  std::mutex chat_mutex_{};
  std::deque<std::string> pending_prompts_{};
  bool chat_goal_in_flight_{false};
};

}  // namespace asr_node

#endif  // ASR_NODE_ASR_NODE_HPP

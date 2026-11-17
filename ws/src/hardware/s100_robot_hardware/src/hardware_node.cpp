#include "encos_motors/protocol.hpp"
#include "s100_can/transport.hpp"
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>

class HardwareNode final : public rclcpp::Node {
 public:
  HardwareNode() : Node("s100_hardware") {
    s100_can::Config config;
    config.backend = declare_parameter<std::string>("backend", "mock");
    config.target = declare_parameter<std::string>("can_target", "can5");
    config.config_directory = declare_parameter<std::string>("canhal_config_directory", "");
    const auto channel = declare_parameter<int>("physical_channel", 5);
    const auto motor_id = declare_parameter<int>("motor_id", 1);
    const auto model = declare_parameter<std::string>("motor_model", "EC-A4310-P2-36");
    const auto hz = declare_parameter<double>("update_rate", 250.0);
    max_cycles_ = declare_parameter<int>("max_cycles", 0);
    joint_name_ = declare_parameter<std::string>("joint_name", "test_joint");
    mock_query_ = declare_parameter<bool>("mock_query_enabled", true);
    if (channel < 5 || channel > 9 || motor_id < 1 || motor_id >= 0x7ff ||
        !std::isfinite(hz) || hz < 1 || hz > 1000 || max_cycles_ < 0) {
      throw std::invalid_argument("Invalid CAN channel, motor ID, rate or cycle count");
    }
    config.physical_channel = static_cast<uint8_t>(channel);
    motor_id_ = static_cast<uint16_t>(motor_id);
    limits_ = encos_motors::model_limits(model);
    transport_ = s100_can::make_transport(config);
    is_mock_ = config.backend == "mock";
    publisher_ = create_publisher<sensor_msgs::msg::JointState>("joint_states", 10);
    timer_ = create_wall_timer(std::chrono::nanoseconds(static_cast<int64_t>(1e9 / hz)),
                              [this]() { cycle(); });
    RCLCPP_INFO(get_logger(), "backend=%s; CAN%d; motor=%d; %.1f Hz requested; hardware TX unavailable in this node",
                config.backend.c_str(), static_cast<int>(channel), static_cast<int>(motor_id), hz);
  }

  bool failed() const { return failed_; }

 private:
  void cycle() {
    try {
      // Only mock receives queries. Physical CANHAL mode is passive RX only.
      if (is_mock_ && mock_query_) transport_->send(encos_motors::query_brake_state(motor_id_));
      s100_can::Frame frame;
      if (transport_->receive(frame) && frame.id == motor_id_) {
        const auto feedback = encos_motors::decode_feedback(frame, limits_);
        if (feedback) {
          if (feedback->error != 0) {
            RCLCPP_WARN(get_logger(), "Motor %u reports error %u", motor_id_, feedback->error);
          }
          sensor_msgs::msg::JointState state;
          state.header.stamp = now();
          state.name = {joint_name_};
          state.position = {feedback->position};
          state.velocity = {feedback->velocity};
          state.effort = {feedback->torque};
          publisher_->publish(state);
        }
      }
      if (max_cycles_ > 0 && ++cycles_ >= max_cycles_) {
        RCLCPP_INFO(get_logger(), "Completed %d cycles", cycles_);
        rclcpp::shutdown();
      }
    } catch (const std::exception& error) {
      failed_ = true;
      timer_->cancel();
      RCLCPP_ERROR(get_logger(), "Communication stopped: %s", error.what());
      rclcpp::shutdown();
    }
  }

  std::unique_ptr<s100_can::Transport> transport_;
  encos_motors::Limits limits_{};
  uint16_t motor_id_{1};
  bool is_mock_{true};
  bool mock_query_{true};
  bool failed_{false};
  int max_cycles_{0};
  int cycles_{0};
  std::string joint_name_;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  int status = 0;
  try {
    auto node = std::make_shared<HardwareNode>();
    rclcpp::spin(node);
    status = node->failed() ? 1 : 0;
  } catch (const std::exception& error) {
    RCLCPP_ERROR(rclcpp::get_logger("s100_hardware"), "%s", error.what());
    status = 1;
  }
  if (rclcpp::ok()) rclcpp::shutdown();
  return status;
}

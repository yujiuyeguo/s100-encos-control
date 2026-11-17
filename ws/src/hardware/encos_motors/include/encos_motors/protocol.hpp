#pragma once

#include "s100_can/transport.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace encos_motors {
struct Limits {
  double position;
  double speed;
  double torque;
  double current;
  double kp;
  double kd;
  double torque_constant;
};

struct MitCommand {
  double position{0}; // rad
  double velocity{0}; // rad/s
  double kp{0};
  double kd{0};
  double torque{0};   // Nm
};

struct Feedback {
  uint16_t motor_id;
  uint8_t error;
  double position;
  double velocity;
  double current;
  double torque;
  double motor_temperature;
  double mos_temperature;
};

// Values and bit layout ported from the existing ieTDZK-T001 ENCOS driver.
Limits model_limits(const std::string& model);
s100_can::Frame encode_mit(uint16_t id, const MitCommand& command, const Limits& limits);
s100_can::Frame query_brake_state(uint16_t id);
std::optional<Feedback> decode_feedback(const s100_can::Frame& frame, const Limits& limits);
}  // namespace encos_motors

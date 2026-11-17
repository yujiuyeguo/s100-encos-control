#include "encos_motors/protocol.hpp"

#include <cmath>
#include <stdexcept>

namespace encos_motors {
namespace {
void validate_id(uint16_t id) {
  if (id == 0 || id >= 0x7ff) throw std::invalid_argument("ENCOS motor ID must be 1..0x7fe");
}

void validate_limits(const Limits& limits) {
  for (double value : {limits.position, limits.speed, limits.torque, limits.current,
                       limits.kp, limits.kd, limits.torque_constant}) {
    if (!std::isfinite(value) || value <= 0) throw std::invalid_argument("Invalid ENCOS model limits");
  }
}

uint64_t quantize(double value, double low, double high, unsigned bits) {
  if (!std::isfinite(value) || value < low || value > high) {
    throw std::invalid_argument("ENCOS command outside model range or non-finite");
  }
  return static_cast<uint64_t>((value - low) / (high - low) * ((uint64_t{1} << bits) - 1));
}

double expand(uint16_t value, double low, double high, unsigned bits) {
  return low + static_cast<double>(value) / ((uint64_t{1} << bits) - 1) * (high - low);
}
}  // namespace

Limits model_limits(const std::string& model) {
  if (model == "EC-A2806-P2-36") return {12.5, 18, 12, 10, 500, 5, 1.35};
  if (model == "EC-A4310-P2-36") return {12.5, 18, 30, 30, 500, 5, 1.40};
  if (model == "EC-A4315-P2-36") return {12.5, 18, 70, 30, 500, 5, 2.80};
  if (model == "EC-A6408-P2-25") return {12.5, 18, 60, 60, 500, 5, 2.35};
  if (model == "EC-A8112-P1-18" || model == "EC-A8112-P1-18H") return {12.5, 18, 90, 60, 500, 5, 2.10};
  if (model == "EC-A10020-P1-12") return {12.5, 18, 150, 70, 500, 50, 2.50};
  throw std::invalid_argument("Unknown ENCOS model: " + model);
}

s100_can::Frame encode_mit(uint16_t id, const MitCommand& command, const Limits& limits) {
  validate_id(id);
  validate_limits(limits);
  const auto p = quantize(command.position, -limits.position, limits.position, 16);
  const auto v = quantize(command.velocity, -limits.speed, limits.speed, 12);
  const auto kp = quantize(command.kp, 0, limits.kp, 12);
  const auto kd = quantize(command.kd, 0, limits.kd, 9);
  const auto t = quantize(command.torque, -limits.torque, limits.torque, 12);
  const uint64_t payload = (kp << 49) | (kd << 40) | (p << 24) | (v << 12) | t;
  s100_can::Frame frame{id, 8, {}};
  for (unsigned i = 0; i < 8; ++i) frame.data[i] = (payload >> ((7 - i) * 8)) & 0xff;
  return frame;
}

s100_can::Frame query_brake_state(uint16_t id) {
  validate_id(id);
  return {id, 2, {0xe1, 0x25}};
}

std::optional<Feedback> decode_feedback(const s100_can::Frame& frame, const Limits& limits) {
  s100_can::validate_frame(frame);
  validate_id(frame.id);
  validate_limits(limits);
  // Only the full MIT feedback type is decoded in this foundation.
  if (frame.length != 8 || (frame.data[0] >> 5) != 1) return std::nullopt;
  const uint16_t p = (uint16_t(frame.data[1]) << 8) | frame.data[2];
  const uint16_t v = (uint16_t(frame.data[3]) << 4) | (frame.data[4] >> 4);
  const uint16_t c = (uint16_t(frame.data[4] & 0xf) << 8) | frame.data[5];
  const double current = expand(c, -limits.current, limits.current, 12);
  return Feedback{frame.id, uint8_t(frame.data[0] & 0x1f),
    expand(p, -limits.position, limits.position, 16),
    expand(v, -limits.speed, limits.speed, 12), current, current * limits.torque_constant,
    (double(frame.data[6]) - 50) / 2, (double(frame.data[7]) - 50) / 2};
}
}  // namespace encos_motors

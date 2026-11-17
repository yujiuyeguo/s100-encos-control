#include "encos_motors/protocol.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

int main() {
  using namespace encos_motors;
  const auto limits = model_limits("EC-A4310-P2-36");
  const auto zero = encode_mit(1, {}, limits);
  // Fixed wire vector: kp=kd=0, p=32767, v=t=2047.
  const std::array<uint8_t, 8> expected{0, 0, 0, 0x7f, 0xff, 0x7f, 0xf7, 0xff};
  if (zero.id != 1 || zero.length != 8 || zero.data != expected) return 1;
  const auto maximum = encode_mit(2, {12.5, 18, 500, 5, 30}, limits);
  const std::array<uint8_t, 8> upper{0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  if (maximum.data != upper) return 2;
  const auto query = query_brake_state(3);
  if (query.id != 3 || query.length != 2 || query.data[0] != 0xe1 || query.data[1] != 0x25) return 3;
  // Known endpoint feedback: max position/speed/current and 25/30 degrees C.
  const auto feedback = decode_feedback({3, 8, {0x22, 0xff, 0xff, 0xff, 0xff, 0xff, 100, 110}}, limits);
  if (!feedback || feedback->error != 2 || feedback->position != 12.5 ||
      feedback->velocity != 18 || feedback->current != 30 ||
      std::abs(feedback->torque - 42) > 1e-9 ||
      feedback->motor_temperature != 25 || feedback->mos_temperature != 30) return 4;
  if (decode_feedback(query, limits)) return 5;
  try { encode_mit(1, {0, 0, 0, 5.1, 0}, limits); return 6; }
  catch (const std::invalid_argument&) {}
  try { encode_mit(1, {std::numeric_limits<double>::quiet_NaN(), 0, 0, 0, 0}, limits); return 7; }
  catch (const std::invalid_argument&) {}
  try { query_brake_state(0x7ff); return 8; }
  catch (const std::invalid_argument&) {}
  try { model_limits("unknown"); return 9; }
  catch (const std::invalid_argument&) {}
  return 0;
}

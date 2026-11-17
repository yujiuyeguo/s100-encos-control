#include "s100_can/transport.hpp"

#include <stdexcept>

namespace s100_can {
#ifdef S100_ENABLE_CANHAL
std::unique_ptr<Transport> make_canhal_transport(const Config& config);
#endif

void validate_frame(const Frame& frame) {
  if (frame.id > 0x7ff || frame.length > 8) {
    throw std::invalid_argument("Only standard classic CAN frames (ID <= 0x7ff, length <= 8) are supported");
  }
}

void MockTransport::send(const Frame& frame) {
  validate_frame(frame);
  last_sent_ = frame;
  ++sent_count_;
}

bool MockTransport::receive(Frame& frame) {
  if (!has_receive_) return false;
  frame = pending_receive_;
  has_receive_ = false;
  return true;
}

void MockTransport::inject_receive(const Frame& frame) {
  validate_frame(frame);
  pending_receive_ = frame;
  has_receive_ = true;
}

bool canhal_compiled() {
#ifdef S100_ENABLE_CANHAL
  return true;
#else
  return false;
#endif
}

std::unique_ptr<Transport> make_transport(const Config& config) {
  if (config.backend == "mock") return std::make_unique<MockTransport>();
  if (config.backend != "canhal") throw std::invalid_argument("Unknown CAN backend: " + config.backend);
#ifdef S100_ENABLE_CANHAL
  return make_canhal_transport(config);
#else
  throw std::runtime_error("CANHAL not compiled; rebuild on S100 with -DS100_ENABLE_CANHAL=ON");
#endif
}
}  // namespace s100_can

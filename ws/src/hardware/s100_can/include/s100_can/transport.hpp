#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace s100_can {

// Host byte order, standard classic CAN only. CANHAL conversion stays in its backend.
struct Frame {
  uint16_t id{0};
  uint8_t length{0};
  std::array<uint8_t, 8> data{};
};

struct Config {
  std::string backend{"mock"};
  std::string target{"can5"};
  std::string config_directory;
  uint8_t physical_channel{5};
};

class Transport {
 public:
  virtual ~Transport() = default;
  virtual void send(const Frame& frame) = 0;
  // Empty receive returns false. CANHAL receive may block up to its configured timeout.
  virtual bool receive(Frame& frame) = 0;
};

// Mock records TX without manufacturing motor feedback; RX can be injected by tests.
class MockTransport final : public Transport {
 public:
  void send(const Frame& frame) override;
  bool receive(Frame& frame) override;
  void inject_receive(const Frame& frame);
  const Frame& last_sent() const { return last_sent_; }
  uint64_t sent_count() const { return sent_count_; }

 private:
  Frame last_sent_{};
  Frame pending_receive_{};
  uint64_t sent_count_{0};
  bool has_receive_{false};
};

// One owner / thread per transport. CANHAL is process-global: one instance per process.
std::unique_ptr<Transport> make_transport(const Config& config);
void validate_frame(const Frame& frame);
bool canhal_compiled();

}  // namespace s100_can

#include "s100_can/transport.hpp"

#include <arpa/inet.h>
#include <hobot_can_hal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace s100_can {
namespace {
std::atomic<bool> in_use{false};

class CanhalTransport final : public Transport {
 public:
  explicit CanhalTransport(const Config& config) : config_(config) {
    if (config_.physical_channel < 5 || config_.physical_channel > 9 || config_.target.empty()) {
      throw std::invalid_argument("S100 expansion board requires physical CAN channel 5..9 and a target");
    }
    if (in_use.exchange(true)) throw std::runtime_error("Only one CANHAL owner per process is supported");
    try {
      // SDK 4.0.5 loads ./config/*.json. Resolve before changing CWD and restore
      // immediately after Init; construct before starting any other threads.
      const auto root = std::filesystem::absolute(config_.config_directory);
      for (const char* file : {"nodes.json", "channels.json", "ipcf_channel.json"}) {
        if (!std::filesystem::is_regular_file(root / "config" / file)) {
          throw std::runtime_error("Missing CANHAL configuration: " + (root / "config" / file).string());
        }
      }
      const auto previous = std::filesystem::current_path();
      std::filesystem::current_path(root);
      int rc;
      try {
        rc = canInit();
      } catch (...) {
        std::filesystem::current_path(previous);
        throw;
      }
      initialized_ = rc >= 0;
      std::filesystem::current_path(previous);
      if (rc < 0) throw std::runtime_error("canInit failed: " + std::to_string(rc));
    } catch (...) {
      if (initialized_) canDeInit();
      in_use.store(false);
      throw;
    }
  }

  ~CanhalTransport() override {
    if (initialized_) canDeInit();
    in_use.store(false);
  }

  void send(const Frame& frame) override {
    validate_frame(frame);
    struct canframe tx{};
    tx.time_stamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    // Matches /app/Can/can_send/canhal_send.c on SDK 4.0.5-Beta.
    tx.canid = htonl(frame.id);
    tx.count = sequence_++;
    tx.can_type = CANType_Can;
    tx.can_channel = config_.physical_channel;
    tx.len = frame.length;
    std::copy_n(frame.data.begin(), frame.length, tx.data);
    struct pack_info pack{};
    pack.data_num = 1;
    pack.length = 1;
    const int rc = canSendMsgFrame(config_.target.c_str(), &tx, &pack);
    if (rc <= 0) throw std::runtime_error("CANHAL did not accept frame: " + std::to_string(rc));
  }

  bool receive(Frame& frame) override {
    if (next_ == available_) {
      struct pack_info pack{};
      pack.length = static_cast<uint16_t>(rx_.size());
      const int rc = canRecvMsgFrame(config_.target.c_str(), rx_.data(), &pack);
      // -303 is also used for IPC read timeout. Other failures are surfaced.
      if (rc == -303) return false;
      if (rc < 0) throw std::runtime_error("CANHAL receive failed: " + std::to_string(rc));
      if (pack.data_num > rx_.size()) throw std::runtime_error("Invalid CANHAL receive count");
      next_ = 0;
      available_ = pack.data_num;
    }
    while (next_ < available_) {
      const auto& rx = rx_[next_++];
      // SDK example prints received canid directly (host order); TX uses htonl.
      if (rx.can_channel != config_.physical_channel || rx.can_type != CANType_Can ||
          rx.len > 8 || rx.canid > 0x7ff) continue;
      frame = Frame{};
      frame.id = static_cast<uint16_t>(rx.canid);
      frame.length = rx.len;
      std::copy_n(rx.data, rx.len, frame.data.begin());
      return true;
    }
    return false;
  }

 private:
  Config config_;
  bool initialized_{false};
  uint8_t sequence_{0};
  std::array<struct canframe, 32> rx_{};
  std::size_t next_{0};
  std::size_t available_{0};
};
}  // namespace

std::unique_ptr<Transport> make_canhal_transport(const Config& config) {
  return std::make_unique<CanhalTransport>(config);
}
}  // namespace s100_can

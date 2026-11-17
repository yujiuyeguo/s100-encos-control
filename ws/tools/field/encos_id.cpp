// ENCOS V1.16 sections 7.3 / 8.1. Only CAN5 ID query and ID change; no motion commands.
#include "encos_id_protocol.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#ifdef S100_ID_CANHAL
#include <arpa/inet.h>
#include <hobot_can_hal.h>
#endif
using Clock = std::chrono::steady_clock;
void print_frame(const char* prefix, const encos_id::Frame& f) {
  std::printf("%s CAN5 id=0x%03X len=%u data=", prefix, f.id, f.length);
  for (unsigned i = 0; i < f.length; ++i) std::printf("%02X ", f.data[i]);
  std::puts("");
}
unsigned parse_id(const std::string& value) {
  std::size_t used = 0;
  auto id = std::stoul(value, &used, 0);
  if (used != value.size() || id > 0x7fe) throw std::invalid_argument("Invalid ID");
  encos_id::validate_id(static_cast<unsigned>(id));
  return static_cast<unsigned>(id);
}
#ifdef S100_ID_CANHAL
class Bus {
 public:
  Bus() {
    int rc = canInit();
    if (rc < 0) throw std::runtime_error("canInit failed: " + std::to_string(rc));
  }
  ~Bus() { canDeInit(); }
  void send(const encos_id::Frame& f) {
    canframe tx{};
    tx.time_stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    tx.canid = htonl(f.id); tx.count = sequence_++; tx.can_type = CANType_Can;
    tx.can_channel = 5; tx.len = f.length;
    std::copy_n(f.data.begin(), f.length, tx.data);
    pack_info pack{}; pack.data_num = 1; pack.length = 1;
    print_frame("TX", f);
    int rc = canSendMsgFrame("can5", &tx, &pack);
    std::printf("CANHAL submit ret=%d (not motor confirmation)\n", rc);
    if (rc < 0) throw std::runtime_error("CANHAL send failed: " + std::to_string(rc));
  }
  template <class Callback> void receive_for(Callback callback) {
    auto deadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < deadline) {
      canframe frames[32]{}; pack_info pack{}; pack.length = 32;
      int rc = canRecvMsgFrame("can5", frames, &pack);
      if (rc == -303) continue;
      if (rc < 0 || pack.data_num > 32) throw std::runtime_error("Invalid receive: " + std::to_string(rc));
      for (unsigned i = 0; i < pack.data_num; ++i) {
        const auto& rx = frames[i];
        // SDK 4.0.5 RX IDs are host order, unlike TX.
        if (rx.can_channel != 5 || rx.can_type != CANType_Can || rx.canid != 0x7ff || rx.len > 8) continue;
        encos_id::Frame f{}; f.id = rx.canid; f.length = rx.len;
        std::copy_n(rx.data, rx.len, f.data.begin());
        print_frame("RX", f); callback(f);
      }
    }
  }
  unsigned discover() {
    std::set<unsigned> ids;
    send(encos_id::query());
    receive_for([&](const auto& f) { if (auto id = encos_id::queried_id(f)) ids.insert(*id); });
    if (ids.size() != 1) throw std::runtime_error("Expected exactly one responding ID; found " + std::to_string(ids.size()));
    std::printf("Motor replied: ID=%u\n", *ids.begin());
    return *ids.begin();
  }
 private:
  uint8_t sequence_{0};
};
#endif
int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  try {
    if (argc < 2 || std::string(argv[1]) == "--help") {
      std::puts("Usage: encos_id query [--hardware]\n       encos_id set --new ID [--old ID] [--hardware]\nDefault: offline preview. Hardware: CAN5 only, one motor, powered for >=5s.\nSet discovers current ID, checks optional --old, changes once, verifies ACK and query.");
      return argc < 2 ? 2 : 0;
    }
    std::string action = argv[1]; bool hardware = false;
    unsigned new_id = 0, old_id = 0;
    for (int i = 2; i < argc; ++i) {
      std::string key = argv[i];
      if (key == "--hardware") hardware = true;
      else if ((key == "--new" || key == "--old") && i + 1 < argc) {
        unsigned id = parse_id(argv[++i]);
        if (key == "--new") new_id = id; else old_id = id;
      } else throw std::invalid_argument("Unknown or incomplete option: " + key);
    }
    if (action != "query" && action != "set") throw std::invalid_argument("Use query or set");
    if ((action == "set" && !new_id) || (action == "query" && (new_id || old_id)))
      throw std::invalid_argument("set requires --new ID; query takes no IDs");
    if (!hardware) {
      std::puts("OFFLINE PREVIEW: no CANHAL initialization or physical frames");
      print_frame("QUERY", encos_id::query());
      if (action == "set") {
        if (old_id) print_frame("SET", encos_id::set(old_id, new_id));
        else std::printf("Hardware will discover current ID, then set ID=%u\n", new_id);
      }
      return 0;
    }
#ifdef S100_ID_CANHAL
    Bus bus;
    unsigned current = bus.discover();
    if (action == "query") return 0;
    if (old_id && current != old_id) throw std::runtime_error("Discovered ID differs from --old; no change sent");
    if (current == new_id) { std::puts("PASS: requested ID already confirmed; no configuration write needed"); return 0; }
    bus.send(encos_id::set(current, new_id));
    // No subsequent CAN command for >500ms after configuration (manual section 3.5).
    bool ack = false;
    bus.receive_for([&](const auto& f) { ack = ack || encos_id::set_ack(f, new_id); });
    unsigned confirmed = bus.discover();
    if (!ack || confirmed != new_id) throw std::runtime_error("Change not fully confirmed; do not blindly repeat; query first");
    std::printf("PASS: motor ACK and re-query confirmed ID=%u\n", confirmed);
    return 0;
#else
    throw std::runtime_error("Offline build: --hardware unavailable; prepare on S100");
#endif
  } catch (const std::exception& e) {
    std::fprintf(stderr, "ERROR: %s\n", e.what()); return 1;
  }
}

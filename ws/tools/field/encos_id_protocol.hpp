#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace encos_id {
struct Frame {
  uint16_t id{0x7ff};
  uint8_t length{0};
  std::array<uint8_t, 8> data{};
};
inline void validate_id(unsigned id) {
  if (id < 1 || id > 0x7fe) throw std::invalid_argument("Motor ID must be 1..2046 (0x7fe)");
}
inline Frame query() { return {0x7ff, 4, {0xff, 0xff, 0x00, 0x82}}; }
inline Frame set(unsigned old_id, unsigned new_id) {
  validate_id(old_id); validate_id(new_id);
  return {0x7ff, 6, {static_cast<uint8_t>(old_id >> 8), static_cast<uint8_t>(old_id),
                    0, 4, static_cast<uint8_t>(new_id >> 8), static_cast<uint8_t>(new_id)}};
}
inline std::optional<unsigned> queried_id(const Frame& f) {
  if (f.id != 0x7ff || f.length != 5 || f.data[0] != 0xff ||
      f.data[1] != 0xff || f.data[2] != 1) return std::nullopt;
  unsigned id = (unsigned(f.data[3]) << 8) | f.data[4];
  if (id < 1 || id > 0x7fe) return std::nullopt;
  return id;
}
inline bool set_ack(const Frame& f, unsigned new_id) {
  return f.id == 0x7ff && f.length == 4 && f.data[0] == (new_id >> 8) &&
         f.data[1] == (new_id & 0xff) && f.data[2] == 1 && f.data[3] == 4;
}
}  // namespace encos_id

#include "../encos_id_protocol.hpp"
#include <cassert>
int main() {
  using namespace encos_id;
  const auto q = query();
  assert(q.id == 0x7ff && q.length == 4 && q.data[3] == 0x82);
  const auto s = set(0x123, 0x456);
  const std::array<uint8_t,8> expected{1,0x23,0,4,4,0x56,0,0};
  assert(s.length == 6 && s.data == expected);
  Frame reply{0x7ff,5,{0xff,0xff,1,1,0x23}};
  assert(queried_id(reply) == 0x123);
  reply.data[2] = 0; assert(!queried_id(reply)); // TX echo is not a motor reply.
  reply = {0x7ff,4,{4,0x56,1,4}};
  assert(set_ack(reply,0x456) && !set_ack(reply,1));
  reply.data[3] = 0; assert(!set_ack(reply,0x456)); // Failure reply.
  reply.data[3] = 4; reply.length = 3; assert(!set_ack(reply,0x456));
  for (unsigned bad : {0U,0x7ffU}) {
    bool rejected=false;
    try { set(1,bad); } catch (const std::invalid_argument&) { rejected=true; }
    assert(rejected);
  }
}

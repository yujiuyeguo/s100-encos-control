#include "s100_can/transport.hpp"
#include <stdexcept>

int main() {
  s100_can::MockTransport transport;
  s100_can::Frame frame{1, 2, {0xe1, 0x25}};
  transport.send(frame);
  if (transport.sent_count() != 1 || transport.last_sent().data != frame.data) return 1;
  s100_can::Frame received;
  if (transport.receive(received)) return 2; // TX must not create fake feedback.
  transport.inject_receive(frame);
  if (!transport.receive(received) || received.id != 1 || received.length != 2) return 3;
  if (transport.receive(received)) return 4;
  try { transport.send({0x800, 1, {0}}); return 5; }
  catch (const std::invalid_argument&) {}
  try { transport.send({1, 9, {0}}); return 6; }
  catch (const std::invalid_argument&) {}
  try { s100_can::make_transport({"invalid", "can5", "", 5}); return 7; }
  catch (const std::invalid_argument&) {}
  return 0;
}

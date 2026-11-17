// S100 SDK 4.0.5 IpcBox RunCmd: only the commands needed for time synchronization.
#include <hb_ipcf_hal.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
struct Packet {
  uint32_t magic;
  uint32_t version;
  uint32_t checksum;
  uint32_t length;
  char command[64];
  uint8_t reserved[48];
};
static_assert(sizeof(Packet) == 128, "IpcBox v3 requires a 128-byte header");

bool submit(IPCFHAL_Channel& channel, const char* command) {
  alignas(64) Packet packet{};
  packet.magic = 0x43504944;
  packet.version = 3;
  packet.length = sizeof(packet);
  std::strcpy(packet.command, command);
  const auto* bytes = reinterpret_cast<const uint8_t*>(&packet);
  uint32_t checksum = 0;
  for (std::size_t i = 0; i < sizeof(packet); ++i) checksum += bytes[i];
  packet.checksum = checksum;
  const int rc = HorizonHal_IPCF_Send(bytes, sizeof(packet), &channel);
  std::printf("IpcBox submitted '%s': ret=%d, expected=%zu\n", command, rc, sizeof(packet));
  // RunCmd is asynchronous; completion must be checked through MCU logs / actual sync.
  if (rc != static_cast<int>(sizeof(packet))) return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 || (std::strcmp(argv[1], "init-mcu") != 0 &&
                    std::strcmp(argv[1], "debug-on") != 0 &&
                    std::strcmp(argv[1], "debug-off") != 0)) {
    std::fprintf(stderr, "Usage: %s init-mcu|debug-on|debug-off\n", argv[0]);
    return 2;
  }

  IPCFHAL_Channel channel{};
  channel.instance = 7;
  channel.id = 0;
  std::strcpy(channel.name, "cpu2mcu_ins7ch0");
  channel.fifo_size = 64000;
  std::strcpy(channel.ipcf_dev.dev_path, "/dev/ipcdrv");
  std::strcpy(channel.ipcf_dev.dev_name, "ipcdrv");
  channel.ipcf_hal.pkg_size_max = 4096;

  int rc = HorizonHal_IPCF_Init(&channel);
  if (rc < 0) {
    std::fprintf(stderr, "IpcBox init failed: %d\n", rc);
    return 1;
  }
  rc = HorizonHal_IPCF_Config(&channel);
  bool success = false;
  if (rc < 0) {
    std::fprintf(stderr, "IpcBox config failed: %d\n", rc);
  } else if (std::strcmp(argv[1], "init-mcu") == 0) {
    // Once per MCU1 boot. Repeating Timesync_Init on this firmware breaks IPC readiness.
    success = submit(channel, "TimeSyncCtrl 4 10") && submit(channel, "TimeSyncCtrl 6");
  } else {
    success = submit(channel, std::strcmp(argv[1], "debug-on") == 0 ?
                              "TimeSyncCtrl 1" : "TimeSyncCtrl 0");
  }
  HorizonHal_IPCF_Deinit(&channel);
  return success ? 0 : 1;
}

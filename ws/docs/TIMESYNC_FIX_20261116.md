# 时间同步 IPC 发送失败修复

任务日期：2026-11-16。平台：RDK S100 V1P1 / 4.0.5-Beta，MCU1 GccDebugLiteSIP_V2.0（Mar 3 2026）。

## 原因与修复

1. **MCU 同步服务默认未启动。** Linux 示例可以注册 instance10/channel0，但内核发送日志为 `instance 10 no ready`；strace 显示写 `/dev/ipcdrv` 返回 EAGAIN，libhbipcfhal 将它转换为 -14（IPCF_HAL_E_CHANNEL_INVALID）。因此不是 CAN5/CAN6 接线、FIFO 容量或电机协议的问题。
2. **固件重复初始化有缺陷。** 从官方 ELF 的符号与反汇编可见，IPC_Init 每次调用 Ipc_MDMA_Init，但 TimeSync_Ipc_Instance3_Open_Func 的静态标志只允许第一次执行 OpenInstance。重复调用 TimeSyncCtrl 4/6 后，共享区重新初始化，OpenInstance 却被跳过，造成再次 no ready。规避方式：每次 MCU1 启动后只初始化一次，后续只重启 Acore 程序。此次曾 stop MCU1，确认 core0/core1 enter stop、offline/dead 后重新加载同一 ELF，清理测试期间的状态；未刷写 Flash。
3. **PPS 配对及无效快照。** 改用独立 MCU Ethernet PPS：MCU 选择编号10，Linux 示例选择索引4（`mcu_eth_sync`）。不再以正在被校时的 RTC 自身 PPS 作为触发源。原始方案曾出现 PHC=0 快照却返回成功，继续发送会把 RTC 校成错误时间；补丁丢弃非法快照。
4. **现有 eth0 周期性断链会清除 PHC 捕获配置。** 内核日志约每50秒一次 Link Down/Up，与后续连续零值 PHC 快照相对应。补丁在非法快照时重新调用 timesyncSetClockSnapshotSource，再等待下次 PPS；不发送该次无效时间。本任务不修改 NetworkManager / 网口配置。

## 项目工具

- `ws/tools/field/mcu_timesync_control.cpp`：通过官方 IpcBox v3 / instance7/channel0 提交固定同步命令，不提供任意 MCU shell 或电机命令入口。提交成功不单独等同于 MCU 已执行；以同步日志及实际快照为准。
- `ws/tools/field/timesync.sh`：准备、首次初始化、持续运行和日志开关。
- `ws/tools/field/timesync_sample.patch`：只修改项目 build 目录中的厂商示例副本，保留 -14 等实际错误码；拒绝非法快照，并恢复 PHC 捕获配置。
- 板端路径：`/home/sunrise/s100-encos-control`。厂商 `/app` 源码、原机器人项目和 Flash 分区均未修改。

## 人工操作

在板端：

```bash
cd ~/s100-encos-control
bash ws/tools/field/timesync.sh --prepare
```

仅在 MCU1 刚启动、尚未初始化时间同步服务时：

```bash
bash ws/tools/field/timesync.sh --init-mcu
```

这会依次提交 `TimeSyncCtrl 4 10` 和 `TimeSyncCtrl 6`。同一次 MCU1 启动内不要重复。当前修复会话已执行过初始化，直接运行：

```bash
bash ws/tools/field/timesync.sh --run
```

在前台运行，Ctrl+C 停止。重新启动这个 Linux 进程只需再次 `--run`；不要再初始化 MCU。未设置开机自启或后台服务。该命令同步 PHC0 到 MCU RTC，使用 `-s 0`；不启动外部 gPTP、不调整 Linux NTP 服务。

需要看 MCU 接收与同步误差时，在另一个终端：

```bash
bash ws/tools/field/timesync.sh --debug-on
sudo timeout 3s cat /proc/remoteproc_mcu1
bash ws/tools/field/timesync.sh --debug-off
```

## 如果误重复初始化，需要恢复 MCU1

先 Ctrl+C 停止自己启动的同步程序；确认没有其他机器人任务依赖 MCU1 后再进行运行时重启（会中断所有 MCU1 业务）：

```bash
echo stop | sudo tee /sys/class/remoteproc/remoteproc_mcu0/state
sleep 2
sudo timeout 3s cat /proc/remoteproc_mcu1
cat /sys/class/remoteproc/remoteproc_mcu0/state
cat /sys/class/remoteproc/remoteproc_mcu1/alive
```

只有日志确认 core0/core1 已进入 stop，且状态为 offline / dead 后才继续，不能在尚未停稳时重新加载：

```bash
echo S100_MCU_DEBUG_4.0.5_20260507.elf | sudo tee /sys/class/remoteproc/remoteproc_mcu0/firmware
echo start | sudo tee /sys/class/remoteproc/remoteproc_mcu0/state
sleep 3
cat /sys/class/remoteproc/remoteproc_mcu1/alive
```

恢复 alive 后，执行一次 `--init-mcu`，然后执行 `--run`。不要用重复 init 代替恢复，不修改或绕过 CANHAL 的延迟检查。

## 验证与限制

- PASS：fresh MCU1 boot 后单次初始化；诊断副本连续12秒发送成功。MCU 日志确认 `Get Time From Acore success`、`TimeKeeperRTC and TimeKeeperIPC Success`；首次跳变后快照误差绝对值约43.8～71.3µs。
- PASS：停止后重启 Acore 同步进程（不重复 MCU 初始化），8秒内8次发送成功、无零值快照、无发送错误。
- PASS：最终部署工具连续运行65秒，62次同步发送成功，2次无效 PHC 快照被拦截并重新配置捕获，随后恢复，IPC 发送错误0次。跳过前两次有效快照后的绝对偏差范围37,820～1,922,129ns（约37.8µs～1.923ms），不能用前述12秒结果概括最终精度。该结果证明此窗口内的恢复行为，不构成长期稳定性验收。
- 测试已结束；MCU1 管理节点 running、心跳 alive，MCU调试日志已关闭，无本次同步进程后台运行。下次直接 `--run`，本次 MCU 启动内不要再次 `--init-mcu`。
- 这是本板固件的运行时规避方案，未修改厂家固件中一次性 OpenInstance 的实现。需遵守单次初始化顺序。
- 无电机或 CAN 发帧；不把短时同步误差当作长期精度、250Hz 控制实时性或全系统时序保证。

证据目录：`log/timesync_fix_20261116/`。原始失败：`reproduce.log`；服务启用：`enable.txt`；首次收敛：`fresh_boot.txt` / `mcu_fresh_boot.txt`；Acore 重启：`acore_restart.txt`；工具准备：`prepare_tool.txt`；最终65秒验收：`rearm_65s.txt`、`final_summary.json`；结束状态：`final_mcu_status.txt`。

有界自动测试须让 timeout 与被测进程具有相同权限，例如 `sudo timeout -s INT -k 2s 65s bash ws/tools/field/timesync.sh --run`。预期超时退出码124；不要用普通用户的 timeout 包住内部 sudo，以免无法终止子进程。

参考：[官方同步方案](https://developer.d-robotics.cc/rdk_s_doc/en/Advanced_development/linux_development/driver_development_super/driver_timesync)、[官方 IpcBox 协议与 RunCmd](https://developer.d-robotics.cc/rdk_s_doc/Advanced_development/mcu_development/mcu_ipc)。

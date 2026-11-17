# S100 MCU1 启动与 CANHAL 被动检查

日期：2026-11-16。

## 结果

- PASS：官方 MCU1 ELF 运行时加载成功，`remoteproc_mcu0/state=running`，`remoteproc_mcu1/alive=alive`。
- PASS：MCU1 版本 `GccDebugLiteSIP_V2.0`，构建时间 `Mar 3 2026 15:11:48`；读取到递增的 taskcounter。
- PASS：启动日志包含 `MCU FreeRtos Lite Init Success!`、CAN IRQ affinity 初始化及 `Welcome to D-Robotics MCU lite!`。
- PASS：五路 CANHAL / IPC 通道分别初始化成功，`canInit()=0`。
- PARTIAL：每路被动接收约 100 ms，均无帧，底层返回 `-11`（EAGAIN），CANHAL 映射为 `-303`。本次结合日志与超时长度判为未获得数据，不表示已证明物理总线正常；不能把所有 -303 都视为无害超时。
- NOT_VERIFIED：扩展板实际收发、接线、终端电阻、总线速率、经典 CAN / CAN FD 与电机兼容性、CAN ID 字节序在线表现。

## 范围

用户在确认上一轮建议后授权继续核对并启动 MCU1。本次未调用 CAN 发帧接口、未运行原厂 can_send / cantest、未使能电机或松闸，未刷写 MCU_a / MCU_b，不改原机器人项目和 /app/Can 配置。MCU1 留在运行状态，未添加开机自启。

## 固件来源与一致性

- 官方版本目录：`RDKS100-V4.0.5_20260507`，与板端 Linux 构建版本对应。
- [官方预编译固件](https://archive.d-robotics.cc/downloads/os_images/rdk_s100/RDKS100-V4.0.5_20260507/RDK_MCU_SDK/board_support_package/firmwares/mcu1-product.zip)。
- [同版本 MCU 社区源码](https://archive.d-robotics.cc/downloads/os_images/rdk_s100/RDKS100-V4.0.5_20260507/RDK_MCU_SDK/board_support_package/mcu-community.tar.gz)。
- 本地保存于 `build/vendor/mcu-4.0.5/`，解包使用 debug 目录下 `S100_MCU_DEBUG.elf`。
- ARM32 ELF，大小 9,932,880 bytes。
- SHA256：`500e6d1b619d0e4393916b4a9b019df08fa26da191668147d2af7681de0ff5de`，本机和板端校验一致。
- 板端文件：`/lib/firmware/S100_MCU_DEBUG_4.0.5_20260507.elf`。
- 核对同版本源码的 SysCore 启动任务、CanGw_Swc_Init 以及周期任务，启动路径为控制器/IPC 初始化；未执行 shell cantest。

## 实际启动操作

只在确认 MCU1 原为 dead、remoteproc_mcu0 原为 offline 后执行一次，方式遵循[官方 MCU 启动文档](https://developer.d-robotics.cc/rdk_s_doc/en/Advanced_development/mcu_development/basic_information)：

```bash
echo S100_MCU_DEBUG_4.0.5_20260507.elf | sudo tee /sys/class/remoteproc/remoteproc_mcu0/firmware
echo start | sudo tee /sys/class/remoteproc/remoteproc_mcu0/state
```

这是通过 MCU0 管理 MCU1 的运行时启动流程，不是重启 MCU0。启动后 `remoteproc_mcu1/state` 仍显示 offline，不能单独用此字段判失败；实际管理节点为 remoteproc_mcu0，结合 MCU1 alive、版本和 taskcounter 验证。

## CANHAL 被动检查

独立临时目录 `/tmp/s100-passive-probe.hRaG3R`，每通道一个 JSON 配置目录，未修改厂商样例。
探针只有 `canInit → canRecvMsgFrame → canDeInit`；不包含发送接口。每个进程独占当前 IPC 通道，退出后释放。

| 物理通道 | IPC instance / channel | canInit | 接收结果 |
| --- | --- | --- | --- |
| CAN5 / J2 | 0 / 4 | 0 | 无帧，-303 / 底层 -11 |
| CAN6 / J4 | 0 / 6 | 0 | 无帧，-303 / 底层 -11 |
| CAN7 / J6 | 4 / 7 | 0 | 无帧，-303 / 底层 -11 |
| CAN8 / J8 | 4 / 2 | 0 | 无帧，-303 / 底层 -11 |
| CAN9 / J10 | 0 / 3 | 0 | 无帧，-303 / 底层 -11 |

本地探针源文件：`build/vendor/mcu-4.0.5/passive_probe.c`。原始证据：`log/mcu_bringup_20261116/{start,status,passive_probe}.txt`。

## 下一步

确认端子上当前连接的设备。优先在不接电机的情况下，断电接好 CAN5 / CAN6 的 H-H、L-L、GND，并在两个端点分别启用 120Ω，再执行有明确授权的外部互发测试。若已接电机，先核对电机型号、节点 ID、波特率和帧格式，不能直接运行通用发帧示例。当前没有物理发帧测试结果。

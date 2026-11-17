# CAN5 ↔ CAN6 外部互发测试

日期：2026-11-16，板端日志时间约 16:17:40。

## 结论

**PASS：CAN5→CAN6 与 CAN6→CAN5 各发送一帧，对端均正确收到。**

| 方向 | 接收通道 | CAN ID | 长度 | 接收数据 | CANHAL 接收返回 |
| --- | --- | --- | --- | --- | --- |
| CAN5→CAN6 | 6 | 0x131 | 8 | 00 AA AA AA AA AA AA FC | 0 |
| CAN6→CAN5 | 5 | 0x131 | 8 | 00 AA AA AA AA AA AA FC | 0 |

这验证了当前固件、接线和配置下，CAN5 / CAN6 的基本双向通信。每个方向只有一帧，不包含持续负载、丢包率、实时性、波特率测量、电机通信或其余三路物理接口测试。

## 环境与范围

- 用户确认已按前文接好 CAN5 / CAN6，并明确要求执行测试。
- RDK S100 V1P1，4.0.5-Beta，MCU1 `GccDebugLiteSIP_V2.0`，构建 Mar 3 2026 15:11:48。
- 测试前后：`remoteproc_mcu0/state=running`、MCU1 `alive=alive`。
- CAN5 / J2 ↔ CAN6 / J4，接线与终端跳帽由用户手动完成；软件未测量终端电阻阻值。
- 同版本 MCU 源码中 CAN5 / CAN6 配置为 `FLEXCAN_NORMAL_MODE`，未启用软件内部回环。
- CAN5 IPC=0/4，CAN6 IPC=0/6；配置均位于本次独立临时目录，不改 `/app/Can` 配置。
- 官方 `canhal_send` 发送固定样例帧，未发送 ENCOS 运动、松闸或设零指令。
- 测试结束后本次接收、发送和同步进程均退出；MCU1 保持运行。

## 测试方法

发送端使用先前按 README 编译的官方程序：`/tmp/s100-quickstart-compile.RdgBWX/canhal_send`。
接收端使用有超时的独立诊断探针，初始化 pack / frame 缓冲区，检查 CANHAL 返回码、物理通道、帧类型、ID、长度与所有数据字节；不会因发送端打印成功就判通过。

每个方向：先打开对端 IPC 并等待 READY，发送端执行 `canhal_send can5 5` 或 `canhal_send can6 6`，接收端匹配完整帧后退出。两个方向顺序执行，未同时占用相同 IPC 通道。

原始接收记录：

```text
RECV rc=0 frames=1
FRAME channel=6 type=0 id=0x00000131 len=8 data=00 AA AA AA AA AA AA FC
RESULT target=can6 PASS

RECV rc=0 frames=1
FRAME channel=5 type=0 id=0x00000131 len=8 data=00 AA AA AA AA AA AA FC
RESULT target=can5 PASS
```

物理测试只覆盖官方 CANHAL 路径；新项目 C++ Transport 包装层未在本次测试中执行，不把此结果当作其完整集成验收。

## 时间同步问题（互发测试时的历史状态）

后续已完成运行时修复及65秒验证，见[同步修复报告](TIMESYNC_FIX_20261116.md)。以下保留互发测试当时的原始失败记录。

**FAIL：板端 timesync_sample 在此次互发测试阶段未完成同步。**

已复制其 IPC JSON 到独立工作目录，执行 `timesync_sample -p 0 -r -P 0 -s 0`，程序在发送同步 IPC 数据时输出：

```text
Timesync Info: timesync ipc send data failed
```

该进程随后退出。未修改内核、MCU 固件、Linux 时间同步服务或 CANHAL 延迟检查。之后在无同步进程的情况下进行上述两帧测试，CANHAL 返回均为 0，未出现 -408。此结果不证明 MCU RTC / PHC 已正确同步；同步问题留待后续处理。

## 证据

- 本地：`log/can_pair_20261116/rx_can6.log`、`rx_can5.log`、`tx_can5.log`、`tx_can6.log`、`timesync.log`、`results.json`。
- 板端独立测试目录：`/tmp/s100-pair-test.2vFRtf`，含 probe、配置与原始日志。
- 本地探针源：`build/can_pair_20261116/pair_rx.c`；不是生产节点。
- 初次外层汇总脚本在两个方向已通过后遇到残留 `sync.wait()` 的 NameError；硬件接收进程均正常退出 0。随后修正本地脚本，并离线从两份原始接收日志重新校验得到 `results.json`，未为修复汇总而重复发帧。

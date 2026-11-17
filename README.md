# S100 ENCOS 控制基础项目

独立的 ROS 2 / C++17 工作空间。参考 `ieTDZK-T001` 的硬件与 bringup 分层，先建立 S100 原生 CAN 通信与 ENCOS 协议基础，不依赖或修改原项目。

## Quick Start：手动启动与 CAN5 ↔ CAN6 互测

适用：RDK S100 V1P1、官方 MCU 扩展板、系统 4.0.5-Beta / 4.0.5_20260507。除 SSH 登录外，本节命令均在板端 `sunrise` 终端执行。这里测试两个 CAN 接口之间的物理链路，不需要 ROS 2，也不连接电机。

截至 2026-11-16：MCU1 启动及五路 CANHAL 初始化 **PASS**；CAN5→CAN6、CAN6→CAN5 各一帧的物理互发 **PASS**。时间同步 IPC 故障已有运行时修复，65秒运行（62次成功、2次无效快照拦截恢复、0次发送错误）与 Acore 进程重启验证 **PASS**，见[同步修复记录](ws/docs/TIMESYNC_FIX_20261116.md)。互发结果见[互发测试记录](ws/docs/CAN_PAIR_TEST_20261116.md)；发送端 API 成功不等于接收端收到。

### 1. 准备硬件并断电接线

准备短连接线、两个 2-pin 跳帽，H/L 优先使用双绞线。正常关闭 S100 并断开 DC 电源后接线，测试总线上不要接电机或其他节点。通过扩展板背面信号丝印识别端子，不按拿板方向猜左右顺序。

```text
同一块 MCU 扩展板

CAN5：J2                       CAN6：J4
  CAN_H ──────────────────────── CAN_H
  CAN_L ──────────────────────── CAN_L
  GND   ──────────────────────── GND  （参考地连接）

  J3 插跳帽                     J5 插跳帽
  接入一个 120Ω                 接入一个 120Ω
```

H 接 H、L 接 L，不要把 H 和 L 短接，也不要交叉连接。此次两个端点各一个 120Ω，不另加第三个终端电阻。若有万用表，在断电、总线无其他节点时量 H-L，两个 120Ω 并联应约为 60Ω；不要在通电状态使用电阻档。

主板与扩展板用 FPC 连接：MAIN → 主板 J23，SUB → 扩展板 J1。接线完成后通电，CONNECT 绿灯常亮表示连接及 5V 供电状态正常，但不能据此证明 CAN 已能收发。[官方接口与终端电阻说明](https://developer.d-robotics.cc/rdk_s_doc/Quick_start/hardware_introduction/rdk_s100/rdk_s100_mcu_port_expansion_board)

**实际电机只有 H/L 时，GND 怎么办？**

H/L 承载差分数据，GND 用来提供参考电位。差分接收能够抑制一定的共同噪声，但两端地电位差仍须处于收发器允许的共模范围。只有两根 CAN 信号线，不足以判断接口是否隔离。[收发器厂商对地电位差与共模噪声的说明](https://www.ti.com/document-viewer/lit/html/SSZTD47)

- 电机 CAN 参考地可能与电源负极相连，参考路径已通过电源系统建立，也可能采用隔离接口；以该型号手册为准。
- 不要把 H/L 中任何一根接到 GND；120Ω 终端电阻接在 H-L 之间，不是接地电阻。
- CAN 参考地线不承担电机供电回流，不能替代电机的电源负极线；隔离接口的总线侧参考地与逻辑侧地也不能随意短接。
- 上图是同板 CAN5/CAN6 的测试接法，不要求实际电机一定另有第三个 CAN 引脚。接电机前确认其电源及隔离方式，再决定参考地连接。

### 2. 登录并检查 MCU1

在电脑终端连接板子，把占位 IP 替换为实际地址：

```bash
ssh sunrise@S100_IP  # 将 S100_IP 替换为板端地址
```

板端检查：

```bash
cat /etc/version
cat /sys/class/remoteproc/remoteproc_mcu0/state
cat /sys/class/remoteproc/remoteproc_mcu1/alive
cat /sys/class/remoteproc/remoteproc_mcu1/mcu_version
```

如果管理节点是 `running` 且 MCU1 是 `alive`，跳过启动，进入第 3 步。若为 `offline` / `dead`，先确认已安装的固件校验正确：

```bash
echo '500e6d1b619d0e4393916b4a9b019df08fa26da191668147d2af7681de0ff5de  /lib/firmware/S100_MCU_DEBUG_4.0.5_20260507.elf' | sha256sum -c -
```

只有校验输出 `OK`，且当前确为 `offline` / `dead` 时，执行一次：

```bash
echo S100_MCU_DEBUG_4.0.5_20260507.elf | sudo tee /sys/class/remoteproc/remoteproc_mcu0/firmware
echo start | sudo tee /sys/class/remoteproc/remoteproc_mcu0/state
sleep 2
cat /sys/class/remoteproc/remoteproc_mcu0/state
cat /sys/class/remoteproc/remoteproc_mcu1/alive
cat /sys/class/remoteproc/remoteproc_mcu1/mcu_version
```

预期 `running`、`alive`，版本 `GccDebugLiteSIP_V2.0`。管理 MCU1 启停的节点是 `remoteproc_mcu0`；本次实测 `remoteproc_mcu1/state` 仍为 offline，不单独用它判失败。固件缺失或校验失败时，按[固件来源与启动记录](ws/docs/MCU_BRINGUP_20261116.md)准备匹配版本的 ELF，再操作；不要直接换用其他版本。关机重启后需要重新检查并按需启动，当前未设置自启。

### 3. 编译官方例程并准备独立配置

只在自己的目录生成文件，不修改 `/app/Can` 的源码或配置。本步骤编译不发帧：

```bash
mkdir -p ~/s100-can-test/bin
gcc -I/usr/hobot/include/canhal /app/Can/can_send/canhal_send.c \
  -L/usr/hobot/lib -Wl,-rpath,/usr/hobot/lib -lhbcanhal -lpthread \
  -o ~/s100-can-test/bin/canhal_send
gcc -I/usr/hobot/include/canhal /app/Can/can_get/canhal_get.c \
  -L/usr/hobot/lib -Wl,-rpath,/usr/hobot/lib -lhbcanhal -lpthread \
  -o ~/s100-can-test/bin/canhal_get
```

以下代码为 CAN5、CAN6 分别生成配置，重复执行会覆盖自己测试目录中的 JSON：

```bash
python3 - <<'PY'
import json
from pathlib import Path
for physical, ipc_channel in [(5, 4), (6, 6)]:
    name = f'can{physical}'
    directory = Path.home() / 's100-can-test' / name / 'config'
    directory.mkdir(parents=True, exist_ok=True)
    configs = {
        'channels.json': {'io_channels': {'ipcf': './config/ipcf_channel.json'}},
        'nodes.json': {'nodes': [{
            'id': 0, 'enable': True, 'mode': 'RW', 'target': name,
            'clk_source': '/dev/hrtc0', 'raw_protocol': 'built_1.0',
            'io_channel': {'device_type': 'ipcf', 'channel_id': 0},
        }]},
        'ipcf_channel.json': {
            'enable': True, 'libipcf_path': '/usr/hobot/lib/libhbipcfhal.so.1',
            'channels': [{'id': 0, 'channel': {
                'name': name, 'instance': 0, 'channel': ipc_channel,
                'fifo_size': 64000, 'fifo_type': 0, 'pkg_size_max': 4096,
                'dev_path': '/dev/ipcdrv', 'dev_name': 'ipcdrv',
                'recv_timeout': 4000,
            }}],
        },
    }
    for filename, config in configs.items():
        (directory / filename).write_text(json.dumps(config, indent=2) + '\n')
    print(name, 'IPC instance=0 channel=', ipc_channel)
PY
```

必须从对应的 `can5` / `can6` 目录运行例程，因为 CANHAL 从当前目录读取 `config/`。同一个 IPC 通道不要同时由其他程序占用。[官方 CAN 映射与例程说明](https://developer.d-robotics.cc/rdk_s_doc/Advanced_development/mcu_development/mcu_can)

### ENCOS 电机 ID 查询与设置

已确认 CAN5（J2）上的单台电机当前 ID 为 **1**。手动脚本支持先查询再设置，默认仅预览报文，详见 [ID 操作说明](ws/docs/ENCOS_ID_QUICKSTART.md)。板端执行：

```bash
cd ~/s100-encos-control
bash ws/tools/field/encos_id.sh query --hardware
bash ws/tools/field/encos_id.sh set --new 1 --hardware
```

当前 ID 已为1时跳过写入；首次安装或更新后先执行 `bash ws/tools/field/encos_id.sh --prepare`。本次仅验证了查询及相同ID跳过写入，尚未实测更换ID或掉电保存。

EC-A6408-P2-25 已完成0.5 N·m短时及5秒请求；随后1 N·m、目标5秒的请求在约20.8rpm时触发检查并提前撤去力矩。最终复查电流0A、转速接近零。目前已按用户要求取消脚本的人为速度、力矩及时长上限；该修改后尚未重新运动。工具命令、验证范围和收尾修复见 [力矩测试说明](ws/docs/ENCOS_TORQUE_JOG.md)。

单电机零力矩逐档测试已完成：50～2000Hz、每档5秒，最终连续测试19,500帧提交与电机回复数量相等，未发现回复缺口。仅覆盖3字节直接力矩帧；周期抖动和验证边界见 [频率测试报告](ws/docs/ENCOS_RATE_TEST_20261117.md)。人工测试入口为 `ws/tools/field/encos_rate.sh`，须在S100运行。 新版默认使用CPU5/FIFO40实时配置；30秒验证的250/1000/2000Hz最长主机提交间隔分别约4.010/1.005/0.505ms，97,500次提交与回复匹配。配置、对照数据和验证范围见 [时序优化报告](ws/docs/ENCOS_TIMING_TUNING_20261117.md)。

### 4. 时间同步（已修复 IPC 发送失败）

S100 本版固件默认不启动 MCU 时间同步服务。只运行 Linux 的 `timesync_sample`，会出现 `instance 10 no ready`，用户态返回 -14。这里通过 IpcBox 启用 MCU 服务，无需刷写固件或连接串口。

已部署工具在板端 `~/s100-encos-control/ws/tools/field/`。首次准备或更新工具后，在板端执行：

```bash
cd ~/s100-encos-control
bash ws/tools/field/timesync.sh --prepare
```

这一步复制板上 SDK 示例及 JSON 到项目的 `build/`，应用快照检查补丁并编译，不初始化 MCU、不发同步数据；不改 `/app` 原文件。

**只在 MCU1 刚启动、尚未初始化同步服务时执行一次：**

```bash
bash ws/tools/field/timesync.sh --init-mcu
```

它通过 IpcBox instance7/channel0 提交 `TimeSyncCtrl 4 10`、`TimeSyncCtrl 6`。**同一次 MCU1 启动内不要重复执行**：此固件重复初始化会清除 IPC ready 状态，但内部一次性标志阻止重新打开通道。当前会话已初始化过，直接进入下面的 `--run`；只重启 Linux 同步进程，也不需要再执行 `--init-mcu`。

另开一个 SSH 终端持续运行：

```bash
cd ~/s100-encos-control
bash ws/tools/field/timesync.sh --run
```

实际参数是 `-p 0 -r -P 4 -s 0 -l 15`：PHC0 → MCU RTC，MCU 使用 PPS10、Linux 使用 PPS 索引4，均对应独立的 MCU Ethernet PPS。不能把 Linux 的 `-P` 索引直接换成 MCU 的编号10。`-s 0` 禁用系统时钟同步选择，不改 NTP/gPTP 服务。

预期持续出现 `timesync ipc send data succeed`。如需确认 MCU 侧实际接收和校时结果，在另一个终端执行：

```bash
bash ws/tools/field/timesync.sh --debug-on
sudo timeout 3s cat /proc/remoteproc_mcu1
bash ws/tools/field/timesync.sh --debug-off
```

MCU 日志应出现 `Get Time From Acore success`、`TimeKeeperRTC and TimeKeeperIPC Success`。最终65秒验证中，成功发送62次，2次无效快照被丢弃并重新配置捕获，发送错误为0；跳过前两次有效快照后的绝对偏差范围约37.8µs～1.923ms，不代表长期精度保证。Ctrl+C 停止自己启动的 Linux 同步进程；之后仅再次执行 `--run` 即可。当前未设置开机自启。

若误重复初始化，出现 -14 / `instance 10 no ready`，不要继续重复发送初始化命令或绕过校验。需先停止同步程序，按[修复报告的 MCU1 恢复步骤](ws/docs/TIMESYNC_FIX_20261116.md)确认 MCU1 已停稳，再重新启动和初始化一次。工具保留发送错误码，并拒绝把零值或非法 PHC 快照发给 MCU。

参考：[官方 MCU 同步方案](https://developer.d-robotics.cc/rdk_s_doc/Advanced_development/linux_development/driver_development_super/driver_timesync)与[官方 IpcBox 说明](https://developer.d-robotics.cc/rdk_s_doc/Advanced_development/mcu_development/mcu_ipc)。

### 5. 正向测试：CAN5 发 → CAN6 收

确认总线只有 J2、J4 两个节点。终端 A 先接收：

```bash
cd ~/s100-can-test/can6
sudo stdbuf -oL ../bin/canhal_get can6
```

终端 B 再发送一帧：

```bash
cd ~/s100-can-test/can5
sudo ../bin/canhal_send can5 5
```

`can5` 是配置中的 target，最后的 `5` 是物理 CAN5，**不是 IPC channel，也不是电机 ID**。板上这个官方发送例程固定发 CAN ID `0x131`，8 字节数据 `00 AA AA AA AA AA AA FC`。

验收看终端 A：应收到 ID `0x00000131` 和上述完整数据。发送端 `Send end` 只是例程打印，即使发送函数报错也可能打印；不能把它当作验收结果。接收例程无数据超时也会打印信息，应以实际帧 ID 与字节为准。

### 6. 反向测试：CAN6 发 → CAN5 收

先在终端 A 按 Ctrl+C 停止 CAN6 接收，确认该进程退出。然后终端 A 改为：

```bash
cd ~/s100-can-test/can5
sudo stdbuf -oL ../bin/canhal_get can5
```

终端 B 改为：

```bash
cd ~/s100-can-test/can6
sudo ../bin/canhal_send can6 6
```

CAN5 收到相同 ID 和数据，才表示反向也通过。完成后 Ctrl+C 停止接收和自己启动的同步程序；MCU1 可以保持运行。需要重新接线时，先正常关机并断开电源。

### 7. 没收到帧时

| 现象 | 先检查 |
| --- | --- |
| MCU1 dead / 管理节点不为 running | 固件文件、校验结果、启动日志；不要连续重复 start |
| canInit 失败 | 是否在正确工作目录、JSON target 与 IPC 配置、权限、通道是否被占用 |
| -303，底层 -11 | 本次被动检查表现为等待无数据；还需排查对端发送、接线和 MCU，不能直接判硬件好坏 |
| -14 / instance 10 no ready（同步程序） | MCU 同步服务是否已初始化；不要重复初始化，见第4步 |
| -408 / Exceed max delay | MCU RTC 与 PHC 时间同步及系统负载，不先绕过延迟检查 |
| 发送端无报错，接收端无有效帧 | H/L 是否接反、J3/J5 跳帽、两个控制器的速率/帧格式、MCU 日志 |

查看日志：`sudo timeout 3s cat /proc/remoteproc_mcu1` 与 `sudo dmesg | tail -50`。timeout 到期退出是正常的。不要用电机是否转动判断互发是否成功；本节不发送 ENCOS 控制指令，通用测试帧也不得发到已接电机的总线上。

## 项目结构

```text
s100-encos-control/
├── AGENTS.md
├── ws/
│   ├── build_workspace.sh
│   ├── env.sh
│   ├── docs/PROJECT_HANDOFF.md
│   └── src/
│       ├── hardware/
│       │   ├── s100_can/             # 统一帧接口、mock、可选 CANHAL
│       │   ├── encos_motors/         # MIT 打包、型号参数、MIT 反馈解析
│       │   └── s100_robot_hardware/  # ROS 2 节点，mock / 真机被动接收
│       └── bringup/s100_bringup/    # 离线启动、参数、CANHAL 模板
└── build/ install/ log/             # 构建后生成
```

当前链路：`offline.launch.py → s100_hardware_node → ENCOS 协议 → MockTransport`。
CANHAL 后端提供真实收发 API，但当前 ROS 节点仅允许 mock 发送，选择 CANHAL 时只被动接收。没有使能、松闸、MIT 运动命令订阅或自动 MCU 启动。Mock 只记录发帧，不伪造电机反馈，因此离线运行时 `/s100/joint_states` 无数据是正常现象。

## 本机构建与离线运行

```bash
cd ~/s100-encos-control
source /opt/ros/jazzy/setup.bash
bash ws/build_workspace.sh --cmake-args -DBUILD_TESTING=ON
source ws/env.sh
ros2 launch s100_bringup offline.launch.py
```

有限周期烟测：

```bash
ros2 launch s100_bringup offline.launch.py max_cycles:=5
colcon test --base-paths ws/src --event-handlers console_direct+
colcon test-result --verbose
```

ROS 参数默认 CAN5、电机 ID 1、EC-A4310-P2-36；这些是模板值，不代表实际接线与电机型号。`update_rate=250` 是定时器请求频率，没有硬实时保证。命名空间为 `/s100`，节点名为 `s100_hardware`。

## S100 后端

S100 使用 `CANHAL → IPC → MCU CAN`，不依赖 SocketCAN `can0`。在装有 ROS 2 和厂商 SDK 的 S100 上，可单独构建底层库：

```bash
bash ws/build_workspace.sh --packages-up-to encos_motors \
  --cmake-args -DS100_ENABLE_CANHAL=ON -DBUILD_TESTING=ON
```

编译只链接厂商库，不初始化总线。默认构建关闭 CANHAL；本机离线测试不需要厂商 SDK。

CANHAL 头文件 `/usr/hobot/include/canhal/hobot_can_hal.h`，库 `/usr/hobot/lib/libhbcanhal.so`。后端对发送 ID 使用 `htonl()`、接收 ID 按 host order 读取，依据板端 SDK 4.0.5-Beta 官方示例；官方路径的 CAN5/CAN6 双向单帧互发已通过，新项目 C++ 后端完整集成收发仍待验证。

`canhal_config_directory` 应设置为安装后的 `share/s100_bringup/config/canhal` 绝对路径，该目录包含 `config/` 子目录。初始化期间后端临时切换工作目录以适配厂商库，初始化后立即恢复；应在其他业务线程启动前构造，且每进程只支持一个 CANHAL 实例。后端并非实时 API；接收有 JSON 中配置的超时，当前模板 20 ms。

| 物理接口 | IPC instance | IPC channel |
| --- | --- | --- |
| CAN5 / J2 | 0 | 4 |
| CAN6 / J4 | 0 | 6 |
| CAN7 / J6 | 4 | 7 |
| CAN8 / J8 | 4 | 2 |
| CAN9 / J10 | 0 | 3 |

模板仅配置 CAN5。换通道需同步修改 `nodes.json` 的 target、IPC 映射及节点物理通道，电机报文 ID 与 IPC channel 含义不同。波特率由 MCU 配置决定；该 JSON 不配置电机总线速率。

## 已有范围与后续接入点

- `s100_can::Transport`：经典标准 CAN，统一 host-order ID。mock 可注入 RX 用于测试，CANHAL 会按物理通道筛选 RX。
- `encos_motors`：七个型号参数、MIT 命令编码、完整 MIT 反馈解码及抱闸状态查询编码。KD 按原项目为 9 bit；超范围或非有限命令直接拒绝。未实现设零、Flash、清故障或抱闸动作。
- `s100_robot_hardware`：单电机通信骨架，有反馈时发布 `sensor_msgs/JointState`；CANHAL 模式不发帧。
- 下一阶段：MCU1 已完成启动验证，先完成物理互发和总线速率/帧格式核对，再做单电机通信验证；之后添加独立通信线程、多电机配置、ros2_control SystemInterface、controller_manager 与控制器。现阶段未迁移 RL、IMU、URDF 和原机器人的安全系统。

参考：[官方 CAN 文档](https://developer.d-robotics.cc/rdk_s_doc/Advanced_development/mcu_development/mcu_can)、[MCU 启动说明](https://developer.d-robotics.cc/rdk_s_doc/Advanced_development/mcu_development/basic_information)。当前实现依据本机会话中已读取的板端头文件、示例及原项目协议代码。

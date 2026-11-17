# S100 ENCOS 项目交接

更新日期：2026-11-17。独立项目根目录 `/home/tdzk/s100-encos-control`。

## 授权与范围

- 用户要求：遇到 5 小时额度耗尽时保留断点，刷新后继续当前 IPC/同步修复；不把额度中断当作取消。恢复时从本文件读取进度，避免重复执行真机初始化。
- 用户要求新建类似现有机器人项目的基础框架，并明确不修改现有项目。
- 原项目 `/home/tdzk/ieTDZK-T001` 只读参考；该项目已有未提交变更，不属于本任务。
- 初始搭建与只读检查未启动 MCU。2026-11-16 用户授权继续核对并启动 MCU1；已执行运行时加载与 CANHAL 被动检查，未调用物理 CAN 发帧接口、未使能或驱动电机、未刷写 Flash。
- 随后用户确认已连接 CAN5/CAN6 并授权互发测试；本轮各方向发送一帧官方 0x131 示例，未发送电机指令。

## 架构

- ROS 2 / ament 工作空间，源码 `ws/src`，产物在根目录 build/install/log。
- `s100_can`：C++17 Transport / Frame、MockTransport、可选 CANHAL 后端。
- `encos_motors`：七种 ENCOS 型号参数，MIT 编码 / 反馈解码、抱闸状态查询帧。
- `s100_robot_hardware`：单电机 ROS 节点，默认 mock，CANHAL 模式只有被动 RX。
- `s100_bringup`：固定 mock 的离线启动入口及 CAN5 JSON 配置。
- 非完整 ros2_control 项目；控制器、URDF、IMU、RL、多电机、通信工作线程待后续需求落实。
- ROS节点不提供硬件运动命令入口；独立field工具提供显式授权的短时力矩调试。默认250Hz仅为请求频率，硬实时NOT_VERIFIED。
- CANHAL 发送返回仅表示 API 接受，不能当作总线上线或电机执行证据。
- CANHAL 进程级单实例；SDK 通过 CWD 找 config，构造应在业务线程启动前；RX 可能等待配置超时。

## 板端只读检查结果（MCU1 启动前）

- 系统 `4.0.5-Beta`，aarch64 / PREEMPT_RT。
- `/app/Can/{can_send,can_get,can_multi_ch}` 与 `/usr/hobot/include/canhal` 存在。
- `/usr/hobot/lib/libhbcanhal.so` 和 `/dev/ipcdrv` 存在；设备节点仅 root 可读写。
- MCU1 alive=dead；remoteproc_mcu0 state=offline。
- 在已检查目录内未找到 S100_MCU_DEBUG.elf；不能据此断定板端没有 MCU 固件，需核对启动来源及版本。
- 原厂 can_send IPC 模板当前指向 CAN7（instance=4/channel=7）。本项目模板用 CAN5（0/4）。
- 实际电机接线通道、型号、总线速率尚未确认；当前配置是占位模板。
- 本轮仅将通信层源码放入板端临时目录编译，不安装、不运行硬件程序。

### 2026-11-16 SSH 只读复核

- 通过 sunrise 登录；设备树 model=`D-Robotics RDK S100 V1P1`，系统仍为 4.0.5-Beta。
- Linux 无 can0/can1 网络接口；不能将扩展板的五个物理端子直接当作 Linux can0..can4。
- MCU1 alive=dead、state=offline，firmware 字段为 `rproc-soc:remoteproc_mcu1-fw`；无有效 MCU1 版本/构建时间。
- MCU0 mcu_version 可读：`GccReleaseLiteSIP_V2.0`，构建时间 Feb 11 2026 15:42:54。
- `/proc/mtd` 存在 MCU_a / MCU_b 分区；未检查其内容，不据此断言固件完整或可启动。
- dmesg 显示 IPC HAL probe success、instance 0/4 注册成功；CANHAL 头文件、动态库和 /dev/ipcdrv 存在。
- 多通道样例 JSON 与官方映射一致：CAN5=0/4、CAN6=0/6、CAN7=4/7、CAN8=4/2、CAN9=0/3。
- 官方物理端子：J2=CAN5、J4=CAN6、J6=CAN7、J8=CAN8、J10=CAN9；对应终端跳帽 J3/J5/J7/J9/J11。
- PARTIAL：板端软件基础可确认，MCU1 未运行；SSH 只读检查不能证明 FPC/收发器/接线电气完整，未执行物理 CAN 测试、MCU 启动或发帧。

### 2026-11-16 经授权启动状态

- PASS：取得官方同版本 4.0.5_20260507 预编译 MCU1 ELF，校验后安装至 `/lib/firmware/S100_MCU_DEBUG_4.0.5_20260507.elf`，经 remoteproc_mcu0 加载启动，未改 Flash 或开机自启。
- 管理节点 `remoteproc_mcu0/state=running`；MCU1 `alive=alive`、版本 `GccDebugLiteSIP_V2.0`，构建 Mar 3 2026 15:11:48，taskcounter 可读。`remoteproc_mcu1/state` 本身仍为 offline，应结合实际管理节点和心跳判断。
- PASS：CAN5~CAN9 各自 CANHAL / IPC 初始化成功。被动接收无帧，约 100ms 超时返回 -303，底层 -11；只证明 API 可初始化，不证明物理收发。
- 已保留 MCU1 运行，未调用 CAN TX 接口。物理接线、经典 CAN / CAN FD 配置及电机参数待确认，物理发帧测试 NOT_VERIFIED。
- 固件来源、SHA256、日志与操作记录见 `ws/docs/MCU_BRINGUP_20261116.md`；原始日志 `log/mcu_bringup_20261116/`。

### 2026-11-16 最新：CAN5/CAN6 物理互发

- PASS：CAN5→CAN6 与 CAN6→CAN5 各一帧；对端均 `canRecvMsgFrame=0`，ID=0x131、type=0、len=8、数据 `00 AA AA AA AA AA AA FC`，接收通道分别 6 / 5。
- 范围：官方 CANHAL 发送例程 + 独立有界接收探针；不覆盖新项目 Transport 集成、持续负载、时延、其他三路或电机。
- 历史 FAIL（后续已修复，见下方最新断点）：timesync_sample `-p 0 -r -P 0 -s 0` 发送同步 IPC 数据失败并退出；后续无同步进程的单帧互发仍成功，未绕过 CANHAL 检查。RTC/PHC 同步未验收。
- 运行状态：MCU1 alive，管理节点 running；本次诊断进程均退出。未刷 Flash、未改厂商配置或原项目。
- 证据：`ws/docs/CAN_PAIR_TEST_20261116.md`、`log/can_pair_20261116/`；README 已更新验证范围及同步命令缺失的 JSON 复制步骤。

## 验证

- PASS：本机 ROS 2 Jazzy / GCC 13，四包 colcon 构建；节点修正日志参数类型后重建，无编译警告。
- PASS：`colcon test-result --verbose`：2 tests，0 errors / failures / skipped。覆盖 MIT 固定字节向量、9-bit KD 上限、反馈端点、非法输入、mock TX/RX 与不伪造反馈。
- PASS：`offline.launch.py max_cycles:=5`，domain 173 / localhost，打印 backend=mock，完成五周期并正常退出。日志：`log/offline_smoke/2026-11-12-21-20-07-774819-woker-2146185/launch.log`。
- PASS：S100 上 GCC / C++17 将 transport.cpp 与 canhal_transport.cpp 编译为 ARM64 共享库并链接 libhbcanhal；位置 `/tmp/s100-encos-build.y469jn/libs100_can.so`。未执行库或 canInit；不等于已验证物理通信。
- PASS：2026-11-16 MCU1 启动、五路 CANHAL 初始化、官方路径 CAN5/CAN6 双向各单帧；NOT_VERIFIED：其他三路物理收发、新项目 C++ 后端完整集成、总线速率测量、电机执行和实时性；PASS：时间同步运行时修复后65秒运行62次发送成功、2次无效快照恢复、0次发送错误；不构成长期精度保证。
- 原项目 Git 状态与检查前一致：已有交接文件修改及一份未跟踪历史报告；本轮未写入原项目。
- 手动操作入口：根目录 `README.md` 的 Quick Start，包含 MCU 启动、J2/J4 接线、J3/J5 终端跳帽、独立配置与双向验收；另说明只有 H/L 的电机与 CAN 参考地关系。互发及同步短时修复验证已通过；范围与限制见独立报告。

## 最新：ENCOS ID 工具及单电机查询（2026-11-16）

- 用户明确要求CAN5唯一电机设置ID1，并要求可手动使用的设置脚本；随后用户对调H/L线序。授权覆盖ID查询及设置，不覆盖电机运动、使能或设零。
- 已新增 `ws/tools/field/encos_id{.sh,.cpp,_protocol.hpp}` 与 `test/test_encos_id.cpp`，板端已部署源码、CAN5配置并编译。默认离线预览，`--hardware` 才初始化CANHAL和发帧。固定CAN5、先查询、单次修改、等待ACK后复查，配置命令之后至少等待2秒才发下一条。
- 初次无应答，未发送修改指令；对调线序后 `set --new 1 --hardware` 收到0x7FF/5字节 `FF FF 01 00 01`，确认现有ID=1，自动跳过配置写入。目标已满足，无电机运动命令。
- 验证：离线协议测试、参数检查及板端构建PASS；真实查询和同ID跳过写入PASS。其他ID修改/ACK流程及掉电保存未真机验证。CANHAL返回37不是独立总线证据，电机实际应答才是本次依据。
- 文档 `ws/docs/ENCOS_ID_QUICKSTART.md`；日志 `log/encos_id/set_id1_after_rewire.txt`。程序已正常退出，无新增后台进程。原项目未修改。

## 最新：EC-A6408-P2-25 的0.5 N·m短时测试完成

- 用户授权约0.5 N·m运动，并指定 `EC_A6408_P2_25`、不需要松闸/锁闸。本次按直接力矩模式执行；未发送任何抱闸指令。用户原有未修改项目仍保持独立。
- 工具 `ws/tools/field/encos_jog.cpp` / `.sh` 已部署板端；默认离线。显式 `--model EC-A6408-P2-25 --hardware` 跳过抱闸操作，CAN5/ID1固定+0.5 N·m、目标0.2秒；`--zero-only` 仅发零力矩。未设后台自启。
- 正力矩实测9帧、软件窗口215ms，收到控制反馈，最高约0.4103rpm，只有轻微运动，故障码0。驱动实际转矩系数2.09 N·m/A、超时500ms；未改配置、不等同于标称2.35或实测输出扭矩。
- 原收尾3次零力矩后延迟60ms接收，触发CANHAL -408；已独立查询确认接近静止0.0012rpm。修复为每帧零力矩后及时接收；保留延迟检查，只做零力矩复验，收到3帧电流0A反馈，最终转速约-0.0001rpm，无故障，退出0。没有重复正力矩测试。
- 当前本次调试进程已退出，无持续力矩请求；未使能、松闸/锁闸、设零、重启MCU或改Flash，未重复初始化时间同步。此工具为单电机人工调试入口，不改变ROS节点mock默认。
- 证据和手动步骤：`ws/docs/ENCOS_TORQUE_JOG.md`；日志 `log/encos_id/jog_6408_05Nm.txt`、`jog_6408_after_status.txt`、`jog_6408_zero_verify.txt`。后续运动参数变更按新用户指令处理，不自动增加力矩/时长。

## 最新：5秒请求与1 N·m测试（已结束）

- 用户先授权0.5 N·m时长5秒：已完成207帧正力矩、窗口5011ms，转速范围-0.0176～0.0621rpm，故障0；随后零力矩反馈0A、独立速度查询0.0016rpm，正常退出。
- 用户随后要求1 N·m，沿用5秒目标：已发送23帧正力矩，反馈速度达到20.8343rpm，触发原有20rpm检查并提前撤去力矩，未跑满5秒。故障码0，退出1为主动终止。
- 只追加零力矩复查（没有再发正力矩）：3帧反馈均0A，最终独立查询0.0007rpm，故障0，退出0。当前无本次进程后台运行，无持续力矩请求。
- 新CLI `--seconds` 范围(0,5]、默认0.2秒；`--torque` 范围(0,1] N·m、默认0.5。单轮必须得到新鲜有效控制反馈；保留20rpm、超时及故障检查。型号路径不发抱闸指令。
- 命令示例 `bash ws/tools/field/encos_jog.sh --model EC-A6408-P2-25 --seconds 5 --torque 1 --hardware`：会产生新运动请求，仍可能提前停止；不得因恢复会话自动重复或放宽速度限制。
- 日志 `log/encos_id/jog_6408_05Nm_5s.txt`、`jog_6408_1Nm_5s.txt`、`jog_6408_1Nm_after_zero.txt`。说明及板端部署均已更新，原机器人项目未修改。

## 最新：用户要求取消桌面调试脚本人为限制（已部署、未再运动）

- 新指令明确要求取消限制。已移除 `encos_jog.cpp` 运行20rpm、启动1rpm阈值，1 N·m/5秒上限，以及转矩系数/500ms超时的软件启动门槛。以上优先于前文历史检查描述；不要恢复这些阈值。
- `--torque` 只保留ENCOS协议可编码范围-327.68～327.67 N·m和有限数字校验；`--seconds` 为任意正有限值。默认仍0.5 N·m/0.2秒，用户上次目标1 N·m/5秒需显式传参。
- 保留真实故障/通信错误处理、有效反馈要求、结束归零与Ctrl+C。未改电机内部保护、CANHAL延迟检查、原项目或MCU固件。
- 本轮只修改/部署/构建及离线预览（含2 N·m/10秒和协议端点），未初始化CANHAL或新增物理帧，未自动重复1 N·m测试。取消阈值后的运动结果NOT_VERIFIED。
- 文档 `ws/docs/ENCOS_TORQUE_JOG.md` 已更新；板端 `/home/sunrise/s100-encos-control` 已同步新版源码/可执行文件。

## 最新：单电机零力矩逐档频率测试（2026-11-17，已结束）

- 用户要求逐渐提高控制频率检查丢包。本次用CAN5/ID1零力矩控制帧 `67 00 00`、请求8字节类型3反馈，未施加非零力矩或操作抱闸。
- 新工具 `ws/tools/field/encos_rate.cpp` / `.sh` 已部署板端并编译；默认离线，`--hardware` 真机；`--sweep --seconds 5` 同一CANHAL进程顺序测50/100/250/500/1000/2000Hz，每档5秒，专用配置recv_timeout=0、单线程交替收发。不会改encos_jog已取消的限制。
- 最终连续测试PASS：接受/回复分别250、500、1250、2500、5000、10000，合计19,500对；回复缺口、接口错误、调度跳过时隙、故障帧均0，反馈相电流均0A。
- MCU侧CAN5 TX与RX均增加19,500，无溢出/发送忙/PDU错误增量。初版分进程6档RollCntErr增加6，同进程连续复测仅初始化边界增加1，支持进程重建序号复位解释；不将它直接当作丢帧。
- 主机提交间隔：250Hz P99约4.061ms、最大4.080ms；2000Hz P99约0.560ms、最大0.565ms。这不是总线上线时间或电机执行周期；未实测端到端逐帧时延，未验收硬实时。
- 范围限制：单电机、3字节直接力矩帧，不覆盖8字节MIT帧、多电机/多总线、推理感知压力或长期稳定性；无协议命令序号，数量匹配只支持本窗口没有回复缺口，不能排除重复/丢失抵消。
- SDK非阻塞空读仍产生-11日志，程序对应-303计为空读；日志开销包含在本次环境。测试临时Linux时间同步程序已退出，MCU1仍alive；未重新初始化MCU同步服务，未改原项目或固件。
- 报告 `ws/docs/ENCOS_RATE_TEST_20261117.md`；原始日志与JSON `log/encos_rate/continuous.txt`、`continuous_results.json`、`mcu_continuous_{before,after}.txt`、`summary.json`。初版分进程日志另存同目录，不覆盖历史数据。

## 最新：通信时序优化与测试完成（2026-11-17）

- 用户授权优化并测试。只改独立 `ws/tools/field/encos_rate.cpp` / `.sh`，默认离线。新增profile baseline/tuned/rt（默认rt），rt绑CPU5/FIFO40，SDK日志变量CAN_HAL_DEBUG_LEVEL=5，最后20us非阻塞轮询；统计数组提前分配触页。
- 普通线程继承timer slack50000ns；tuned请求1ns；RT下不应用slack，CONFIG读0。设置调度/绑核失败会退出。邮箱IRQ线程FIFO50高于测试线程40；未改IRQ、CPU隔离/频率或全局RT配置。
- 5秒同场景对照PASS：rt的250/1000/2000Hz最长主机提交间隔4.002085/1.002528/0.512713ms，无回复缺口/漏时隙。tuned2000Hz存在3个调度漏时隙但接受/回复均9997，不是链路丢包。SDK日志从约4MB/5s降至约800B。
- 随后rt每档30秒验证PASS：250Hz 7500对、1000Hz 30000对、2000Hz 60000对，合计97,500对；接口错误、故障帧、回复缺口、漏时隙均0，电流0A。
- 30秒验证间隔P99/最大：250Hz 4.001437/4.010337ms；1000Hz 1.002028/1.004853ms；2000Hz 0.502014/0.504664ms。这是主机提交间隔，不是总线上线或电机执行周期，不宣称硬实时。
- MCU侧CAN5 TX/RX均增加97,500，溢出/忙/PDU错误无增量；RollCntErr增加3对应三次独立进程初始化边界。临时180秒Linux同步进程已结束，MCU1仍alive，无后台测试发帧。
- 不改原项目、不施加非零力矩、不改电机/MCU参数、不重复同步初始化。仍单通信线程独占CANHAL；整机控制线程/最新命令缓存集成、多电机/MIT帧/负载压力未覆盖。
- 报告 `ws/docs/ENCOS_TIMING_TUNING_20261117.md`；日志 `log/encos_timing/` 的comparison.json、validation.json、各档原始日志与MCU前后计数。已部署板端源码和可执行文件。

## Git 交接（2026-11-17）

- 独立项目已初始化Git，分支main，origin为 `https://github.com/yujiuyeguo/s100-encos-control.git`，用户明确授权推送。
- 版本内容包含基础框架、现场工具、操作/验证文档和用户提供的ENCOS手册；build/install/log及固件下载产物由.gitignore排除。原始测试日志仍保留本机，不随提交上传。
- 本次仅版本整理，不重新运行硬件测试；既有验证范围以各报告为准，未修改原机器人项目。

## 后续

### 当前任务断点（2026-11-16：IPC / 时间同步运行时修复已完成）

- 已确认根因：timesync_sample 实际发送 -14；strace 显示 /dev/ipcdrv write 返回 EAGAIN；dmesg 为 `instance 10 no ready`。MCU TimeSync 服务默认未初始化，与 CAN5/6 的 instance0 无关。
- 已找到无需 UART / 无需刷 Flash 的修复路径：IpcBox instance7/channel0 RunCmd 执行 `TimeSyncCtrl 4 10` 后执行一次 `TimeSyncCtrl 6`，MCU侧 PPS=10 对应 Linux `-P 4`（MCU Ethernet PPS）。
- 重要固件限制：IPC_Init 每次初始化共享区，但 TimeSync_Ipc_Instance3_Open_Func 用静态一次性标志只 OpenInstance 一次；重复执行 TimeSyncCtrl 4/6 会导致通道再次 no ready。不能在同次 MCU 启动内重复初始化。已通过 ELF 符号与反汇编核对。
- 排查期间已 stop MCU1，确认进入 stop / offline / dead 后重启同一个已验证 ELF（未刷 Flash）。最新 MCU1 alive / 管理节点 running。
- 最新验证 PASS：fresh boot + 单次初始化 + `timesync_sample -p 0 -r -P 4 -s 0 -l 15` 连续 12 秒发送成功；MCU 日志确认收到且 RTC/IPC 收敛，暖机后误差绝对值约 43.8～71.3µs。原始日志 `log/timesync_fix_20261116/fresh_boot.txt` 与 `mcu_fresh_boot.txt`。
- 同步诊断进程在 12s 后正常由 timeout 中止，MCU同步服务保留；未运行后台自启服务。未发送任何 CAN 或电机报文。
- PPS=0（RTC自作PPS）在校时跳变后会出现 PHC=0 快照，已在诊断副本增加零值/非法快照拒发；不再使用此配对。
- 本地诊断与源码：`build/timesync_fix_20261116/`，板端 `/tmp/s100-timesync-diagnose.cqLXL8`。该目录有 ipcbox_timesync.cpp / 可执行文件和修改过的 timesync_sample.c（原厂文件未改）。
- 已完成并部署：`ws/tools/field/{timesync.sh,mcu_timesync_control.cpp,timesync_sample.patch}`，板端 `/home/sunrise/s100-encos-control`；`--prepare` 在板端编译通过，不进行硬件I/O。`--run` 在前台同步；`--init-mcu` 仅供每次新MCU1启动后执行一次。
- Acore进程停止重启验证：8秒8次成功，不重新初始化MCU。最终整合发现eth0既有周期断链后捕获配置失效；补丁拒绝零值/非法PHC快照并重新配置捕获，不修改网络配置。
- 最终65秒验证 PASS：62次成功，2次无效快照丢弃后恢复，0次IPC发送错误；跳过前两次有效快照后的绝对偏差37.8µs～1.923ms，非长期精度验收。日志 `log/timesync_fix_20261116/rearm_65s.txt` 与 `final_summary.json`。
- 最终状态：管理节点running、MCU1 alive；调试日志已关闭，本次同步进程已退出，无后台自启。当前直接运行 `bash ws/tools/field/timesync.sh --run`，不要因为恢复上下文重跑 TimeSyncCtrl 4/6。
- README及 `ws/docs/TIMESYNC_FIX_20261116.md` 已更新操作、固件限制和证据。运行时规避方案未修复厂家固件的重复初始化缺陷；未改原项目、厂商/app、Flash，未新增CAN或电机发帧。

根目录 `README.md` 保存手动步骤。编译命令已在板端验证；实际 CAN5/CAN6 互发与时间同步结果以最新测试段落及独立报告为准。

1. CAN5/CAN6基本互发及CAN5电机ID查询已通过，当前ID=1；实际电机型号用户指定为EC-A6408-P2-25，已完成短时直接力矩测试，更多控制参数仍待后续任务。
2. 时间同步IPC运行时修复及一次单电机短时测试已完成；更长时间或更大力矩运动需用户另行指定。
3. 在总线验证基础上扩展多电机与 ros2_control，沿用上层接口而非耦合厂商 API。

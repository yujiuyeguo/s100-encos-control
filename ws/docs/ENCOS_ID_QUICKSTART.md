# ENCOS 电机 ID 查询与设置

依据本目录《ENCOS+调试技术手册+V1.16.pdf》第24页7.3节、第25页8.1节；设置指令间隔依据3.5节。

工具已部署在 S100 的 `/home/sunrise/s100-encos-control`。固定使用 J2 / CAN5、IPC instance0/channel4，经典 CAN 标准数据帧，仲裁 ID 0x7FF。电机手册规定经典 CAN 为1Mbps；JSON不设置MCU波特率。工具不会更改波特率、启动MCU、初始化时间同步、发送运动或使能命令。

## 使用前

- 总线上只接一台待配置电机，独立供电，上电至少5秒。
- H接H、L接L；CAN5为J2，终端跳帽为J3；总线两个末端各120Ω，核对电机端终端电阻。此前CAN5与CAN6互发的连线应拆除。
- MCU1须已运行；不要让其他进程同时占用CAN5的IPC通道。
- 按根README管理时间同步；不要为设置ID而重复执行 `timesync.sh --init-mcu`。

## 板端命令

首次部署或更新源码后编译；此步骤只构建，不初始化CANHAL、不发物理报文：

```bash
cd ~/s100-encos-control
bash ws/tools/field/encos_id.sh --prepare
```

默认是离线预览，以下命令均不接触硬件：

```bash
bash ws/tools/field/encos_id.sh query
bash ws/tools/field/encos_id.sh set --old 2 --new 1
```

实际查询（会发送一帧ID查询报文）：

```bash
bash ws/tools/field/encos_id.sh query --hardware
```

实际设置为ID1：

```bash
bash ws/tools/field/encos_id.sh set --new 1 --hardware
```

也可指定预期旧ID，查询不一致会停止：

```bash
bash ws/tools/field/encos_id.sh set --old 1 --new 2 --hardware
```

ID接受十进制或0x开头十六进制，范围1～2046；0x7FF为设置/查询专用标识符，不用作电机ID。

`set` 会先查询，等待2秒收集有效应答；没有应答或发现多个不同ID时退出，不盲目写入或重置。若当前已是目标ID，直接确认并跳过配置写入。否则发送一次设置帧，等待2秒收集成功ACK（期间不发下一条命令，满足大于500ms间隔），再查询核对新ID；同时满足ACK和新ID才输出修改成功。没有收到ACK但查询显示新ID时仍返回未完全确认，应先人工查询，不盲目重复写入。

输出 `CANHAL submit ret=...` 只说明接口接受；`Motor replied` 和 `PASS` 才依据电机应答判断。SDK的 `-303` / 底层 `-11` 接收超时日志不代表收到电机反馈。工具无广播重置入口；查询无回应时先检查供电、H/L、终端和通信模式。

## 本次实机结果

日期：本机2026-11-16（板端日志2026-11-17，时区不同）。用户确认CAN5只有一台电机，并授权设置为ID1。

- 初次查询无应答，脚本退出，未发送设置帧。
- 用户对调CAN线序后再次执行 `set --new 1 --hardware`，收到：

```text
TX CAN5 id=0x7FF len=4 data=FF FF 00 82
CANHAL submit ret=37 (not motor confirmation)
RX CAN5 id=0x7FF len=5 data=FF FF 01 00 01
Motor replied: ID=1
PASS: requested ID already confirmed; no configuration write needed
```

结论：CAN5已取得电机协议应答，当前电机ID确认是1；无需修改。未实测从其他ID修改到新ID、掉电保存或电机运动，不把查询成功描述为这些功能已验证。

证据：`log/encos_id/set_id1.txt`、`set_id1_after_rewire.txt`、`mcu_status.txt`。协议编码、应答识别和非法ID离线测试已通过，板端编译通过。

源码：`ws/tools/field/encos_id_protocol.hpp`（无ROS/厂商依赖），`encos_id.cpp`（CLI与CANHAL后端），`encos_id.sh`（准备和调用），`test/test_encos_id.cpp`（离线协议检查）。

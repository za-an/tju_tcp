# 第三阶段与挑战任务实现

本阶段在原第二阶段工作区上增加拥塞控制，保留八个课程 API 与 `sendToLayer3()` 接口。默认 `basic` 完成基础 Reno；挑战模式包含完整 Reno 快速恢复、NewReno 和 CUBIC；`TJU_RACK=1` 启用 RACK-TLP 定时检测，`TJU_CHECKSUM=1` 启用可选报文校验，`TJU_SACK=1` 启用可变 `hlen` 的 SACK option。默认无扩展时仍是 20 字节头；SACK ACK 可携带多个 block，但受 1400 字节报文上限约束，数据报文保留完整 SMSS 时不附加 block option。

## 模式与配置

在启动客户端/服务端前设置 `TJU_CC=basic|reno|newreno|cubic`，每条连接在 `tju_socket()` 中读取一次；未设置时使用 Task2 可靠传输兼容模式。未知模式会使 socket 创建失败，不会静默回退。例：

```sh
TJU_CC=newreno TJU_TRACE_DIR=/tmp/my-trace ./client
```

可组合启用扩展：`TJU_SACK=1` 启用受限 scoreboard，`TJU_RACK=1` 记录 RACK 和 TLP 事件，`TJU_CHECKSUM=1` 对固定头/载荷计算 8 位旋转校验并丢弃校验失败报文。该校验适配当前 20 字节教学头；它不是标准 TCP 16 位 checksum 的替代品。

目录应提前创建。测试脚本自动建立每次运行独立目录。初始拥塞窗口默认 1 SMSS，初始阈值默认 65535 字节；可用 `-DTJU_INITIAL_CWND=...`、`-DTJU_INITIAL_SSTHRESH=...` 适配课程公布的配置，修改参数后重新编译。SMSS 默认按 v3 为 `1400 - 20 = 1380` 字节；对旧课程框架可显式编译 `-DTJU_SMSS=1375`。所有窗口与 trace 均按字节计量，旧绘图脚本也已去掉固定除以 1375 的换算。

## 代码定位与行为

- `inc/tju_congestion.h`、`src/tju_congestion.c`：独立的每连接拥塞控制状态机；只接受已核验的 ACK/重复 ACK/超时事件，不处理网络、锁或定时线程。
- `inc/global.h`：`snd_nxt` 是已排队的数据右边界；新增 `snd_max` 是实际已发送的右边界，`flight_size` 只统计已发送但未累计确认的 payload。SYN、FIN、探测和重复重传不增加 payload 在途量。
- `src/tju_tcp.c`：发送新数据同时受通告窗口右边界和剩余拥塞窗口约束；ACK 不能确认只入队而未发送的数据。重传不重复计入 FlightSize。窗口缩小或丢包降低 cwnd 后，已有 FlightSize 可以暂时大于窗口，发送端停止新增在途数据而非删除已有数据。
- 慢启动采用 `cwnd += min(新确认字节数, SMSS)`；拥塞避免累计已确认字节，每确认约一个拥塞窗口增长一个 SMSS，避免 ACK 拆分造成过快增长。
- 第三个有效重复 ACK 触发快速重传。重复 ACK 必须确认号相同、无 payload、无 SYN/FIN、通告窗口不变且仍有在途数据；窗口更新、探测应答和数据捎带 ACK 不算重复 ACK。
- `basic`：设置 `ssthresh=max(FlightSize/2,2*SMSS)`，降低 cwnd，重传得到新 ACK 后进入拥塞避免。
- `reno`：进入快速恢复时 `cwnd=ssthresh+3*SMSS`，后续重复 ACK 膨胀窗口并允许发送符合两个窗口的新数据，新 ACK 到达后收缩至 ssthresh。
- `newreno`：额外记录恢复边界 recover；部分 ACK 不退出恢复，扣除新确认字节、按规则补回一个 SMSS，并立即重传下一缺口。完全确认恢复边界后使用 `min(ssthresh, FlightSize+SMSS)` 保守退出。序号比较支持 32 位回绕。
- RTO：单个连接维护一个最早未确认报文定时器；新 ACK 推进后重启，全确认后停止，重复 ACK 不重新启动。快速重传会重新计时。首次超时按当前 FlightSize 降阈值，同一段被计时器再次重传时保留阈值，cwnd 回到一个 SMSS。RTO 初始/下限为 1 秒，上限为课程测试兼容的 4 秒，使用单调时钟、RTTVAR/SRTT、Karn 排除歧义样本；SYN 重传后数据阶段初始 RTO 重设为 3 秒。未设置 `TJU_CC` 的第二阶段兼容模式保持 1 秒重传周期，以适应课程 90 秒高丢包验收；显式 Reno/CUBIC 模式执行指数退避。

本次还修复了接入中的流控问题：小于一段的窗口在无在途数据时拆分待发送段，零窗口探测从一个 RTO 开始指数退避，乱序数据占用不再重复扣减通告序号区间，接收越界数据被裁剪，重叠重组不会残留已交付段。关闭过程中继续有 ACK 进展或收到零窗口探测响应时，不会因为总传输超过 30 秒就提前放弃数据；停滞超时则返回失败，保留发送队列。

## Trace

保留课程 `SEND/RECV/CWND/RWND/SWND/RTTS/DELV` 格式。新增：

```text
[微秒时间戳] [CC] [reason:ack mode:newreno cwnd:... ssthresh:... rwnd:... flight:... state:... ack:... recover:...]
[微秒时间戳] [RETRANSMIT] [reason:fast seq:... length:...]
```

`CC reason` 包含 init、send、ack、window、dupack、fast、partial、timeout；`RETRANSMIT reason` 包含 fast、partial、rto。CC 的 rwnd 是对端通告值；课程 RWND 是本端接收窗口。SWND 为 `min(cwnd,rwnd)`，并非剩余可发送字节数。两台 VM 各自时间轴单独分析。

## 编译与确定性测试

在课程 Linux VM 中执行：

```sh
cd /vagrant/tju_tcp
make
make check
make -C test/phase3
```

`make check` 包含 15 组控制器测试和 15 组真实传输路径测试。后者经 `tju_handle_packet()` 注入编码报文并捕获真实 `sendToLayer3()` 输出，只替换时钟与网络边界，不改生产协议规则。覆盖窗口约束、握手/FIN 不增长窗口、未发送 ACK 拒绝、快重传、Reno 恢复发送、NewReno 多缺口、重复 RTO、不同报文超时、部分字节 ACK、回绕、小窗口、乱序和重叠接收。测试执行文件复制到虚拟机临时目录，以避开 Windows/VirtualBox 共享文件夹刚链接后的短暂执行失败。

## 双机复现与性能对比

见 `../../test/phase3/README.md`。`run_experiments.py` 默认 100 MiB，接收端逐字节验证，比较两端 FNV-1a 辅助校验值，等待双方 CLOSED。支持 `--matrix` 分别改变丢包率与单向时延、选择三种模式并重复实验；保存原始 trace、stdout/stderr、配置、返回码、Git 标识、工作区状态与源码 SHA-256。构建后的程序复制到每次实验的 VM 临时目录，避免后续重编译影响运行中的实验。

测试程序不修改课程验收器，不伪造丢包结果。随机丢包不能保证触发指定恢复分支；分支正确性由确定性回归与实际 trace 共同判断。吞吐统计使用应用数据阶段耗时，不含 TIME_WAIT；1 MiB 对照实验只用于性能趋势，不能代替 100 MiB 可靠传输测试。

## 范围与验证边界

本次提交的是第三阶段及挑战方向的代码、测试和复现工具，不是最终课程论文或线上验收成绩。课程平台的初始窗口、TJU_MSL、标准 checksum 存放规范和最终评分环境仍应按平台公告核对；当前 checksum 是固定教学头上的可选 8 位扩展校验，SACK 是受限 marker/scoreboard 实现，不能写成完整标准 TCP option 支持。旧阶段报告中的状态说明需要在撰写最终报告时按最终源码与真实证据更新。

依据：课程说明书 v3 第 5.5、5.6、10.6、10.7 节；RFC 5681、RFC 6298、RFC 6582。AI 协作包括独立控制器实现、传输接入、测试设计及缺陷复核；所有运行结论应以本目录验证记录和原始实验目录为准，学生人工复核/答辩记录需本人填写。

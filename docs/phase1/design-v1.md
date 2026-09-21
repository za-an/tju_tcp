# TJU_TCP 总体设计 V1

## 1. 范围和目标

TJU_TCP 是承载在 UDP `20218` 上的课程自定义传输协议，不与互联网标准 TCP 互操作。本设计覆盖第二阶段的连接管理、可靠数据传输和流量控制，以及第三阶段的基础 Reno；基础 Reno 通过后按依赖顺序实现完整 Reno、NewReno、SACK/6675、RACK-TLP 和 CUBIC 五项挑战。

## 2. 模块划分

| 模块 | 职责 | 主要接口 |
|---|---|---|
| 应用入口 | 创建 socket、调用连接和收发 API | `client.c`、`server.c` |
| TCP 控制块 | 状态、地址、缓冲区、计时器和窗口 | `tju_tcp_t` |
| 连接管理 | bind/listen/accept/connect/close 和状态机 | `tju_tcp.c` |
| 可靠传输 | 分段、序号、ACK、重传和重组 | `tju_tcp.c` |
| 流量/拥塞控制 | `rwnd`、`cwnd`、`ssthresh`、零窗口探测、Reno/NewReno/SACK/RACK-TLP/CUBIC | `tju_tcp.c` |
| 报文编码 | 网络字节序、头部、payload、checksum | `tju_packet.c` |
| UDP 模拟内核 | 20218 收发、线程和 socket 查找 | `kernel.c` |

## 3. 报文入口和分发

发送统一经过 `sendToLayer3()`；接收线程从 UDP 读取完整 `plen`，检查 `hlen/plen` 合法性后交给 `onTCPPocket()`。分发顺序为：已建立四元组连接 → 监听 socket → 丢弃并记录未知连接报文。后续实现应保证查找结果和连接状态在锁保护下使用。

## 4. 连接状态机设计

```text
CLOSED --listen--> LISTEN
CLOSED --connect/SYN--> SYN_SENT
LISTEN --SYN--> SYN_RECV --ACK--> ESTABLISHED
SYN_SENT --SYN-ACK/ACK--> ESTABLISHED
ESTABLISHED --close/FIN--> FIN_WAIT_1 --> FIN_WAIT_2 --> TIME_WAIT --> CLOSED
ESTABLISHED --peer FIN--> CLOSE_WAIT --close/FIN--> LAST_ACK --> CLOSED
```

设计要求：SYN 和 FIN 各占用一个序号；确认号表示对端下一个期望序号；握手报文丢失按 RTO 重传；最终 ACK 丢失时对重复 SYN-ACK 返回有效 ACK；主动关闭方等待 `2×TJU_MSL` 后释放连接。

## 5. 序号、确认号和缓冲区

- ISN 不使用固定常量，应包含时间和本地变化量。
- `send_base` 指向最早未确认数据；`next_seq` 指向下一个待发送序号。
- `recv_next` 是按序交付所需的下一个字节序号。
- 发送队列保留未确认段，累计 ACK 推进后释放已确认段。
- 接收端保留失序/重叠段，按序拼接后只向应用交付一次。
- 收发缓冲区容量至少为 `5000×SMSS` 的要求值；不得以固定小数组替代课程要求。

## 6. RTT、RTO 和重传

每条连接维护 `SRTT`、`RTTVAR`、`RTO` 和针对最早未确认段的定时器。初始 RTO 按说明书；第一个样本 `R` 使用 `SRTT=R`、`RTTVAR=R/2`、`RTO=SRTT+max(G,4×RTTVAR)`；后续先更新 RTTVAR，再更新 SRTT。重传过的报文不作为 RTT 样本。超时后重传最早未确认段并指数退避，ACK 推进时重启定时器，全部确认后停止。

## 7. 流量控制、基础 Reno 和挑战接口

接收端通告 `rwnd=min(可用空间,65535)`（未实现窗口扩展时）。发送端第二阶段允许的在途字节数受 `rwnd` 限制；第三阶段使用：

```text
effective_window = min(rwnd, cwnd)
FlightSize = 已发送但尚未累计确认的数据字节数
```

基础 Reno 变量以字节为单位：`cwnd`、`ssthresh`、`FlightSize`。慢启动阶段每个新数据 ACK 增加不超过一个 SMSS；拥塞避免阶段每 RTT 增长约一个 SMSS；RTO 或三次重复 ACK 时设置 `ssthresh=max(FlightSize/2,2×SMSS)` 并降低 `cwnd`，然后重新慢启动或按基础 Reno规则进入拥塞避免。

基础 Reno 冻结后，挑战按以下顺序实现：完整 Reno 依据 RFC 5681 §3.2 进行窗口膨胀和恢复 ACK 收缩；NewReno 依据 RFC 6582 §3.2 处理部分确认；SACK 依据 RFC 2018 §2–§5 协商、编码和解释选项，并依据 RFC 6675 §3–§5 维护记分板和选择重传段；RACK-TLP 依据 RFC 8985 §3、§6–§8 使用逐报文发送时间进行丢包检测和尾部探测；CUBIC 依据 RFC 9438 §3–§4 实现窗口函数、快速收敛和乘性降低。每项挑战使用独立开关、分支和 trace，且必须通过基础 Reno 回归测试。

## 8. 线程与同步

接收线程负责 UDP 收包和协议处理；应用线程调用 API。发送队列、接收队列和状态变量分别使用明确的 mutex 保护；`tju_recv()` 使用条件变量等待数据，不使用无界忙等。定时器采用每连接可管理的机制，禁止每个报文无控制地创建永久线程。

## 9. 错误和资源释放

所有分配、锁、线程、定时器和 socket 都要有失败路径；报文长度必须满足 `20≤hlen≤plen≤1400`；非法 checksum、未知连接和超出缓冲区的数据必须丢弃并记录；`tju_close()` 返回前应完成已提交数据的可靠交付，或返回规定的失败状态。

## 10. 测试策略

### 10.1 基线

环境为两台 Vagrant VM，client `172.17.0.2`、server `172.17.0.3`，网卡 `enp0s8`，`100 Mbps/20 ms/0% loss`。启动 server 后启动 client，保存编译输出、两端日志、qdisc、ping 和 `udp port 20218` pcap。

### 10.2 第二阶段

依次执行无丢包握手/收发、SYN/SYN-ACK/ACK 丢失、主动/被动关闭、不同长度数据、失序/重复/重叠、checksum 错误、RTO、三次重复 ACK、小窗口和零窗口探测。每项保存参数、日志、抓包和预期/实际结果。可靠传输最终用不少于 100 MB 数据验证。

### 10.3 第三阶段

固定代码版本，每组只改变一个实验变量，至少选择带宽、时延、丢包率、缓冲区大小或报文长度中的两类。每组重复实验并记录吞吐率、完成时间、重传次数、`rwnd/cwnd/ssthresh` 和原始 trace。使用课程 `test_congestion.py` 或等价命令配置 `tcset`，禁止补造实验数据。

### 10.4 五项挑战

基础 Reno 通过后，依次执行完整 Reno、NewReno、SACK/6675、RACK-TLP 和 CUBIC。完整 Reno 和 NewReno 使用可控重复 ACK 与部分确认场景；SACK/6675 使用一个窗口内多个非连续丢包；RACK-TLP 使用尾部丢包、ACK 延迟和可控重排序；CUBIC 使用无丢包增长、单次丢包和不同 RTT 对照。每项至少重复三次，保存参数、日志、pcap、trace、吞吐率、完成时间、重传次数和与基础 Reno 的差异。

## 11. 验收标准

第一阶段验收：环境可复现、基线可编译运行、UDP 抓包有效、架构结论可由函数和日志支持、需求表覆盖必做项和五项挑战、设计能映射到后续代码和测试、各项结论均有源码、规范或运行证据。第二、第三阶段及挑战阶段的功能状态须随开发更新，不提前标记为完成。

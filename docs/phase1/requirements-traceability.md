# 需求追踪表

状态含义：`基线已验证` 表示当前框架可观察到的行为；`第二阶段`、`第三阶段`、`挑战阶段` 表示实现计划，不能视为当前已完成。

| 任务要求 | 标准/依据 | 设计模块或状态 | 主要数据结构 | 测试方法 | 状态 |
|---|---|---|---|---|---|
| UDP 承载 TJU_TCP 报文 | 说明书 | `startSimulation`、`sendToLayer3`、接收线程 | UDP socket、20 字节头 | pcap 检查 `20218` 双向报文 | 基线已验证 |
| socket 创建和地址绑定 | 说明书 | `CLOSED`、`LISTEN` | `tju_tcp_t`、`tju_sock_addr` | 编译运行 server/client | 基线已验证 |
| 三次握手 | RFC 9293 | `SYN_SENT`、`SYN_RECV`、`ESTABLISHED` | ISN、seq、ack、状态 | 正常、SYN/SYN-ACK/ACK 丢失 | 第二阶段 |
| 连接关闭 | RFC 9293 | FIN-WAIT、CLOSE-WAIT、LAST-ACK、TIME-WAIT | `fin_seq`、`fin_ack` | 主动、被动、同时关闭和 FIN 重传 | 第二阶段 |
| 数据分段和重组 | RFC 9293/说明书 | 发送/接收路径 | `sending_buf`、`received_buf` | 小数据、边界长度、乱序 | 第二阶段 |
| 累计确认和滑动窗口 | RFC 9293 | ACK 推进、发送窗口 | `send_base`、`next_seq`、`peer_window` | 多段数据和 ACK 推进 | 第二阶段 |
| checksum | 说明书 | 报文编码/校验入口 | TJU_TCP 头和 payload | 篡改报文、校验失败丢弃 | 第二阶段 |
| 失序、重复和重叠处理 | RFC 9293 | 接收缓存和累计 ACK | 接收段队列、`recv_next` | 注入失序、重复、重叠报文 | 第二阶段 |
| RTT/RTO、Karn 算法 | RFC 6298 | 最早未确认段定时器 | `SRTT`、`RTTVAR`、`RTO` | 时延变化、RTO 超时、重传 | 第二阶段 |
| 基本快速重传 | RFC 5681/说明书 | 三次重复 ACK | 重复 ACK 计数、未确认队列 | 制造三次重复 ACK | 第二阶段 |
| 流量控制 | 说明书 | `rwnd`、窗口通告、零窗口探测 | 收发缓冲区、`peer_window` | 小接收窗口、零窗口、窗口恢复 | 第二阶段 |
| 基础 Reno | RFC 5681 | 慢启动、拥塞避免、RTO/重复 ACK 响应 | `cwnd`、`ssthresh`、`FlightSize` | cwnd trace、RTO、重复 ACK | 第三阶段 |
| 完整 Reno 快速恢复 | RFC 5681 §3.2 | 第三个重复 ACK 快速重传、`cwnd` 膨胀、恢复期新数据、恢复 ACK 收缩 | `dup_ack_count`、恢复状态、恢复起止序号 | 单个窗口丢包、三次重复 ACK、恢复 ACK | 挑战阶段 |
| NewReno 部分确认恢复 | RFC 6582 §3.2 | 无 SACK 时用 `recover` 识别部分确认，恢复期间重传下一个缺失段 | `recover`、部分 ACK 分类、重传指针 | 一个窗口内两个丢包，验证部分 ACK 不提前退出 | 挑战阶段 |
| SACK 选项和协商 | RFC 2018 §2–§5 | SACK-Permitted 协商、SACK block 编解码、解释和边界检查 | SACK 能力、SACK block 列表 | 协商成功/拒绝、非连续失序段 | 挑战阶段 |
| SACK 保守丢包恢复 | RFC 6675 §3–§5 | 维护 scoreboard，按算法规则选择需要重传的段 | `sack_scoreboard`、段状态 | 多个非连续丢包，只重传缺失段 | 挑战阶段 |
| RACK-TLP | RFC 8985 §3、§6–§8 | 基于发送时间的丢包判定、Tail Loss Probe 和定时器 | 逐报文发送时间、重排序窗口、TLP 状态 | 尾部丢包、ACK 延迟和可控重排序 | 挑战阶段 |
| CUBIC | RFC 9438 §3–§4 | CUBIC 窗口增长、快速收敛、乘性降低和 TCP-friendly 区域 | `W_max`、`K`、epoch、最小 RTT | 无丢包增长、单次丢包、不同 RTT 对照 | 挑战阶段 |
| 功能和性能验证 | 说明书 | 日志、抓包、trace、脚本 | 原始数据和图表 | 带宽、时延、丢包、缓冲区及五项挑战对照 | 第三阶段及挑战阶段 |

## 当前基线证据

- 编译：`baseline-build.log`。
- 双向应用输出：`baseline-client.log`、`baseline-server.log`。
- UDP 报文：`baseline.pcap`、`baseline-packets.txt`。
- 网络配置和 ping：`network-client.txt`、`network-server.txt`。

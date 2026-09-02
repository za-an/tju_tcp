# 需求追踪表

状态含义：`基线已验证` 表示当前框架可观察到的行为；`第二阶段`、`第三阶段` 表示实现计划，不能视为当前已完成。

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
| 功能和性能验证 | 说明书 | 日志、抓包、trace、脚本 | 原始数据和图表 | 带宽、时延、丢包、缓冲区对照 | 第三阶段 |

## 当前基线证据

- 编译：`baseline-build.log`。
- 双向应用输出：`baseline-client.log`、`baseline-server.log`。
- UDP 报文：`baseline.pcap`、`baseline-packets.txt`。
- 网络配置和 ping：`network-client.txt`、`network-server.txt`。


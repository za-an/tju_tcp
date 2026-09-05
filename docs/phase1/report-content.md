# 第一阶段报告填写内容

> 对应报告模板第二、三、四章。第一阶段是设计阶段，因此“实现状态”采用“设计完成/待第二阶段实现/待第三阶段实现”的表述。当前基线实测结果仅用于证明环境、构建和 UDP 最小通信可运行，不代表 TCP 机制已经实现。

## 二、任务分析、标准依据（第一阶段）

### 2.1 标准依据

本项目实现课程自定义 TJU_TCP。TJU_TCP 使用自定义 20 字节报文头，承载在 UDP 端口 20218 之上，仅在课程提供的 client/server 虚拟机之间运行，不与互联网标准 TCP 直接互操作。设计时以 RFC 的机制和语义为依据，再按照课程说明书的报文格式、接口和教学范围进行裁剪。

#### 表 2-1 标准/课程要求分析表

| 标准/课程要求 | 本次实践要求完成的功能 | 两者差异简要说明 | RFC相应章节 |
|---|---|---|---|
| RFC 9293 | TCP 基本规范、连接与可靠传输。本实践计划完成 TJU_TCP 的三次握手、连接关闭、序号与确认号、数据分段与重组、累计 ACK、发送/接收窗口和状态管理。 | 本实践使用课程自定义的 20 字节 TJU_TCP 报文头并承载在 UDP `20218` 端口上，只保证两台课程 VM 间运行，不要求与标准 TCP 互操作；课程范围裁剪了 RST、半开放连接恢复等未要求机制。 | §3.1（Header Format）；§3.3/§3.3.2（TCP Terminology Overview / State Machine Overview）；§3.4（Sequence Numbers）；§3.5（Establishing a Connection）；§3.6（Closing a Connection）；§3.7（Segmentation）；§3.8（Data Communication）、§3.8.1（Retransmission Timeout）、§3.8.2（TCP Congestion Control）、§3.8.6（Managing the Window）；§3.10（Event Processing，含 OPEN、SEND、RECEIVE、CLOSE、报文到达和超时事件） |
| RFC 6298 | RTT 估计与 RTO。本实践计划维护 RTT 样本、SRTT、RTTVAR、RTO，实现最早未确认数据的重传定时器、超时重传、指数退避和 Karn 算法。 | 算法原则按 RFC 6298 执行，但定时器服务于课程自定义 TJU_TCP；本实践采用每条连接针对最早未确认数据的定时器，不实现真实 TCP 的全部计时器和时间戳扩展。重传过的报文不参与 RTT 采样。 | §2（The Basic Algorithm）；§3（Taking RTT Samples）；§4（Clock Granularity）；§5（Managing the RTO Timer） |
| RFC 5681 | 基础 Reno。必做内容为慢启动、拥塞避免、RTO 丢包响应、重复 ACK 处理和三次重复 ACK 触发的基本快速重传。 | 基础 Reno 属于第三阶段必做内容；五项挑战在基础 Reno 通过后实施，不得把挑战功能写入基础版本成绩或测试结论。发送窗口使用 `min(rwnd,cwnd)`，并单独记录 `rwnd`、`cwnd` 和 `FlightSize`。 | §3.1（Slow Start and Congestion Avoidance）；§3.2（Fast Retransmit/Fast Recovery）；§4.2（Generating Acknowledgments）；§4.3（Loss Recovery Mechanisms） |
| 挑战任务（可选）RFC 2018/6582/6675/8985/9438 | 本项目五项挑战全部选择：①完整 Reno 快速恢复；②NewReno 部分确认恢复；③SACK 协商、SACK 记分板及基于 SACK 的保守丢包恢复；④RACK-TLP 基于发送时间的丢包检测和尾部丢包探测；⑤CUBIC 拥塞避免与拥塞响应。 | 挑战均建立在可靠传输、累计 ACK、`rwnd` 和基础 Reno 之上；SACK/RACK 还需要扩展字段、记分板和逐报文时间信息。CUBIC 使用三次窗口增长函数、快速收敛和乘性降低，需与 Reno 的线性增长和丢包降窗规则做对比。课程仍使用自定义 20 字节头和 UDP `20218`，只要求课程 VM 间互通，不声称与标准 TCP 直接互操作；五项挑战分别记录分支、trace、参数和对比结果。 | 完整 Reno：RFC 5681 §3.2（Fast Retransmit/Fast Recovery）；NewReno：RFC 6582 §3.2（Specification）；SACK 选项：RFC 2018 §2（SACK-Permitted Option）、§3（SACK Option Format）、§4（Generating SACK Options）、§5（Interpreting the Sack Option and Retransmission Strategy: Data Sender Behavior）；SACK 丢包恢复：RFC 6675 §3（Keeping Track of SACK Information）、§4（Processing and Acting Upon SACK Information）、§5（Algorithm Details）；RACK-TLP：RFC 8985 §3（RACK-TLP High-Level Design）、§6（RACK Algorithm Details）、§7（TLP Algorithm Details）、§8（Managing RACK-TLP Timers）；CUBIC：RFC 9438 §3（Design Principles of CUBIC）、§4（CUBIC Congestion Control） |

#### 标准采用说明

本表章节号按 RFC Editor 发布的官方原文目录核对（访问日期：2026-09-05），不是按博客或二手资料推定。官方原文如下：

- [RFC 9293 — Transmission Control Protocol](https://www.rfc-editor.org/rfc/rfc9293.html)
- [RFC 6298 — Computing TCP's Retransmission Timer](https://www.rfc-editor.org/rfc/rfc6298.html)
- [RFC 5681 — TCP Congestion Control](https://www.rfc-editor.org/rfc/rfc5681.html)
- [RFC 2018 — TCP Selective Acknowledgment Options](https://www.rfc-editor.org/rfc/rfc2018.html)
- [RFC 6582 — The NewReno Modification to TCP's Fast Recovery Algorithm](https://www.rfc-editor.org/rfc/rfc6582.html)
- [RFC 6675 — A Conservative Loss Recovery Algorithm Based on Selective Acknowledgment (SACK) for TCP](https://www.rfc-editor.org/rfc/rfc6675.html)
- [RFC 8985 — The RACK-TLP Loss Detection Algorithm for TCP](https://www.rfc-editor.org/rfc/rfc8985.html)
- [RFC 9438 — CUBIC for Fast and Long-Distance Networks](https://www.rfc-editor.org/rfc/rfc9438.html)

RFC 9293 是连接管理和可靠传输的主要依据，RFC 6298 是 RTT/RTO 和定时器设计依据，RFC 5681 是基础 Reno 和完整 Reno 快速恢复的依据；RFC 6582、RFC 2018、RFC 6675、RFC 8985、RFC 9438 分别对应其余挑战。课程自定义部分覆盖报文头格式、UDP 承载、测试工具、最大长度和提交流程。第一阶段不实现协议功能，但为五项挑战预留拥塞状态、重复 ACK 计数、SACK 记分板、逐报文发送时间、TLP 状态和 CUBIC 参数的设计位置。

### 2.2 需求追踪表

#### 表 2-2 需求追踪表

| ID | 任务要求 | 报告对应章节 | 实现模块 | 测试/证据 |
|---|---|---|---|---|
| R1 | 连接建立与连接关闭：三次握手、ISN、SYN/ACK 序号语义、重复握手报文、FIN 状态机、FIN 重传、TIME-WAIT 和资源释放 | 第 5 章；设计见第 4.3 节 | `src/tju_tcp.c` 状态机；`tju_connect`、`tju_bind`、`tju_listen`、`tju_accept`、`tju_close`；`tju_tcp_t` | 正常握手；SYN、SYN-ACK、最终 ACK 丢失；主动、被动、同时关闭；状态日志和 UDP pcap；第二阶段提交 |
| R2 | 可靠数据传输：分段、序号、累计确认、发送/接收缓冲区、失序/重复/重叠处理、checksum、RTT/RTO、超时和快速重传 | 第 6 章；设计见第 4.3、4.4 节 | `src/tju_tcp.c`；`src/tju_packet.c`；发送未确认队列和接收失序队列 | 小数据和边界长度；篡改 checksum；失序/重复/重叠报文；RTO；三次重复 ACK；不少于 100 MB 文件；日志、pcap 和 trace |
| R3 | 流量控制：接收窗口通告、有效窗口、窗口滑动、窗口缩小处理、零窗口探测、SWS 避免 | 第 6 章；设计见第 4.3、4.4 节 | `receiver_window_t`、`sender_window_t`；`advertised_window`、`rwnd`；发送判定和接收缓存 | 小接收缓冲区；降低接收处理速度；观察 `rwnd` 减小、归零、探测和恢复；窗口变化日志和图表；第二阶段提交 |
| R4 | 基础 Reno 及五项挑战：基础慢启动、拥塞避免、RTO/重复 ACK 响应；完整 Reno、NewReno、SACK/6675、RACK-TLP 和 CUBIC 均计划完成并分别验收 | 第 7 章；挑战设计见本章“挑战任务实施说明” | `cwnd`、`ssthresh`、`FlightSize`、恢复状态、`recover`；SACK 块和 scoreboard；逐报文发送时间、重排序窗口和 TLP；CUBIC `W_max/K` 等参数；独立挑战分支 | 基础 Reno 曲线和丢包响应；完整 Reno 窗口膨胀/收缩；NewReno 部分确认；SACK 多丢包恢复；RACK 时间检测和 TLP；CUBIC 增长/降低曲线；每项均保存 trace、原始数据、参数和对比图表 |
| R5 | 功能测试、性能测试、代码复现、过程记录和报告一致性 | 第 8 章、附录 B；第一阶段计划见第 4.5 节 | `test/` 脚本；日志、pcap、trace、数据处理和绘图脚本；版本记录和复核材料 | 固定代码版本，每组只改变一个变量；至少选择带宽、时延、丢包、缓冲区或报文长度中的两类；保存原始数据并重复实验；第一阶段先完成基线证据 |

第一阶段的需求状态为“已完成需求分解和设计映射，协议功能待后续阶段实现”。当前基线实测只证明 R5 中的环境、构建、最小 UDP 通信和抓包链路可运行，不能据此判定 R1-R4 已经完成。

## 三、已有编程框架的代码架构分析（第一阶段）

### 3.1 环境搭建与基线运行

实验使用 `Vagrantfile` 创建的两台 Ubuntu 虚拟机：

| 项目 | 客户端 | 服务端 |
|---|---|---|
| 主机名 | `client` | `server` |
| 私有 IP | `172.17.0.2/16` | `172.17.0.3/16` |
| 通信网卡 | `enp0s8` | `enp0s8` |
| 内核 | `5.4.0-80-generic` | `5.4.0-80-generic` |
| GCC | `9.3.0` | `9.3.0` |
| GNU Make | `4.2.1` | `4.2.1` |
| Git | `2.25.1` | `2.25.1` |
| Python | `3.8.10` | `3.8.10` |
| 网络配置工具 | `/usr/local/bin/tcset` | `/usr/local/bin/tcset` |
| 抓包工具 | `/usr/sbin/tcpdump` | `/usr/sbin/tcpdump` |

基线网络参数为两端均执行：

```bash
sudo tcset enp0s8 --rate 100Mbps --delay 20ms --delay-distro 0 --loss 0% --overwrite
```

即带宽 100 Mbps、单向延迟 20 ms、抖动 0 ms、丢包率 0%。两端均配置后理论额外 RTT 约为 40 ms。实测 ping 结果：client 到 server 为 10/10 成功、0% 丢包、`min/avg/max = 40.829/46.807/71.533 ms`；server 到 client 为 10/10 成功、0% 丢包、`min/avg/max = 40.324/54.402/108.899 ms`。偶发高值来自虚拟机调度和共享宿主机资源，应保留原始数据并在性能分析中说明。

在 `/vagrant/tju_tcp` 执行 `make clean && make` 成功，生成 `server` 和 `client`。并行启动两端程序后，双方均正确收到：

```text
hello world
hello tju
```

使用 `tcpdump -ni enp0s8 -s 0 -U -c 4 -w baseline.pcap 'udp port 20218'` 抓包，得到 4 个 UDP 报文，统计为 `4 packets captured`、`4 packets received by filter`、`0 packets dropped by kernel`。相关原始证据保存在 `docs/phase1/`。

### 3.2 项目结构、模块功能和修改边界

| 文件/目录 | 主要职责 | 当前基线功能 | 后续允许或计划修改内容 |
|---|---|---|---|
| `Makefile` | 编译目标和对象文件依赖 | 编译 `build/*.o`、`server`、`client` | 需要时维护构建规则，保持命令可复现 |
| `inc/global.h` | 常量、状态值和核心数据结构 | 定义 `MAX_LEN`、状态枚举、窗口和 `tju_tcp_t` | 扩展序号、队列、计时器、Reno/NewReno/SACK/RACK-TLP/CUBIC 状态字段 |
| `inc/tju_packet.h` | 报文结构和编解码接口 | 定义 20 字节头部及 `get_*` 接口 | 按说明书补充 checksum；增加可跳过的扩展 TLV、SACK block 和探测标志编解码 |
| `inc/tju_tcp.h` | 对应用暴露 TJU_TCP API | 声明 8 个规定接口 | 保持接口和函数签名兼容 |
| `inc/kernel.h` | UDP 模拟内核接口 | 声明启动、收发和哈希查找 | 保持底层接口和 UDP 承载端口 |
| `src/client.c` | 客户端基线入口 | 创建 socket、connect、发送和接收两段数据 | 主要作为测试入口，不承载协议算法 |
| `src/server.c` | 服务端基线入口 | socket、bind、listen、accept、发送和接收 | 主要作为测试入口，不承载协议算法 |
| `src/tju_tcp.c` | socket、连接、收发、状态和缓冲区 | 提供教学骨架；当前连接状态和数据处理不完整 | 第二阶段实现 R1-R3，第三阶段实现基础 Reno，挑战阶段依次实现五项挑战 |
| `src/tju_packet.c` | 头部序列化和字段解析 | 网络字节序读写、payload 拼接 | 补充规定的校验和边界处理；实现扩展 TLV、SACK block 和探测信息 |
| `src/kernel.c` | UDP `20218` 收发、接收线程、socket 查找 | `sendto/recvfrom`、接收线程和分发 | 只做与协议入口、校验和线程安全相关的必要修改 |
| `test/` | 测试程序、RDT 测试和绘图脚本 | 提供 `rdt_*`、`test_congestion.py`、抓包绘图工具 | 增加五项挑战的独立测试、trace 解析和 Reno 对比脚本，不修改测试逻辑绕过验收 |

### 3.3 规定接口、调用关系和数据流

#### 3.3.1 客户端调用关系

```text
client.c:main
  -> startSimulation()
  -> tju_socket()
  -> tju_connect(172.17.0.3:1234)
  -> tju_send("hello world")
  -> tju_send("hello tju")
  -> tju_recv(...)
```

#### 3.3.2 服务端调用关系

```text
server.c:main
  -> startSimulation()
  -> tju_socket()
  -> tju_bind(172.17.0.3:1234)
  -> tju_listen()
  -> tju_accept()
  -> tju_send("hello world")
  -> tju_send("hello tju")
  -> tju_recv(...)
```

#### 3.3.3 报文发送路径

```text
tju_send()
  -> create_packet_buf()
  -> packet_to_buf()/header_in_char()
  -> sendToLayer3()
  -> UDP sendto(172.17.0.X:20218)
  -> enp0s8
```

#### 3.3.4 报文接收路径

```text
UDP recvfrom(20218)
  -> receive_thread()
  -> 按 plen 读取完整报文
  -> onTCPPocket()
  -> cal_hash() 查找连接或监听 socket
  -> tju_handle_packet()
  -> received_buf
  -> tju_recv()
  -> 应用缓冲区
```

当前基线 `tju_handle_packet()` 主要将 payload 追加到接收缓冲区；后续应在该路径加入状态检查、checksum、序号、累计 ACK、窗口更新和重传事件处理。

### 3.4 关键数据结构、并发与定时机制

#### 3.4.1 数据结构位置和作用

当前基线的公共结构定义在 `inc/global.h` 和 `inc/tju_packet.h`，连接控制、报文处理和 UDP 接收分别位于 `src/tju_tcp.c`、`src/tju_packet.c` 和 `src/kernel.c`。需要区分“源码中已经存在的骨架字段”和“后续实现计划字段”：注释中的字段尚未提供运行时功能，不能在第一阶段报告中写成已实现。

| 数据结构或对象 | 源码位置和当前状态 | 生命周期/作用 | 后续实现职责 |
|---|---|---|---|
| `tju_tcp_t` | `inc/global.h`；`tju_socket()` 已分配并初始化 | 保存 `state`、绑定地址、已建立连接四元组、收发缓存及同步对象；`tju_accept()` 当前通过 `memcpy` 复制控制块 | 增加 ISN、`send_base`、`next_seq`、`recv_next`、ACK、关闭标志、错误状态和算法开关；连接关闭后按状态机释放全部资源 |
| `tju_header_t` / `tju_packet_t` | `inc/tju_packet.h`；固定 20 字节头，`tju_packet_t` 另有 `sent_time` 和 `data` 指针 | `create_packet()` 创建临时报文，`packet_to_buf()` 序列化，`free_packet()` 释放；`get_*()` 按偏移解析字段 | 保持网络字节序和 `20 <= hlen <= plen <= 1400`；增加 checksum、扩展 TLV、SACK block 和探测标志的合法性检查 |
| `window_t` / `sender_window_t` | `inc/global.h`；当前仅分配窗口指针，`sender_window_t` 实际只有 `window_size`，其余建议字段仍为注释 | 挂在每个 `tju_tcp_t` 上 | 保存 `rwnd/cwnd/ssthresh/FlightSize`、未确认队列、SRTT/RTTVAR/RTO、重复 ACK 计数；扩展完整 Reno、NewReno、RACK-TLP 和 CUBIC 状态 |
| `receiver_window_t` | `inc/global.h`；当前有 `received[TCP_RECVWN_SIZE]` 数组，失序相关字段仍为注释 | 随连接创建和销毁，提供接收缓存 | 保存 `recv_next`、可用空间、失序/重叠段队列、SACK blocks 和 scoreboard，保证按序且只交付一次 |
| `sending_buf` / 未确认队列 | `tju_tcp_t` 中已有指针和长度；当前 `tju_send()` 发送后没有未确认队列 | 应用发送时产生，累计 ACK 后释放 | 保存每个段的序号、长度、发送次数、首次/最近发送时间和确认状态，支持 RTO、快速重传、SACK/6675 和 RACK |
| `received_buf` / 失序队列 | `tju_tcp_t` 中已有指针和长度；`tju_handle_packet()` 当前直接追加 payload | 收到报文时扩展，`tju_recv()` 消费 | 先校验 checksum 和序号，再合并失序/重叠段，推进累计 ACK 并唤醒等待的应用线程 |
| `listen_socks[]` / `established_socks[]` | `src/kernel.c` 全局数组；由 `startSimulation()` 清零，`tju_listen()`/`tju_connect()`/`tju_accept()` 写入 | 进程启动时初始化，连接建立和关闭时增删 | 用锁保护哈希表更新和查找，处理哈希冲突、未知连接和关闭后的清理 |

#### 3.4.2 状态机、线程和锁

`inc/global.h` 已定义 `CLOSED`、`LISTEN`、`SYN_SENT`、`SYN_RECV`、`ESTABLISHED`、FIN 相关状态，以及 `SLOW_START`、`CONGESTION_AVOIDANCE`、`FAST_RECOVERY`。当前基线中，`tju_listen()` 直接进入 `LISTEN`，`tju_connect()` 和 `tju_accept()` 直接设置 `ESTABLISHED`，尚未发送或验证 SYN、ACK、FIN；因此这些枚举是后续设计接口，不是当前基线已经完成的 TCP 状态机。

线程和调用关系如下：

1. `client.c:main()` 或 `server.c:main()` 是应用主线程，调用 `startSimulation()` 后使用 TJU_TCP API。
2. `startSimulation()` 在 `src/kernel.c` 创建并绑定 UDP `20218` socket，再启动一个 `receive_thread`。
3. `receive_thread()` 使用 `recvfrom(MSG_PEEK)` 读取头部，根据 `plen` 接收完整 UDP 报文，调用 `onTCPPocket()`；后者通过四元组 `cal_hash()` 查找 socket，再调用 `tju_handle_packet()`。
4. 当前 `tju_socket()` 初始化 `send_lock`、`recv_lock` 和 `wait_cond`；`tju_handle_packet()` 和 `tju_recv()` 使用 `recv_lock` 保护接收缓存，但 `tju_send()` 当前未使用 `send_lock`，全局 socket 哈希表也没有锁保护。

后续实现应为每个连接设置明确的状态锁，分别保护发送队列、接收队列和状态变量；条件变量与 `received_len` 的谓词配套，在收到按序数据、连接关闭或错误时唤醒 `tju_recv()`。`tju_accept()` 不应再复制包含 mutex/condition variable 的整个结构体，而应逐字段初始化新控制块，避免同步对象被浅复制。

#### 3.4.3 定时机制、可靠性和风险

当前源码没有运行中的 RTO 定时器：`tju_packet_t.sent_time` 仅作为结构字段存在，`tju_send()` 使用固定 `seq=464`、`ack=0`，没有 RTT 采样、超时重传或指数退避。后续设计采用“每条连接一个可取消的最早未确认段定时器”：首个在途段发送时启动，累计 ACK 推进时重启，全部确认后停止；超时只重传最早未确认段并退避 RTO。仅使用未重传段采集 RTT，RACK-TLP 另行记录逐报文发送时间。

主要风险及应对如下：

| 风险类别 | 当前代码表现 | 后续应对 |
|---|---|---|
| 可靠性 | 固定序号、无 SYN/ACK/FIN 状态处理、无 ACK、无重传队列、无 checksum、无失序/重复/重叠处理 | 先完成序号空间和状态机，再实现累计 ACK、未确认队列、RTO/快速重传，最后接入五项挑战 |
| 内存和报文安全 | `receive_thread()` 从报文直接读取 `plen` 并分配内存；`tju_handle_packet()` 直接计算 `plen-20`，没有先检查长度和 checksum | 接收入口先验证 `20 <= hlen <= plen <= MAX_LEN`、扩展长度和 checksum，非法报文丢弃并记录 |
| 并发安全 | `tju_recv()` 先无锁忙等；`wait_cond` 未使用；发送锁未使用；全局哈希表无锁；`tju_accept()` 浅复制 pthread 对象 | 使用 mutex + 条件变量的谓词循环；锁保护哈希表和队列；新连接逐字段初始化；为关闭、错误和线程退出设计释放路径 |
| 性能 | `tju_recv()` 忙等占用 CPU；接收缓存反复 `malloc/realloc`；每个报文都可能触发分配和复制；无窗口/批量发送控制 | 使用环形缓冲区或分段队列、条件变量、发送窗口和批量处理；限制队列长度，记录 CPU、吞吐、重传和完成时间 |
| 定时与误判 | 当前没有 RTO；若把重传段再次作为 RTT 样本会污染估计；RACK 还可能受时钟精度和重排序影响 | 使用单调时钟、每连接定时器和退避上限；区分 RTO、重复 ACK、TLP 和 RACK 事件，保存原始时间戳与 trace |

### 3.5 环境搭建结果

环境、编译和基线运行均完成：

1. 两台 VM 的主机名、IP、网卡和路由符合课程配置。
2. `tcset` 能配置 `100 Mbps/20 ms/0% loss`，`tcpdump` 能捕获 UDP `20218` 报文。
3. 基线代码在未修改 `src/`、`inc/` 和 `test/` 协议实现的情况下成功编译。
4. client/server 均能输出两条预期回显。
5. pcap 文件可读取，包含双向共 4 个 UDP 报文，内核丢包数为 0。
6. ping 双向 10 次均成功，实测 RTT 与两端各 20 ms 的 netem 延迟相符，异常高值已保留在原始数据中。

证据文件：`environment-client.txt`、`environment-server.txt`、`network-client.txt`、`network-server.txt`、`baseline-build.log`、`baseline-client.log`、`baseline-server.log`、`baseline.pcap`、`baseline-packets.txt`。

### 3.6 第一阶段人工复核与证据记录

本阶段以 Git 基线 `37fbd090baf2e5df8767edd43fc225a777b5e5e4` 为参照，逐项核对源码、RFC 章节、构建输出、两端日志、网络参数和 pcap。复核确认当前基线只完成 20 字节报文头、网络字节序、UDP `20218` 承载、socket 分发和最小双向收发；三次握手、可靠重传、RTO、窗口控制、并发保护及五项挑战均标记为后续实现或未验证。复核记录和关键证据索引见 `review-notes.md`。

## 四、协议总体设计（第一阶段）

### 4.1 设计目标、实现边界和总体架构

设计目标是在课程两台 VM 之间实现可解释、可测试、可复现的 TJU_TCP：上层提供规定的 socket API，中间层完成连接管理、可靠数据传输、流量控制和基础 Reno，并在基础 Reno 通过后完成五项挑战；底层通过 UDP `20218` 发送自定义报文。

```text
client.c / server.c
        |
TJU_TCP API：socket、bind、listen、accept、connect、send、recv、close
        |
连接状态机 + 可靠传输 + 流量控制 + 基础 Reno
        |
完整 Reno / NewReno + SACK/6675 + RACK-TLP + CUBIC
        |
报文编码/解码 + 网络字节序 + checksum
        |
UDP 模拟内核：sendto、recvfrom、接收线程、socket 查找
        |
UDP 20218 / enp0s8 / 172.17.0.2 <-> 172.17.0.3
```

实现边界：最大报文总长度为 1400 字节；基础报文头保持课程规定的 20 字节，挑战所需的 SACK 块、协商标志和时间信息通过课程允许的扩展表示并在编码层明确长度；第二阶段完成连接管理、可靠传输和流量控制；第三阶段完成基础 Reno、综合测试和报告；基础 Reno 通过后按“完整 Reno→NewReno→SACK/6675→RACK-TLP→CUBIC”的依赖顺序完成五项挑战。所有挑战均在独立分支和独立 trace 中验收，不要求与标准 TCP 互操作。

### 4.2 报文格式和关键数据结构

TJU_TCP 使用课程规定的 20 字节固定报文头，头部字段按网络字节序（大端）编码；payload 紧跟在 `hlen` 指定的偏移处。当前源码中的 `DEFAULT_HEADER_LEN` 为 20，`MAX_LEN` 为 1400，`MAX_DLEN` 为 1375，因此一个基线数据报文的总长度满足 `plen = 20 + payload_len <= 1400`。

| 偏移 | 字段 | 长度 | 用途 |
|---:|---|---:|---|
| 0 | `source_port` | 2 字节 | 发送端的 TJU_TCP 端口，用于连接四元组和报文分发 |
| 2 | `destination_port` | 2 字节 | 接收端的 TJU_TCP 端口，用于连接四元组和报文分发 |
| 4 | `seq_num` | 4 字节 | 报文中首个数据字节的序号；SYN/FIN 控制报文也占用序号空间 |
| 8 | `ack_num` | 4 字节 | 累计确认号，表示接收端按序收到的最后位置之后、期望对端发送的下一个序号 |
| 12 | `hlen` | 2 字节 | 头部长度；基线固定为 20，后续扩展字段计入该长度 |
| 14 | `plen` | 2 字节 | 报文总长度，包含头部、扩展区和 payload |
| 16 | `flags` | 1 字节 | 控制标志位；当前定义 `SYN=0x8`、`ACK=0x4`、`FIN=0x2` |
| 17 | `advertised_window` | 2 字节 | 接收端向发送端通告的可用接收窗口，用于流量控制 |
| 19 | `ext` | 1 字节 | 课程保留扩展字段；基线为 0，后续用于标识扩展编码 |

发送端由 `header_in_char()` 依次写入上述字段，并对 16 位、32 位整数调用 `htons()`、`htonl()`；接收端的 `get_*()` 函数按同一偏移调用 `ntohs()`、`ntohl()` 解析。`packet_to_buf()` 先生成头部，再将 payload 复制到 `msg + hlen`；因此接收端应先确认报文长度合法，再计算 `payload_len = plen - hlen`，避免固定跳过未经验证的偏移。当前 `receive_thread()` 已根据固定 20 字节头读取 `plen`，但 `tju_handle_packet()` 仍按基线常量 20 取数据，后续实现应统一改为使用经过检查的 `hlen`。

**校验和。** 当前基线的 `tju_header_t` 没有独立 checksum 字段，`header_in_char()`、`packet_to_buf()` 和 `tju_handle_packet()` 也没有计算或验证校验和，因此本阶段应将 checksum 标记为“待实现”。后续若按课程允许的扩展位置加入 checksum，应在序列化后的 TJU_TCP 自定义报文范围内计算，即覆盖头部（含扩展区）和 payload，不覆盖外层 UDP/IP 头；计算前将 checksum 字段置零，发送后写回结果，接收端对同一范围重新计算。长度检查和 checksum 检查均通过后，报文才可进入状态机、序号确认和数据交付；失败报文直接丢弃并记录原因。

**序号空间。** `seq_num`、`ack_num` 均为 32 位无符号字段，正式实现采用按字节编号的序号空间，并按 32 位模运算比较。发送端维护 `send_base`（最早未累计确认的序号）和 `next_seq`（下一个待发送序号）；接收端维护 `recv_next`（下一个按序期待的序号）。数据段占用其 payload 长度对应的连续序号，SYN 和 FIN 各额外占用一个序号，ACK 号表示对端下一个期待序号。当前基线 `tju_send()` 固定使用 `seq_num=464`、`ack_num=0`，连接建立也直接进入 `ESTABLISHED`，所以真实序号推进、累计 ACK、重传和回绕处理属于后续阶段，不列为基线已实现功能。

**关键数据结构及作用。**

- `tju_header_t`（`inc/tju_packet.h`）：保存 20 字节头的九个字段，是逻辑报文头；它本身不等同于 C 结构体内存布局，线上格式以 `header_in_char()` 的显式序列化为准。
- `tju_packet_t`（`inc/tju_packet.h`）：由 `header`、`sent_time` 和 `data` 指针组成。`create_packet()` 分配并复制 payload，`packet_to_buf()` 生成待发送字节串，`free_packet()` 释放 payload 和对象；当前 `sent_time` 仅预留，尚未驱动 RTT/RTO。
- `tju_tcp_t`（`inc/global.h`）：每条连接的控制块，保存 `state`、本地/远端地址、发送缓存 `sending_buf/sending_len`、接收缓存 `received_buf/received_len`、`send_lock`、`recv_lock`、`wait_cond` 以及 `window`。它是连接管理、缓冲区、并发同步和后续定时器状态的归属对象。
- `window_t`、`sender_window_t`、`receiver_window_t`（`inc/global.h`）：`window_t` 挂载发送和接收窗口指针；基线 `sender_window_t` 实际只有 `window_size`，`receiver_window_t` 实际只有 `received[TCP_RECVWN_SIZE]`。后续分别扩展 `rwnd/cwnd/ssthresh`、`send_base/next_seq`、未确认队列、重复 ACK/RTO 状态，以及 `recv_next`、失序队列、SACK scoreboard 和可用空间。
- `sending_buf`、`received_buf`：应用发送数据和已接收数据的线性缓存。当前接收路径按到达顺序追加，尚无失序重组、重复抑制和容量边界处理；后续应由序号驱动入队并只向应用交付连续数据。
- `listen_socks[MAX_SOCK]`、`established_socks[MAX_SOCK]`（`inc/kernel.h`）：内核模拟层按本地/远端 IP 与端口四元组散列查找监听连接或已建立连接；`startSimulation()` 初始化数组，`onTCPPocket()` 按“已建立连接优先、监听连接其次、否则丢弃”分发报文。
- `sent_time`、连接级定时器和 trace：后续为每个未确认段记录发送时间，用于 RFC 6298 的 RTT/RTO，以及 RACK-TLP 的基于时间丢包判断；当前代码只保留 `sent_time` 字段，没有运行中的 RTO、重传或 TLP 定时器。

挑战任务的状态不直接塞入固定 20 字节头：SACK 通过课程允许的扩展区编码协商标志和 SACK block，RACK-TLP 复用逐报文发送时间并增加探测标识，CUBIC 的 `W_max`、`K`、epoch 等属于发送端本地拥塞控制状态。扩展区必须计入 `hlen/plen`，并纳入上述 checksum 覆盖范围；未知类型、长度越界或与 `hlen/plen` 不一致时丢弃或按协议记录后忽略。

### 4.3 状态机、序号与窗口协同

#### 4.3.1 连接状态机

```text
CLOSED --bind/listen--> LISTEN
CLOSED --connect / send SYN--> SYN_SENT
LISTEN --receive SYN / send SYN-ACK--> SYN_RECV
SYN_SENT --receive SYN-ACK / send ACK--> ESTABLISHED
SYN_RECV --receive ACK--> ESTABLISHED

ESTABLISHED --application close / send FIN--> FIN_WAIT_1
FIN_WAIT_1 --receive ACK--> FIN_WAIT_2
FIN_WAIT_1 --simultaneous FIN--> CLOSING --receive ACK--> TIME_WAIT
FIN_WAIT_2 --receive FIN / send ACK--> TIME_WAIT
TIME_WAIT --2×TJU_MSL--> CLOSED

ESTABLISHED --receive FIN / send ACK--> CLOSE_WAIT
CLOSE_WAIT --application close / send FIN--> LAST_ACK
LAST_ACK --receive ACK--> CLOSED
```

`tju_tcp_t.state` 保存上述连接状态。握手完成并进入 `ESTABLISHED` 前，应用数据不进入正常发送路径；关闭阶段仍需先处理发送缓存中已经提交的数据，再发送 FIN。ISN 不使用固定常量，SYN 和 FIN 各占用一个序号，ACK 表示对端下一个期望序号。握手和 FIN 报文按 RTO 重传，主动关闭方在 `TIME_WAIT` 等待 `2×TJU_MSL` 后释放连接。当前基线的 `tju_connect()`、`tju_accept()` 会直接设置 `ESTABLISHED`，上述状态迁移属于后续实现设计。

#### 4.3.2 发送窗口和接收窗口

```text
send_right_edge = send_base + min(rwnd, cwnd)
FlightSize = 已发送但尚未累计确认的数据字节数
可发送数据量 = max(0, send_right_edge - next_seq)
```

发送端维护 `send_base`（最早未累计确认的序号）和 `next_seq`（下一个待发送序号）。接收端根据 `received_buf` 和失序缓存的剩余空间计算 `rwnd`，并在 ACK 中通过 `advertised_window` 通告；发送端的 `cwnd` 由基础 Reno 或挑战算法维护。实际发送上限为 `effective_window=min(rwnd,cwnd)`，`FlightSize` 统计已经发出但尚未累计确认的数据字节，只有 `FlightSize < effective_window` 时才继续分段发送。这样，`rwnd` 限制接收缓存占用，`cwnd` 限制网络中的在途数据，`FlightSize` 反映当前已消耗的窗口。

发送窗口由 `[send_base, send_base+effective_window)` 表示；新 ACK 到达后，`send_base` 向右移动并释放已确认的发送缓存，窗口随之滑动。接收端的 `recv_next` 是下一个按序期待的序号，按序数据进入应用接收缓存后，缓存占用减少、`rwnd` 增大；失序数据暂存于失序队列，不直接交付应用。窗口缩小时，发送端保留已经发出的数据，新的发送右边界采用最新通告值。`rwnd=0` 时暂停普通数据发送，并在 RTO 后发送零窗口探测，直到收到窗口恢复 ACK。

#### 4.3.3 ACK 和接收处理

```text
收到报文
  -> 检查长度、扩展区和 checksum
  -> 根据四元组找到 tju_tcp_t 并检查 state
  -> 处理 ACK：确认发送队列、更新 send_base/FlightSize/cwnd
  -> 处理 payload：按序交付，失序缓存，重复/重叠去重
  -> 更新 recv_next、received_buf 和 advertised_window(rwnd)
  -> 发送累计 ACK，唤醒等待中的 tju_recv()
```

累计 ACK 只能推进到连续按序字节的末尾；重复、失序和重叠数据只影响缓存和 ACK，不向应用重复交付。ACK 推进时释放已经确认的发送段，更新 `FlightSize`，并重启或停止最早未确认段的定时器。`cwnd` 在新 ACK、重复 ACK 和超时事件中按当前拥塞控制算法更新，`rwnd` 只由接收缓存可用空间决定。

#### 4.3.4 发送与接收伪代码

```text
send(sock, data):
    lock send_lock
    append data to sending_buf
    effective_window = min(rwnd, cwnd)
    while available_data > 0 and FlightSize < effective_window:
        n = min(MAX_DLEN, available_data, effective_window - FlightSize)
        if n == 0:
            break
        segment = make_segment(seq=next_seq, payload=data[0:n])
        put segment into unacked_queue
        transmit segment
        next_seq += n
        FlightSize += n
        start timer for the oldest unconfirmed segment if needed
    unlock send_lock

on_ack(ack):
    lock send_lock
    if send_base < ack <= next_seq:
        remove and free segments covered by ack
        FlightSize -= newly_confirmed_bytes
        send_base = ack
        update cwnd according to current algorithm
        restart timer, or stop it when unacked_queue is empty
    else if ack == send_base:
        count duplicate ACK and apply fast-retransmit rules when triggered
    unlock send_lock

on_data(segment):
    lock recv_lock
    if segment.seq == recv_next:
        append segment payload and advance recv_next
        merge any now-contiguous out-of-order segments
    else if segment.seq is ahead of recv_next:
        insert into out-of-order queue
    update rwnd from remaining receive-buffer space
    unlock recv_lock
    send cumulative ACK(recv_next, rwnd)
```

当前基线的 `tju_send()` 直接发送固定序号报文，`tju_handle_packet()` 按到达顺序追加 payload，尚未维护 `send_base`、`next_seq`、`recv_next`、`rwnd`、`cwnd` 和 `FlightSize`；本节流程是后续可靠传输、流量控制和拥塞控制的实现依据。

### 4.4 并发控制、定时器与异常处理

TJU_TCP 的运行模型包括应用线程和模拟内核接收线程。应用线程执行 `client.c` 或 `server.c` 中的 socket API，模拟内核的 `receive_thread()` 持续监听 UDP 端口 `20218`，收到完整报文后交给 `onTCPPocket()`，再由连接对应的 `tju_tcp_t` 处理。发送路径使用 `send_lock` 保护发送缓存、未确认报文队列、序号、窗口和拥塞控制状态，接收路径使用 `recv_lock` 保护接收缓存、失序报文、`recv_next` 和接收窗口。`tju_recv()` 在接收缓存为空时应与 `recv_lock` 配合调用 `pthread_cond_wait()`，接收线程写入按序数据后更新条件变量并唤醒等待线程。监听 socket 表和已建立 socket 表的注册、查找、删除也需要单独的内核表锁，并固定锁的获取顺序，避免多个线程交叉加锁造成死锁。当前基线虽然初始化了两个互斥锁和一个条件变量，但 `tju_send()` 尚未保护发送路径，`tju_recv()` 仍采用忙等，socket 哈希表也没有专用同步机制，因此这些并发控制内容属于后续实现要求。

可靠传输采用每条连接一个最早未确认报文的重传计时器。首个未确认报文发送时启动计时器，累计 ACK 推进 `send_base` 后重新计时，所有报文确认后停止计时器。初始和后续 RTO 按 RFC 6298 根据 `SRTT`、`RTTVAR` 和时钟粒度计算，重传报文不参与 RTT 采样；RTO 到期后重传最早未确认报文并进行指数退避，三次重复 ACK 则触发快速重传。基础 Reno 使用 `cwnd`、`ssthresh` 和 `FlightSize` 调整发送速率，完整 Reno、NewReno、SACK、RACK-TLP 和 CUBIC 在后续阶段复用可靠传输和 ACK 路径，但分别维护自己的恢复、记分板、发送时间或窗口函数状态。

连接正常关闭或异常退出时，协议应先停止接收新数据并标记连接不可用，再取消和回收定时器，等待正在执行的定时器回调结束，从监听表或已建立连接表中删除 socket，释放未确认队列、失序队列、`sending_buf`、`received_buf` 和窗口对象，最后销毁互斥锁、条件变量并释放 `tju_tcp_t`。临时报文缓冲区由创建者和发送路径明确管理，在发送完成、发送失败或接收处理结束后释放；内存、锁或定时器创建失败时，保留已有数据和连接状态，释放本次已获取的资源并返回错误，不能继续使用半初始化对象。

异常报文必须在进入状态机和访问 payload 前处理。发生丢包时，发送端在 RTO 超时或满足快速重传条件后重传最早未确认报文；收到重复报文或没有推进 `send_base` 的重复 ACK 时，不重复交付数据，重新发送当前累计 ACK，并统计重复 ACK。序号大于 `recv_next` 的失序报文暂存在失序队列，等缺失数据到达后合并并按序交付；与已有数据重叠的报文只保留尚未接收的字节。报文长度不满足 `20 <= hlen <= plen <= MAX_LEN`、四元组找不到连接或当前状态不允许该报文时，直接丢弃并记录原因。后续实现 checksum 后，校验范围覆盖 TJU_TCP 头、扩展区和 payload，校验失败的报文不推进序号、不发送数据确认、不改变窗口。接收缓存无可用空间时通告 `rwnd=0`，发送端暂停普通数据并在探测计时器到期后发送零窗口探测，收到正窗口 ACK 后恢复发送。当前基线的 `tju_close()` 直接返回，`sent_time` 也只是预留字段，尚未实现 FIN、TIME-WAIT、RTO、重传、校验和及上述完整异常处理；这些内容作为第二阶段可靠传输和流量控制的设计与验收依据。

### 4.5 分阶段实现计划与测试策略

#### 表 4-1 分阶段实现与测试策略

| 阶段/模块 | 实现过程的安排 | 测试环境设置 | 预期结果 | 主要风险与应对 |
|---|---|---|---|---|
| 第一阶段：环境、框架分析与总体设计 | 不修改 `src/`、`inc/`、`test/`；完成环境核对、基线运行、源码分析、RFC 映射、总体设计和测试方案 | client `172.17.0.2`、server `172.17.0.3`，`enp0s8`，`100 Mbps/20 ms/0% loss`，抓取 UDP `20218` | 编译成功，双方各收到两条消息，pcap 可读并记录 4 个 UDP 报文；只确认基线功能 | 保存构建、运行、qdisc、ping 和 pcap；先启动抓包再运行程序 |
| 第二阶段：连接、可靠传输与流量控制 | 实现三次握手、FIN 状态机、分段与重组、序号/累计 ACK、checksum、RTT/RTO、重传、`rwnd` 和零窗口探测 | 两台 VM：client `172.17.0.2`、server `172.17.0.3`；网卡 `enp0s8`；正常组 `100Mbps/20ms/0ms/0%`，恢复组 `100Mbps/20ms/0ms/1%`，时延组 `100Mbps/100ms/0ms/0%`，低带宽组 `10Mbps/20ms/0ms/0%`；两端均配置，每组 3 次；指定报文单次丢失由测试代理完成 | 连接可建立和关闭，数据按序且只交付一次，丢包可恢复，窗口可滑动并能从零窗口恢复 | 每次只改变一个参数，重点检查竞态、ACK 提前推进、重传 RTT 采样和内存释放 |
| 第三阶段：基础 Reno 与性能 | 实现慢启动、拥塞避免、重复 ACK 和 RTO 响应，记录 `cwnd`、`ssthresh`、`rwnd` 和 `FlightSize` | 固定代码版本和 `100MB` 测试数据，每组运行 `60s`、重复 `3` 次；基准组 `100Mbps/20ms/0ms/0%`，带宽组 `10Mbps/20ms/0ms/0%`，时延组 `100Mbps/100ms/0ms/0%`，丢包组 `100Mbps/20ms/0ms/1%`；参数顺序为带宽/单向时延/抖动/丢包率 | trace、吞吐率和完成时间可复现，基础 Reno 回归稳定 | 每组只改变一个网络变量，区分 `rwnd` 与 `cwnd`，保存原始参数、日志、抓包和 trace |
| 挑战任务 1：完整 Reno 快速恢复 | 依据 RFC 5681 §3.2 实现快速重传、窗口膨胀和恢复 ACK 收缩 | 单窗口丢包，记录重复 ACK、恢复状态和窗口 | 验证快速恢复过程，基础 Reno 回归不变 | 冻结基础 Reno，独立分支测试 |
| 挑战任务 2：NewReno | 依据 RFC 6582 §3.2 实现 `recover` 和部分确认处理 | 关闭 SACK，构造一个窗口内两个丢包 | 部分 ACK 不提前退出恢复，数据最终按序交付 | 独立 NewReno trace，避免与 SACK 混淆 |
| 挑战任务 3：SACK 及 SACK 丢包恢复 | 依据 RFC 2018/6675 实现 SACK 协商、scoreboard 和保守重传 | 构造非连续多报文丢失，记录 SACK block 和重传选择 | 只重传缺失段，数据按序交付 | 检查选项长度、序号范围和 scoreboard 更新 |
| 挑战任务 4：RACK-TLP | 依据 RFC 8985 实现逐报文发送时间、RACK 判定和 Tail Loss Probe | 测试尾部丢包、ACK 延迟和可控重排序 | TLP 先探测，达到时间条件后再判定丢失 | 使用单调时钟并限制探测次数 |
| 挑战任务 5：CUBIC | 依据 RFC 9438 实现 CUBIC 窗口增长、丢包响应和 TCP-friendly 区域 | 测试无丢包增长、单次丢包和不同 RTT | 对比 CUBIC 与 Reno 的窗口和吞吐差异 | 固定 SMSS、最小 RTT 和 epoch 起点，核对参数单位 |

第一阶段提交内容包括环境与基线记录、架构分析、需求追踪表、协议设计文档 V1、Git 版本、人工复核摘要、未解决问题和第二阶段计划。

### 挑战任务实施说明

本项目五项挑战全部选择，依据分别为 RFC 5681、RFC 6582、RFC 2018/6675、RFC 8985 和 RFC 9438。实施顺序为：先完成并冻结基础 Reno，再完成完整 Reno 快速恢复；随后在关闭 SACK 的配置下完成 NewReno；再实现 SACK 协商和 RFC 6675 记分板；SACK 通过后加入 RACK-TLP 的发送时间与尾部探测；最后实现 CUBIC，并与 Reno 做相同网络条件下的窗口和吞吐对比。每项挑战均使用独立分支、独立测试开关、原始 trace 和回归结果，挑战结果不覆盖基础 Reno 基线。

### 4.6 第一阶段 AI 工具使用与人工验证

本阶段使用 AI 工具辅助整理源码调用链、RFC 章节映射和测试计划，输出仅作为待核实建议。一次实质性复核中，初步建议把 RFC 9293 `§3.10` 标注为 TCP 状态机；人工查阅 RFC Editor 原文后更正为：状态机概览对应 `§3.3.2 State Machine Overview`，`§3.9` 为 `Interfaces`，`§3.10` 为 `Event Processing`，并手动同步修改 2.1 表格。随后人工对照 `src/tju_tcp.c`、`inc/tju_packet.h`、`src/tju_packet.c` 和 `src/kernel.c`，再用构建日志、两端运行日志、网络参数和 `baseline.pcap` 交叉验证，确认当前只完成基线报文编码和 UDP 双向收发，未把握手、重传、RTO、窗口控制或挑战功能误记为已实现。详细证据索引见 `review-notes.md`。

# TJU_TCP 基线架构分析

## 1. 构建关系

`Makefile` 将 `src/tju_packet.c`、`src/kernel.c`、`src/tju_tcp.c` 编译为 `build/*.o`，再分别与 `src/server.c`、`src/client.c` 链接为 `server` 和 `client`。公共接口位于 `inc/tju_tcp.h`、`inc/tju_packet.h`、`inc/kernel.h`，公共状态和数据结构位于 `inc/global.h`。

```text
src/client.c / src/server.c
             |
       tju_tcp API (tju_tcp.c)
             |
    packet encode/decode (tju_packet.c)
             |
       UDP simulator (kernel.c)
             |
       UDP port 20218
```

## 2. 程序入口和启动

客户端入口 `src/client.c:5`：

1. `startSimulation()` 创建 UDP socket 和接收线程；
2. `tju_socket()` 创建初始为 `CLOSED` 的控制块；
3. 构造 server `172.17.0.3:1234`；
4. `tju_connect()` 后发送两段数据并调用 `tju_recv()`。

服务端入口 `src/server.c:4`：

1. `startSimulation()` 初始化 UDP 承载；
2. `tju_socket()`、`tju_bind(172.17.0.3:1234)`、`tju_listen()`；
3. `tju_accept()` 获取连接控制块；
4. 发送两段数据并调用 `tju_recv()`。

## 3. 主要接口调用关系

```text
server.c -> startSimulation -> tju_socket -> tju_bind -> tju_listen
         -> tju_accept -> tju_send / tju_recv

client.c -> startSimulation -> tju_socket -> tju_connect
         -> tju_send / tju_recv
```

公共 API 在 `inc/tju_tcp.h` 中声明：`tju_socket`、`tju_bind`、`tju_listen`、`tju_accept`、`tju_connect`、`tju_send`、`tju_recv`、`tju_close`。

## 4. 报文发送路径

`tju_send()`（`src/tju_tcp.c:121`）复制应用数据，使用 `create_packet_buf()` 组装 20 字节自定义头部和数据，再调用 `sendToLayer3()`。`sendToLayer3()`（`src/kernel.c:51`）根据主机名选择对端地址，通过 UDP `sendto()` 发往端口 `20218`。

当前基线发送字段需要在后续阶段修正和验证：数据发送使用固定 `seq=464`，未实现可靠发送队列、累计确认或窗口推进。

## 5. 报文接收路径

```text
UDP recvfrom (kernel.c:96)
  -> receive_thread assembles plen bytes
  -> onTCPPocket (kernel.c:5)
  -> calculate 4-tuple hash
  -> tju_handle_packet (tju_tcp.c:194)
  -> append payload to received_buf
  -> tju_recv copies application bytes
```

`onTCPPocket()` 使用源/目的端口、主机名派生 IP 和 `cal_hash()` 查找已建立或监听 socket。`tju_handle_packet()` 当前仅把 payload 追加到接收缓冲区。

## 6. 报文格式

`inc/tju_packet.h` 定义固定 20 字节头部：

| 偏移 | 字段 | 长度 |
|---:|---|---:|
| 0 | source_port | 2 |
| 2 | destination_port | 2 |
| 4 | seq_num | 4 |
| 8 | ack_num | 4 |
| 12 | hlen | 2 |
| 14 | plen | 2 |
| 16 | flags | 1 |
| 17 | advertised_window | 2 |
| 19 | ext | 1 |

`header_in_char()` 使用网络字节序写入字段；`get_*()` 使用网络字节序读取字段。`MAX_LEN=1400`、`MAX_DLEN=1375`，设计和测试必须避免 UDP/IP 分片。

## 7. 关键数据结构生命周期

- `tju_tcp_t`：`tju_socket()` 分配，保存地址、状态、收发缓冲区和窗口指针；后续阶段由连接建立、关闭和资源释放管理其生命周期。
- `sender_window_t` / `receiver_window_t`：挂在 `window_t` 中，当前基线只初始化指针，可靠传输阶段补充窗口、序号和拥塞状态。
- `received_buf`：由 `tju_handle_packet()` 分配/扩展，由 `tju_recv()` 消费并释放。
- `BACKEND_UDPSOCKET_ID`：`startSimulation()` 创建并绑定 `20218`，接收线程持续调用 `recvfrom()`。

## 8. 并发和同步

基线接收线程与主线程共享 `tju_tcp_t`。`send_lock`、`recv_lock`、`wait_cond` 已在结构中定义，但现有 `tju_recv()` 的等待逻辑仍是忙等，后续实现应改为条件变量、明确锁范围并验证无竞态、死锁和资源泄漏。

## 9. 状态和实现边界

`global.h` 预定义 `CLOSED`、`LISTEN`、`SYN_SENT`、`SYN_RECV`、`ESTABLISHED`、FIN 相关状态，以及 `SLOW_START`、`CONGESTION_AVOIDANCE`、`FAST_RECOVERY`。基线 `tju_connect()` 直接将状态设置为 `ESTABLISHED`，`tju_accept()` 直接构造已建立连接；因此三次握手、关闭状态机、重传、流量控制和 Reno 均是后续阶段任务。

## 10. 运行证据

- `baseline-client.log` 和 `baseline-server.log` 均包含两条正确回显。
- `baseline.pcap` 过滤 `udp port 20218` 得到双向各 2 个报文。
- `baseline-build.log` 记录了完整编译命令。


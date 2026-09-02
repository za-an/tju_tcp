# Phase 1 Baseline Evidence

本目录保存第一阶段“环境、框架分析与总体设计”的可追溯材料。所有运行数据来自课程提供的两台 Vagrant 虚拟机；没有修改 `src/`、`inc/` 或 `test/` 中的协议实现。

## 文件索引

| 文件 | 内容 | 来源 |
|---|---|---|
| `environment-client.txt` | client VM 的系统、地址、路由、工具版本 | `vagrant ssh client` |
| `environment-server.txt` | server VM 的系统、地址、路由、工具版本 | `vagrant ssh server` |
| `network-client.txt` | client 的基线 qdisc 和到 server 的 10 次 ping | `tc -s qdisc`、`ping` |
| `network-server.txt` | server 的基线 qdisc 和到 client 的 10 次 ping | `tc -s qdisc`、`ping` |
| `baseline-build.log` | `make clean && make` 的完整输出 | server VM |
| `baseline-client.log` | client 基线程序输出 | `./client` |
| `baseline-server.log` | server 基线程序输出 | `./server` |
| `baseline.pcap` | 过滤 `udp port 20218` 的原始抓包 | server VM `tcpdump` |
| `baseline-packets.txt` | pcap 的可读摘要 | `tcpdump -nn -tttt -r` |
| `architecture.md` | 源码架构和调用关系分析 | 源码核对与运行验证 |
| `requirements-traceability.md` | 课程要求、设计、测试和状态追踪 | 指导书与源码核对 |
| `design-v1.md` | 协议总体设计和后续实现测试计划 | 第一阶段设计版本 |
| `ai-validation-log.md` | AI 协作与人工核验记录模板 | 人工填写/更新 |
| `unresolved-issues.md` | 基线缺陷、范围裁剪和第二阶段待办 | 源码与测试发现 |

## 基线配置

- client：`172.17.0.2`，server：`172.17.0.3`。
- 通信网卡：`enp0s8`。
- TJU_TCP 应用端口：`1234`；UDP 承载端口：`20218`。
- 两端 qdisc：`100 Mbps`、单向 `20 ms` 延迟、`0 ms` 抖动、`0%` 丢包。
- Git 基线：`37fbd090baf2e5df8767edd43fc225a777b5e5e4`。

## 验证结论

- `make clean && make` 成功，生成 `server` 和 `client`。
- client 和 server 均输出 `hello world`、`hello tju`。
- pcap 捕获 4 个 UDP 报文，`4 packets received by filter`、`0 packets dropped by kernel`。
- 基线 qdisc 的 ping 结果约为 40 ms RTT；原始测量值见 `network-client.txt` 和 `network-server.txt`。
- 当前 `src/` 是教学基线骨架；本阶段只记录其行为，不把未实现的 TCP 机制写成已完成。


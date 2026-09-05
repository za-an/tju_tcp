# 人工复核摘要

本文件只保留第一阶段一项有实质意义的复核记录。

| 项目 | 复核内容 |
|---|---|
| 使用目的 | 辅助整理源码调用链、RFC 章节映射和测试计划。 |
| 发现的问题 | 初步建议将 RFC 9293 `§3.10` 标注为 TCP 状态机。 |
| 人工处理 | 查阅 RFC Editor 原文后改为：`§3.3.2 State Machine Overview` 对应状态机概览，`§3.9` 对应 `Interfaces`，`§3.10` 对应 `Event Processing`，并同步修正 2.1 标准依据表。 |
| 代码与实验核对 | 检查 `src/tju_tcp.c`、`inc/tju_packet.h`、`src/tju_packet.c`、`src/kernel.c`，并核对构建日志、两端运行日志、网络参数和 `baseline.pcap`。 |
| 复核结论 | 当前基线已验证 20 字节报文编码、UDP `20218` 承载和双向收发；握手、重传、RTO、窗口控制及五项挑战均保留为后续实现，未将设计内容写成已完成功能。 |

## 证据索引

- RFC 章节和课程要求：`report-content.md`、`requirements-traceability.md`。
- 源码结构与调用关系：`architecture.md`、`design-v1.md`。
- 构建、运行和网络证据：`baseline-build.log`、`baseline-client.log`、`baseline-server.log`、`network-client.txt`、`network-server.txt`、`baseline.pcap`、`baseline-packets.txt`。

# 第一阶段环境与基线摘要

## 虚拟机和网络

| 角色 | 主机名 | 私有地址 | 通信网卡 |
|---|---|---|---|
| 客户端 | `client` | `172.17.0.2/16` | `enp0s8` |
| 服务端 | `server` | `172.17.0.3/16` | `enp0s8` |

两台 VM 由项目根目录的 `Vagrantfile` 创建，使用同一 VirtualBox internal network。到对端地址的路由均经 `enp0s8`。

## 工具版本

- Ubuntu 内核：`5.4.0-80-generic`，x86_64。
- GCC：`9.3.0`。
- GNU Make：`4.2.1`。
- Git：`2.25.1`。
- Python：`3.8.10`。
- tcconfig：`/usr/local/bin/tcset`。
- tcpdump：`/usr/sbin/tcpdump`。

完整命令输出见 `environment-client.txt` 和 `environment-server.txt`。

## 基线参数

两端 `enp0s8` 均设置：

```bash
sudo tcset enp0s8 --rate 100Mbps --delay 20ms --delay-distro 0 --loss 0% --overwrite
```

含义：100 Mbps 带宽、单向 20 ms 延迟、0 ms 抖动、0% 丢包。两端均配置时，额外 RTT 约为 40 ms。qdisc 统计见 `network-client.txt` 和 `network-server.txt`。

## 编译和运行

在 `/vagrant/tju_tcp` 执行 `make clean && make` 成功，生成 `server` 和 `client`；完整输出见 `baseline-build.log`。

并行启动服务端和客户端后：

- `baseline-server.log`：收到 `hello world` 和 `hello tju`；
- `baseline-client.log`：收到 `hello world` 和 `hello tju`；
- `baseline.pcap`：过滤 `udp port 20218`，捕获双向共 4 个 UDP 报文；
- `baseline-packets.txt`：pcap 可读摘要，显示 client/server 的 `20218` 端口通信。

tcpdump 统计：`4 packets captured`、`4 packets received by filter`、`0 packets dropped by kernel`。

## 基线 Git 和构建产物

- Git HEAD：`37fbd090baf2e5df8767edd43fc225a777b5e5e4`。
- 该阶段未修改 `src/`、`inc/` 或 `test/` 中的协议实现。
- `server` 和 `client` 为带调试信息的 x86-64 ELF 可执行文件。

## 解释边界

当前程序是教学基线骨架：它能够完成 UDP 承载下的最小双向数据交换，但 `tju_connect()`、`tju_accept()`、可靠传输、流量控制和拥塞控制仍需按阶段计划实现。因此本文件只将“编译、运行、UDP 通信和抓包”标记为基线已验证。


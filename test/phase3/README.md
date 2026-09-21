# 第三阶段传输与性能实验

这些工具只记录真实运行结果，不包含预生成的实验成绩。`off` 用于第二阶段可靠传输基线（不启用拥塞窗口）；基础模式为 `TJU_CC=basic`，挑战模式为 `reno`（完整快速恢复）、`newreno`（部分 ACK 恢复）和 `cubic`（CUBIC 窗口增长）。

## 构建与单次实验

可在实际代码仓库根目录安装宿主机依赖到本地忽略目录，避免修改全局 Python 环境：

```powershell
python -m pip install --target test/phase3/.deps paramiko
```

运行脚本会自动识别 `.deps`。绘图使用宿主机已安装的 matplotlib；完整依赖范围见 `requirements.txt`。

先从包含 Vagrantfile 的目录启动 `client` 和 `server` 两台虚拟机。宿主机 Python 需要 `paramiko`；绘图需要 `matplotlib`。SSH 默认 `127.0.0.1:2222` / `:2200`，账号与密码均为 `vagrant`；可通过 `--client-port` / `--server-port` 等参数调整。脚本不会启动虚拟机。

```powershell
python .\tju_tcp\test\phase3\run_experiments.py --build --bytes 1048576 --timeout 90
python .\tju_tcp\test\phase3\run_experiments.py --bytes 104857600 --timeout 600
python .\tju_tcp\test\phase3\analyze.py .\tju_tcp\test\phase3\results
```

`--build` 在 client VM 上顺序执行项目 `make` 和本目录 `make`；两台 VM 共享编译结果。已有编译结果时省略该选项。默认单次运行**不修改网络配置**。默认传输量为 100 MiB，代码按块发送，控制待确认队列大小，接收端逐字节核对由绝对偏移决定的数据，并比较两端 FNV-1a 校验值。FNV-1a 是辅助诊断值，不是密码学散列。

客户端等待全部数据得到确认后主动关闭，服务端读到 EOF 后关闭；两端都等待 `CLOSED` 才报告成功，客户端包含 TIME_WAIT 等待。应用吞吐率以数据传输时间计算，不计关闭等待。服务端可另用 `--output FILE` 保存收到的数据。默认 600 秒总超时；100 MiB 在高延时/丢包下可能需要更长的 `--timeout`，不能把超时当作成功实验。

每次运行产生唯一目录，包含 `metadata.json`、两端 stdout/stderr、两端原始 `.trace`。元数据包含代码版本、工作区状态、网络配置、返回值、字节数、校验值和应用吞吐率。虚拟机内原始 trace 另保留于 `/tmp/tju-phase3-<run_id>/`。不同 VM 的时钟不保证同步，绘图分别采用各 trace 自己的起点。

## 零窗口与挑战模式

```powershell
python .\tju_tcp\test\phase3\run_experiments.py --mode reno --bytes 1048576 --timeout 90
python .\tju_tcp\test\phase3\run_experiments.py --mode newreno --bytes 1048576 --recv-capacity 2750 --start-delay-ms 3000 --timeout 120
```

服务端先在接收锁下设置 `recv_capacity`，再发应用就绪字节；客户端收到就绪字节后才发送数据。`--start-delay-ms` 故意暂停读取，`--read-delay-ms` 在每次读取之后暂停。设置 `recv_capacity` 时应检查本次 trace 中确实出现 rwnd=0、探测及窗口恢复，不能只凭程序成功就宣称覆盖了零窗口。

## 网络变量与重复实验

```powershell
# 单次明确修改网络：两侧出口各 20 ms、1% 丢包、100 Mbit/s
python .\tju_tcp\test\phase3\run_experiments.py --mode newreno --loss 1 --delay-ms 20 --rate-mbps 100 --timeout 1800

# 分别改变丢包率和时延，默认每组 3 次；默认比较三个模式
python .\tju_tcp\test\phase3\run_experiments.py --matrix --timeout 3600

# 较小规模，用来验证脚本；不能代替 100 MiB 可靠传输验收
python .\tju_tcp\test\phase3\run_experiments.py --matrix --bytes 1048576 --modes basic newreno --loss-values 0 1 --delay-values 10 20 --timeout 120
```

`--matrix` 或任一 `--loss` / `--delay-ms` / `--rate-mbps` 表示明确选择修改两台 VM 的 `enp0s8` 出口队列，脚本调用课程镜像里的 `sudo -n tcset`。每次修改前保存 qdisc 信息；正常结束、报错或 Ctrl+C 时均在 `finally` 中恢复已尝试修改的 VM 到**已知基线**：100 Mbit/s、每侧 20 ms、0% 丢包。可以用 `--baseline-*` 指定另一基线。这是恢复指定基线，并非恢复任意原始 qdisc。只测试课程虚拟机的 `enp0s8`，不要将 `--interface` 指向其他业务接口。

时延值是**每侧出口时延**，基线 RTT 约为两倍；丢包同样作用于数据和 ACK 两个方向。矩阵的丢包实验固定时延，时延实验固定丢包率。默认每组 3 次；失败组会被保留并立即停止，`--continue-on-failure` 可继续。脚本只终止自己记录 PID 且 `/proc/PID/exe` 匹配的进程，不使用 `pkill`，也不干扰已经占用 UDP 20218 的测试。SSH 默认仅为本机临时 Vagrant VM 接受未知 host key；需要严格校验时传 `--known-hosts`。

## 结果解释

`analyze.py` 输出 `runs.csv`、`summary.json`、吞吐率对比图和每份 trace 的状态图。只对成功实验计算平均值，明确记录失败数量；标准差需要至少两次成功运行，每组是否达到三次会单独标记。协议 trace 使用实际 `CC` 的 `cwnd`、`ssthresh`、对端 `rwnd`、`flight`，并标出实际记录的 RTO/快速重传/部分 ACK 恢复事件。

`DELV` 表示数据交付到 TCP 接收缓存，按固定时间箱计算有效数据速率；它与应用 `recv` 的时刻不同。以首末 DELV 计算的速率会排除首次交付前的等待，不能替代元数据中的应用传输吞吐率。SEND 速率包含重传。重复起始序号计数只作为重传估计，不能视为真实网络丢包数。

随机 `tc` 丢包不能保证某个具体 Reno 分支被触发。判断 RTO、三次重复 ACK、NewReno 部分 ACK 是否得到覆盖，必须检查 CC 原始事件或使用独立的确定性协议测试。脚本不会把随机丢包实验自动标成这些分支的通过证明。

# 第三阶段验证记录（2026-09-17）

## 最终源码验证

- 课程 Ubuntu 20.04 / GCC 9.3.0 两台 Vagrant 虚拟机编译通过。
- `make -C test check`：15 组控制器 + 15 组传输集成测试全部通过。
- 同一传输测试的 AddressSanitizer / UndefinedBehaviorSanitizer 运行通过，无诊断；完整输出见 `validation.log`。
- Python 测试与绘图脚本语法检查通过；真实 trace 的统计及绘图调用通过，代表性窗口图已目视检查。

## 最终源码 100 MiB 丢包传输

- 原始目录：`../../test/phase3/results/20260917T042706.198281Z_newreno_e48851f8`。
- 模式 NewReno，SMSS 1380；两侧出口各 5 ms、0.1% 丢包、100 Mbit/s。
- 接收 104,857,600 字节，逐字节校验通过；两端 FNV-1a 为 `4e771e4e25502bf4`，双方正常关闭。
- 接收端应用耗时 28.637 秒，goodput 29.293 Mbit/s；不含关闭等待。
- 实际 trace 记录快速重传 69 次、NewReno 部分 ACK 恢复 3 次；两端 trace 均无格式错误。
- 已逐项比较元数据记录的源文件 SHA-256 与当前工作区，完全一致。

## 阶段联调对照实验

- 共 36 组，成功 36 组。每组传输 1 MiB。
- 三种模式 basic / reno / newreno；分别改变双向丢包率 0%、1%，以及每侧出口时延 5、20 ms；每种配置重复 3 次。
- 丢包实验固定每侧 20 ms、100 Mbit/s；时延实验固定 0% 丢包、100 Mbit/s。
- 原始数据位于 `../../test/phase3/results/`；汇总为 `runs.csv`、`summary.json`，对比图为 `comparison_1_loss.png`、`comparison_2_delay_ms.png`。
- 对照实验来自阶段联调版本；其后仅修正 ACK/窗口快照一致性、长零窗口关闭等待和 CWND 事件类型。每次实验均保存源码版本信息，最终源码另以上述 100 MiB 和回归测试验证；不把不同版本混写成同一版本。
- 早期的 1375 SMSS 测试结果保留在原目录，元数据中可区分；没有覆盖或改写旧结果。

## 环境与未执行项目

- 网络实验结束后恢复两侧 enp0s8 到 100 Mbit/s、每侧 20 ms、0% 丢包；恢复输出保存在各次 metadata.json。
- 此次未运行课程线上评分平台，未编写最终课程报告或答辩材料。
- `results/` 被 Git 忽略以免默认提交大量 trace；正式交付时需按课程要求另外打包原始实验材料。
- 当前分支 phase3，未自动 Git 提交。原有第二阶段工作区修改被保留。

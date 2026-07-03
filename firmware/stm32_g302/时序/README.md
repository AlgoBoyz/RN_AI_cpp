# 真鼠标 1000 Hz baseline 时序分析

本目录用真鼠标抓包数据校准 Stm_g302 固件的 `0xFE` mouse-move 注入路径，
为 M4-3「反作弊可观测特征」提供量化基线。

## 文件

| 文件 | 说明 |
|---|---|
| `analyze_polling.py` | 活动流分析（间隔分布 / 速度 / jerk / 方向） |
| `analyze_idle.py` | 静止流专项分析（不发包 / burst 结构） |
| `report.md` | 活动流完整报告（11 节，含校准目标对照表） |
| `idle_findings.md` | 静止流结论 |
| `console_output.txt` / `idle_console.txt` | 控制台原始输出 |
| `dt_histogram.csv` 等 | 直方图数据 |

## pcapng 不进 repo

`.gitignore` 顶层屏蔽 `*.pcapng`。两份原始抓包：

| 文件 | 大小 | 内容 |
|---|---|---|
| `respond_1000hz.pcapng` | ~11 MB | 72 s 真鼠标 1000 Hz 主动移动 + 转圈（三挡速度） |
| `idle_1000hz.pcapng` | ~25 MB | 67.8 s 真鼠标完全静置 |

需要重生数据：
- 真鼠标 VID 0xB58E PID 0x9E84
- 用 USBPcap 抓 60-90 s
- 放回本目录文件名一致即可重跑两个脚本

## 重跑分析

```bash
# 活动流（需要先有 respond_1000hz.pcapng）
python3 analyze_polling.py

# 静止流（需要先有 idle_1000hz.pcapng）
python3 analyze_idle.py
```

脚本依赖：
- Python 3.8+（仅 stdlib）
- Wireshark/tshark（默认查找 `C:\Program Files\Wireshark\tshark.exe`）

也可显式传 pcapng 路径：`python3 analyze_polling.py path/to/other.pcapng`

## 核心结论速览

| 维度 | baseline | 对注入路径的要求 |
|---|---|---|
| 稳态间隔 | 双峰 980 / 1015 µs，σ=17 µs | 1 ms deadline + σ≈15-20 µs jitter |
| 跳帧 (2-3 ms) | ~6% | 偶发拒发 / 故意延迟 |
| 长尾 (4+ ms) | ~2.5% | 偶发更长延迟 |
| 单帧 `\|dx\|/\|dy\|` cap | 45 / 13 | 转发层 cap |
| `\|Δspeed\|` p99 | 7 | jerk cap |
| **静止时是否发包** | **不发** | **dx=0,dy=0 直接 drop** |
| 方向熵 | 2.457 bits | forwarder 加水平偏置 |

详见 `report.md` 第 8 节校准对照表。

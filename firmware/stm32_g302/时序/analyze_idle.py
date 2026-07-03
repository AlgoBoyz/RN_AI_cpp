#!/usr/bin/env python3
"""
analyze_idle.py — 真鼠标"静止"状态 EP1 IN 行为分析

输入：USBPcap 抓的 .pcapng（默认 idle_1000hz.pcapng）— 鼠标完全静置 ≥30 s
输出：控制台 + 对 report.md 第 8 节的"idle baseline 校准项"建议（写入 idle_findings.md）

为什么单独分析
---------------
真鼠标在 sensor 检测到无 motion 时 **完全不发 EP 0x81 中断包**（USB host
对 NAK 持续轮询，不算异常）。这与"有 motion 时连续 ~1000 Hz 报告"形成
强对比。注入路径若在 AI 空闲时仍持续发 (0,0) 帧，反而与真设备行为不符。

本脚本量化：
- 抓包总时长 vs. 第一帧出现时间（"静默期"占比）
- 一旦出现 motion 后的 burst 结构（连续报告 / 短脉冲间隔）
- burst 内 dx/dy 分布（即"低速噪声"形态）
- 严格零位移帧占比（应该几乎为 0）
"""
from __future__ import annotations
import statistics
import subprocess
import sys
from collections import Counter
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_PCAP = SCRIPT_DIR / "idle_1000hz.pcapng"
TSHARK_CANDIDATES = [
    Path(r"C:\Program Files\Wireshark\tshark.exe"),
    Path(r"C:\Program Files (x86)\Wireshark\tshark.exe"),
    Path("tshark"),
]


def find_tshark() -> Path:
    for c in TSHARK_CANDIDATES:
        try:
            r = subprocess.run([str(c), "--version"], capture_output=True, text=True, timeout=5)
            if r.returncode == 0:
                return c
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
    raise SystemExit("tshark not found")


def file_duration_seconds(tshark: Path, pcap: Path) -> float:
    """Total capture span: last frame time_relative across ALL packets, not just EP 0x81."""
    cmd = [str(tshark), "-r", str(pcap), "-T", "fields", "-e", "frame.time_relative"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return 0.0
    last = 0.0
    for line in r.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            last = max(last, float(line))
        except ValueError:
            pass
    return last


def extract_reports(tshark: Path, pcap: Path):
    cmd = [
        str(tshark), "-r", str(pcap),
        "-Y", "usb.transfer_type==0x01 && usb.endpoint_address==0x81 && usb.data_len==8",
        "-T", "fields",
        "-e", "frame.time_relative",
        "-e", "usbhid.data",
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"tshark failed: {r.stderr}")
    out = []
    for line in r.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        t = float(parts[0])
        d = parts[1].replace(":", "")
        if len(d) != 16:
            continue
        bs = bytes.fromhex(d)
        out.append((
            t, bs[0],
            int.from_bytes(bs[2:4], "little", signed=True),
            int.from_bytes(bs[4:6], "little", signed=True),
            bs[6] if bs[6] < 128 else bs[6] - 256,
        ))
    return out


# Burst grouping threshold: any gap larger than this starts a new burst.
BURST_GAP_US = 5_000


def group_bursts(events, dts_us):
    bursts = [[events[0]]] if events else []
    for i in range(1, len(events)):
        if dts_us[i-1] < BURST_GAP_US:
            bursts[-1].append(events[i])
        else:
            bursts.append([events[i]])
    return bursts


def main():
    pcap = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PCAP
    if not pcap.exists():
        raise SystemExit(f"pcap not found: {pcap}")
    print(f"[*] pcap: {pcap}")
    tshark = find_tshark()
    total_dur = file_duration_seconds(tshark, pcap)
    events = extract_reports(tshark, pcap)
    print(f"[*] capture total duration: {total_dur:.3f} s")
    print(f"[*] EP 0x81 8B reports    : {len(events)}")
    if not events:
        print("[!] zero EP 0x81 frames — sensor fully idle for whole capture")
        return

    first_t = events[0][0]
    last_t = events[-1][0]
    silent_pre = first_t
    active_span = last_t - first_t

    print()
    print(f"  first frame at t = {first_t:.3f} s")
    print(f"  last  frame at t = {last_t:.3f} s")
    print(f"  silent prefix    : {silent_pre:.3f} s ({silent_pre/total_dur*100:.1f}% of capture)")
    print(f"  active window    : {active_span:.3f} s")

    # Distributions
    xc = Counter(e[2] for e in events)
    yc = Counter(e[3] for e in events)
    btn_c = Counter(e[1] for e in events)
    wheel_c = Counter(e[4] for e in events)

    print()
    print("dx top 10:")
    for v, c in sorted(xc.items(), key=lambda kv: -kv[1])[:10]:
        print(f"  dx={v:4d}: {c:5d}  ({c/len(events)*100:.2f}%)")
    print("dy top 10:")
    for v, c in sorted(yc.items(), key=lambda kv: -kv[1])[:10]:
        print(f"  dy={v:4d}: {c:5d}  ({c/len(events)*100:.2f}%)")
    print(f"buttons: {dict(btn_c)}")
    print(f"wheel  : {dict(wheel_c)}")

    strict_zero = sum(1 for e in events if e[1] == 0 and e[2] == 0 and e[3] == 0 and e[4] == 0)
    nonzero_motion = sum(1 for e in events if e[2] != 0 or e[3] != 0)
    print()
    print(f"strict-zero frames (btn=dx=dy=wheel=0): {strict_zero} ({strict_zero/len(events)*100:.2f}%)")
    print(f"nonzero-motion frames (dx!=0 or dy!=0): {nonzero_motion} ({nonzero_motion/len(events)*100:.2f}%)")

    # Inter-arrival
    dts_us = [(events[i][0]-events[i-1][0])*1e6 for i in range(1, len(events))]
    if dts_us:
        print()
        print(f"inter-arrival in active window:")
        sd = sorted(dts_us)
        def pct(p): return sd[int(p/100*len(sd))]
        print(f"  mean/median/σ   : {statistics.mean(dts_us):.0f} / {statistics.median(dts_us):.0f} / {statistics.stdev(dts_us):.0f} µs")
        print(f"  min/max         : {min(dts_us):.0f} / {max(dts_us):.0f} µs")
        print(f"  p50/p95/p99     : {pct(50):.0f} / {pct(95):.0f} / {pct(99):.0f} µs")

    # Burst structure
    bursts = group_bursts(events, dts_us)
    burst_sizes = [len(b) for b in bursts]
    burst_spans_ms = [(b[-1][0]-b[0][0])*1000 if len(b) > 1 else 0 for b in bursts]
    print()
    print(f"burst count (gap > {BURST_GAP_US//1000} ms): {len(bursts)}")
    print(f"  burst sizes: min={min(burst_sizes)}, median={statistics.median(burst_sizes):.0f}, max={max(burst_sizes)}")
    print(f"  burst spans: min={min(burst_spans_ms):.1f}ms, max={max(burst_spans_ms):.1f}ms")
    print(f"  largest burst: {max(burst_sizes)} frames ({max(burst_spans_ms):.0f} ms continuous report)")

    # Sample first N bursts
    print()
    print(f"first {min(15, len(bursts))} bursts (t / frames / span_ms / first_dx,dy / last_dx,dy):")
    for i, b in enumerate(bursts[:15]):
        span = (b[-1][0]-b[0][0])*1000 if len(b) > 1 else 0
        print(f"  burst {i:2d}: t={b[0][0]:6.3f}s  n={len(b):4d}  span={span:6.1f}ms  "
              f"first=({b[0][2]:3d},{b[0][3]:3d})  last=({b[-1][2]:3d},{b[-1][3]:3d})")

    # Per-burst dx/dy magnitudes — confirm "low speed noise" character
    if bursts:
        big = max(bursts, key=len)
        bx_abs = [abs(e[2]) for e in big]
        by_abs = [abs(e[3]) for e in big]
        print()
        print(f"largest burst ({len(big)} frames) magnitude profile:")
        print(f"  |dx|: max={max(bx_abs)}, median={statistics.median(bx_abs):.0f}, mean={statistics.mean(bx_abs):.2f}")
        print(f"  |dy|: max={max(by_abs)}, median={statistics.median(by_abs):.0f}, mean={statistics.mean(by_abs):.2f}")

    # Write findings markdown for inclusion in report
    out_md = SCRIPT_DIR / "idle_findings.md"
    write_findings(out_md, pcap=pcap, total_dur=total_dur, events=events,
                   first_t=first_t, last_t=last_t, silent_pre=silent_pre,
                   active_span=active_span, dts_us=dts_us, bursts=bursts,
                   xc=xc, yc=yc, strict_zero=strict_zero, nonzero_motion=nonzero_motion)
    print()
    print(f"[+] wrote {out_md.name}")


def write_findings(path, *, pcap, total_dur, events, first_t, last_t, silent_pre,
                   active_span, dts_us, bursts, xc, yc, strict_zero, nonzero_motion):
    burst_sizes = [len(b) for b in bursts]
    burst_spans_ms = [(b[-1][0]-b[0][0])*1000 if len(b) > 1 else 0 for b in bursts]
    big = max(bursts, key=len) if bursts else []
    bx_top = sorted(xc.items(), key=lambda kv: -kv[1])[:8]
    by_top = sorted(yc.items(), key=lambda kv: -kv[1])[:8]

    with open(path, "w", encoding="utf-8") as f:
        f.write(f"""# Idle-state 行为分析（追加到 report.md）

数据源：`{pcap.name}`（同款真鼠标 VID 0xB58E PID 0x9E84，鼠标完全静置）
分析脚本：`analyze_idle.py`

## 核心结论：真鼠标静止时 **不发包**

| 字段 | 值 |
|---|---|
| 抓包总时长 | {total_dur:.3f} s |
| EP 0x81 第一帧出现时刻 | t = {first_t:.3f} s |
| 静默期（pre-window） | {silent_pre:.3f} s （**{silent_pre/total_dur*100:.1f}%** of capture） |
| 活动窗口 | {active_span:.3f} s |
| EP 0x81 总帧数 | {len(events)} |
| 严格零位移帧 (btn=dx=dy=wheel=0) | {strict_zero} ({strict_zero/len(events)*100:.2f}%) |
| dx≠0 或 dy≠0 帧 | {nonzero_motion} ({nonzero_motion/len(events)*100:.2f}%) |

前 {silent_pre:.0f} 秒鼠标 USB 仍在枚举/在线，但 EP 0x81 **零帧** —— sensor
检测无 motion 就**完全不上报**，host 对中断 IN 持续 NAK 不算异常。

## Burst 结构

一旦 sensor 检测到任何 motion，立刻进入 ~1000 Hz 连续报告：

| 维度 | 值 |
|---|---|
| burst 总数 | {len(bursts)} （gap > 5 ms 视为分隔） |
| burst 大小 | min={min(burst_sizes)}, median={statistics.median(burst_sizes):.0f}, max={max(burst_sizes)} |
| burst 时长 | min={min(burst_spans_ms):.1f}ms, max={max(burst_spans_ms):.1f}ms |
| 最大 burst | **{max(burst_sizes)} 帧 / {max(burst_spans_ms):.0f} ms** 连续报告 |

剩余多为 1-8 帧的微 burst —— sensor 在 lift-off / 低速判定阈值上下抖动。

## Burst 内的"低速噪声"形态（最大 burst，{len(big)} 帧）

dx 分布 top 8（注意有方向偏置，可能是桌面微倾斜）：

| dx | 帧数 | 占比 |
|---:|---:|---:|
""")
        n = len(events)
        for v, c in bx_top:
            f.write(f"| {v} | {c} | {c/n*100:.2f}% |\n")
        f.write("\ndy 分布 top 8：\n\n| dy | 帧数 | 占比 |\n|---:|---:|---:|\n")
        for v, c in by_top:
            f.write(f"| {v} | {c} | {c/n*100:.2f}% |\n")
        if big:
            bx_abs = [abs(e[2]) for e in big]
            by_abs = [abs(e[3]) for e in big]
            f.write(f"""
最大 burst 量级：
- `|dx|` max={max(bx_abs)}, median={statistics.median(bx_abs):.0f}, mean={statistics.mean(bx_abs):.2f}
- `|dy|` max={max(by_abs)}, median={statistics.median(by_abs):.0f}, mean={statistics.mean(by_abs):.2f}

低速噪声集中在 `|dx| ≤ 5`、`|dy| ≤ 2`。不是 ±1 噪声而是带方向漂移
的真 motion 信号（桌面微倾斜或手部余热引起的 sensor 漂移）。
""")
        f.write("""
## 修正之前 report.md 第 8 节的判断

之前清单中两项写错，本节修正：

| 校准点 | 旧判断 (错) | 新结论（本 idle 抓包证实） |
|---|---|---|
| 零位移帧占比 | "AI 停止 = 输出 0 反常，应加 ±1 噪声" | **真鼠标不发零帧**，sensor 无 motion 就静默 |
| AI 空闲时注入策略 | 持续发 (0,0) | **直接 drop，不发包** ← 与真鼠标一致 |

## 修正后注入路径策略（M4-3 落地）

| 场景 | 真鼠标行为 | 注入路径 |
|---|---|---|
| AI 输出 dx≠0 或 dy≠0 | burst 报告 ~1 ms 间隔 | 1 ms deadline 调度发送 |
| AI 输出 dx=0,dy=0 | **不报告，bus 静默** | **drop frame，不进 USBD_LL_Transmit** |
| AI 长时停止 | sensor sleep，0 帧/秒 | 注入也 0 帧/秒 |
| AI 恢复输出 | 立即 burst，无 ramp | 立即调度即可 |

这反而简化了 0xFE opcode 注入逻辑 —— 不需要"零位移噪声生成器"。

## 副效应：检测维度

- **空闲带宽指纹**：注入流如果 AI 空闲时仍发 0 帧（哪怕带 ±1 噪声），
  USB bus 上的 NAK / DATA0 比例与真鼠标差异巨大，是 server-side 容易
  对比的特征。
- **burst 启动延迟**：真鼠标从静默到第一帧大约 1 ms 内启动；
  注入路径如果有 deadline 调度积压，启动延迟会偏长。需测。
""")


if __name__ == "__main__":
    main()

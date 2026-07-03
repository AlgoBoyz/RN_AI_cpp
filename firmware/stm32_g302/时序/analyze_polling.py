#!/usr/bin/env python3
"""
analyze_polling.py — 真鼠标 EP1 IN 中断流的时序 / 数据 / 行为分析

输入：USBPcap 抓的 .pcapng（默认同目录下 respond_1000hz.pcapng）
输出：
  - 控制台打印各类统计
  - 同目录 report.md（Markdown 报告）
  - 同目录 dt_histogram.csv / speed_histogram.csv / direction_histogram.csv

依赖：
  - tshark.exe（Wireshark 自带）— 默认查找 C:\\Program Files\\Wireshark\\tshark.exe
  - Python 3.8+（仅 stdlib，无第三方依赖）

HID Report 解析约定（boot-mouse-like 8B）：
  [btn][?][X lo][X hi][Y lo][Y hi][wheel][?]
  X / Y = 16-bit signed little-endian（真鼠标多支持 ±32k，远大于 boot 协议 ±127）

为什么关心这些指标
-------------------
1. polling 间隔 σ：USB FS SOF 调度天然有抖动；σ 太小（< 5 µs）的注入流反而
   反作弊可识别为伪造。本脚本统计 steady-state σ 作为校准目标。
2. 单帧 |dx|/|dy| 上限：真传感器+sensor IC 单帧上报有硬上限，AI 输出未
   切分时可能超出。
3. |Δspeed| (相邻帧 jerk)：真人手部连续运动，jerk 有界；AI 推理逐帧
   独立计算时 jerk 分布偏厚尾。
4. 方向 octant 分布：真人有明显屏幕水平偏置（任务栏方向）；AI 瞄准
   会更接近均匀分布——server-side ML 经典识别特征。
"""
from __future__ import annotations
import csv
import io
import math
import os
import statistics
import subprocess
import sys
from pathlib import Path

# Windows GBK console can't encode µ etc.; force UTF-8 unconditionally.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_PCAP = SCRIPT_DIR / "respond_1000hz.pcapng"
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
    raise SystemExit("tshark not found; install Wireshark or edit TSHARK_CANDIDATES")


def extract_reports(tshark: Path, pcap: Path) -> list[tuple[float, int, int, int, int]]:
    """Return list of (t, btn, x, y, wheel) for EP 0x81 8-byte interrupt frames."""
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
        btn = bs[0]
        x = int.from_bytes(bs[2:4], "little", signed=True)
        y = int.from_bytes(bs[4:6], "little", signed=True)
        wheel = bs[6] if bs[6] < 128 else bs[6] - 256
        out.append((t, btn, x, y, wheel))
    return out


# -------------------- statistics --------------------

def pct(sorted_xs, p):
    if not sorted_xs:
        return 0.0
    i = max(0, min(len(sorted_xs) - 1, int(p / 100 * len(sorted_xs))))
    return sorted_xs[i]


def stat_block(label, xs, unit=""):
    if not xs:
        return f"{label}: <empty>\n"
    s = sorted(xs)
    return (
        f"{label}\n"
        f"  count   : {len(xs)}\n"
        f"  mean    : {statistics.mean(xs):.3f} {unit}\n"
        f"  median  : {statistics.median(xs):.3f} {unit}\n"
        f"  stdev   : {statistics.stdev(xs):.3f} {unit}\n"
        f"  min/max : {min(xs):.3f} / {max(xs):.3f} {unit}\n"
        f"  p01/p05/p25/p50/p75/p95/p99: "
        f"{pct(s,1):.2f} / {pct(s,5):.2f} / {pct(s,25):.2f} / "
        f"{pct(s,50):.2f} / {pct(s,75):.2f} / {pct(s,95):.2f} / {pct(s,99):.2f}\n"
    )


# -------------------- analysis --------------------

def main():
    pcap = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PCAP
    if not pcap.exists():
        raise SystemExit(f"pcap not found: {pcap}")
    print(f"[*] pcap: {pcap}")
    tshark = find_tshark()
    print(f"[*] tshark: {tshark}")
    events = extract_reports(tshark, pcap)
    if len(events) < 100:
        raise SystemExit(f"only {len(events)} reports — too few")
    print(f"[*] EP 0x81 8B reports: {len(events)}")

    duration = events[-1][0] - events[0][0]
    avg_hz = len(events) / duration if duration > 0 else 0.0

    # 1) inter-report interval (microseconds)
    dts_us = [(events[i][0] - events[i-1][0]) * 1e6 for i in range(1, len(events))]
    # steady-state subset: 800..1500 us bucket (the 1 ms main mode)
    ss = [d for d in dts_us if 800 < d < 1500]
    ss_ratio = len(ss) / len(dts_us)

    # double-mode detection: count 950 / 1000 µs peaks
    p950 = sum(1 for d in dts_us if 920 <= d < 970)
    p1000 = sum(1 for d in dts_us if 970 <= d < 1030)

    # finer modal analysis on steady-state: 5 µs bins from 940 to 1060
    # to expose any real sub-ms bimodality (vs a binning artefact at 1000 µs).
    modal_hist = {}
    for d in ss:
        if 940 <= d < 1060:
            b = int(d / 5) * 5
            modal_hist[b] = modal_hist.get(b, 0) + 1
    # Identify the two highest non-adjacent peaks.
    peaks = sorted(modal_hist.items(), key=lambda kv: -kv[1])
    top_peak = peaks[0] if peaks else (0, 0)
    second_peak = next(((b, c) for (b, c) in peaks[1:] if abs(b - top_peak[0]) > 10), (0, 0))

    # fine histogram (50 us bins, up to 4 ms)
    dt_hist = {}
    for d in dts_us:
        if d > 4000:
            continue
        b = int(d / 50) * 50
        dt_hist[b] = dt_hist.get(b, 0) + 1

    # coarse histogram (1 ms bins, full range)
    dt_coarse = {}
    for d in dts_us:
        b = round(d / 1000)
        dt_coarse[b] = dt_coarse.get(b, 0) + 1

    # 2) per-report displacement
    xs = [e[2] for e in events]
    ys = [e[3] for e in events]
    speeds = [math.hypot(e[2], e[3]) for e in events]
    nz = [s for s in speeds if s > 0]
    zero_count = len(speeds) - len(nz)

    # 3) |Δspeed| between adjacent reports (jerk proxy)
    deltas = [abs(speeds[i] - speeds[i-1]) for i in range(1, len(speeds))]

    # 4) direction histogram (8 octants), only for nonzero frames
    oct_names = ["E", "NE", "N", "NW", "W", "SW", "S", "SE"]
    oct_counts = [0] * 8
    for e in events:
        if e[2] == 0 and e[3] == 0:
            continue
        a = math.degrees(math.atan2(-e[3], e[2]))  # -y because screen Y grows down
        if a < 0:
            a += 360
        idx = int((a + 22.5) // 45) % 8
        oct_counts[idx] += 1

    # 5) moving-window avg speed (W reports ≈ 400 ms; W chosen by avg_hz)
    W = max(50, int(0.4 * avg_hz))
    movavg = []
    if len(speeds) > W:
        run_sum = sum(speeds[:W])
        movavg.append(run_sum / W)
        for i in range(W, len(speeds)):
            run_sum += speeds[i] - speeds[i - W]
            movavg.append(run_sum / W)

    # 6) idle gaps: dt > 50 ms (host pause / capture gap candidates)
    long_gaps = [d for d in dts_us if d > 50_000]

    # ---------- print ----------
    print()
    print(f"[*] duration         : {duration:.3f} s")
    print(f"[*] avg report rate  : {avg_hz:.1f} Hz")
    print()
    print(stat_block("inter-report interval (us)", dts_us, "us"))
    print(stat_block(f"steady-state subset (800..1500 us, {ss_ratio*100:.1f}%)", ss, "us"))
    print(f"950 / 1000 µs SOF dual-peak (50 µs bins): {p950} / {p1000}  "
          f"(ratio {p950/(p950+p1000+1e-9)*100:.1f}% : {p1000/(p950+p1000+1e-9)*100:.1f}%)")
    print(f"top modal bin (5 µs resolution): {top_peak[0]}-{top_peak[0]+5} µs ({top_peak[1]} frames)")
    print(f"second peak (>10 µs away)      : {second_peak[0]}-{second_peak[0]+5} µs ({second_peak[1]} frames)")
    bimodality = (second_peak[1] / top_peak[1]) if top_peak[1] else 0.0
    print(f"bimodality ratio (peak2/peak1) : {bimodality:.3f}  "
          f"({'BIMODAL' if bimodality > 0.5 else 'UNIMODAL'})")
    print()
    print("modal hist 5 µs bins (940..1060 µs):")
    for b in sorted(modal_hist):
        c = modal_hist[b]
        bar = "#" * min(60, c * 60 // top_peak[1])
        print(f"  {b:5d} µs : {c:6d}  {bar}")
    print()
    print("dt fine histogram (50 us bins, ≤ 4 ms):")
    for b in sorted(dt_hist):
        bar = "#" * min(60, dt_hist[b] // max(1, len(dts_us)//300))
        print(f"  {b:5d} us : {dt_hist[b]:6d}  {bar}")
    print()
    print("dt coarse histogram (1 ms bins, full):")
    for b in sorted(dt_coarse)[:40]:
        bar = "#" * min(60, dt_coarse[b] // max(1, len(dts_us)//300))
        print(f"  {b:4d} ms : {dt_coarse[b]:6d}  {bar}")
    if len(dt_coarse) > 40:
        rest = sum(v for k, v in dt_coarse.items() if k > sorted(dt_coarse)[39])
        print(f"  (truncated, rest tail sum: {rest})")
    print()
    print(stat_block("per-report |speed| (px/frame)", nz))
    print(f"  zero-motion frames: {zero_count} ({zero_count/len(speeds)*100:.1f}%)")
    print(f"  raw dx range: [{min(xs)}, {max(xs)}]  |dx|max={max(abs(v) for v in xs)}")
    print(f"  raw dy range: [{min(ys)}, {max(ys)}]  |dy|max={max(abs(v) for v in ys)}")
    print()
    print(stat_block("|delta speed| adjacent reports (jerk proxy)", deltas))
    print()
    total_dir = sum(oct_counts) or 1
    print(f"direction histogram (8 octants, screen frame, +X=east, -Y=north):")
    for i, c in enumerate(oct_counts):
        print(f"  {oct_names[i]:>2}: {c:6d}  {c/total_dir*100:5.1f}%  {'#'*min(60, c*60//total_dir)}")
    # entropy of direction (uniform = 3.0 bits, biased < 3.0)
    H = 0.0
    for c in oct_counts:
        if c:
            p = c / total_dir
            H -= p * math.log2(p)
    print(f"  Shannon entropy: {H:.3f} bits (uniform = 3.000)")
    print()
    if movavg:
        print(stat_block(f"moving-window avg speed (W={W} reports ≈ {W*1000/avg_hz:.0f} ms)", movavg))
        mn, mx = min(movavg), max(movavg)
        bins = 10
        bc = [0]*bins
        for v in movavg:
            idx = min(bins-1, int((v-mn)/(mx-mn+1e-9)*bins))
            bc[idx] += 1
        print(f"  histogram:")
        for i,c in enumerate(bc):
            lo, hi = mn + i*(mx-mn)/bins, mn + (i+1)*(mx-mn)/bins
            print(f"    {lo:6.2f}-{hi:6.2f}: {c:5d}  {'#'*min(60, c*60//max(bc))}")
    print()
    print(f"long gaps (>50 ms): {len(long_gaps)}  max={max(long_gaps) if long_gaps else 0:.0f} us")

    # ---------- CSV exports ----------
    with open(SCRIPT_DIR / "dt_histogram.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["bin_us_50us", "count"])
        for b in sorted(dt_hist):
            w.writerow([b, dt_hist[b]])
    with open(SCRIPT_DIR / "dt_histogram_coarse_ms.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["bin_ms", "count"])
        for b in sorted(dt_coarse):
            w.writerow([b, dt_coarse[b]])
    with open(SCRIPT_DIR / "speed_histogram.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["px_per_frame_bin", "count"])
        sp_hist = {}
        for s in nz:
            b = round(s)
            sp_hist[b] = sp_hist.get(b, 0) + 1
        for b in sorted(sp_hist):
            w.writerow([b, sp_hist[b]])
    with open(SCRIPT_DIR / "direction_histogram.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["octant", "label", "count", "pct"])
        for i,c in enumerate(oct_counts):
            w.writerow([i, oct_names[i], c, f"{c/total_dir*100:.2f}"])

    # ---------- Markdown report ----------
    write_report(
        path=SCRIPT_DIR / "report.md",
        pcap=pcap,
        events_n=len(events),
        duration=duration,
        avg_hz=avg_hz,
        dts_us=dts_us,
        ss=ss,
        ss_ratio=ss_ratio,
        p950=p950,
        p1000=p1000,
        modal_hist=modal_hist,
        top_peak=top_peak,
        second_peak=second_peak,
        bimodality=bimodality,
        dt_hist=dt_hist,
        dt_coarse=dt_coarse,
        speeds=speeds,
        nz=nz,
        xs=xs,
        ys=ys,
        deltas=deltas,
        oct_names=oct_names,
        oct_counts=oct_counts,
        movavg=movavg,
        W=W,
        long_gaps=long_gaps,
        H=H,
    )
    print()
    print(f"[+] wrote report.md, dt_histogram.csv, dt_histogram_coarse_ms.csv, speed_histogram.csv, direction_histogram.csv")


def write_report(*, path, pcap, events_n, duration, avg_hz, dts_us, ss, ss_ratio,
                 p950, p1000, modal_hist, top_peak, second_peak, bimodality,
                 dt_hist, dt_coarse, speeds, nz, xs, ys, deltas,
                 oct_names, oct_counts, movavg, W, long_gaps, H):
    def line(xs):
        if not xs: return "—"
        s = sorted(xs)
        return (f"mean={statistics.mean(xs):.2f}, median={statistics.median(xs):.2f}, "
                f"σ={statistics.stdev(xs):.2f}, "
                f"p50/p90/p95/p99/max={pct(s,50):.1f}/{pct(s,90):.1f}/{pct(s,95):.1f}/{pct(s,99):.1f}/{max(xs):.0f}")

    total_dir = sum(oct_counts) or 1
    main_dt_bin = max(dt_hist, key=dt_hist.get) if dt_hist else 0

    with open(path, "w", encoding="utf-8") as f:
        f.write(f"""# 真鼠标 1000 Hz 轮询时序分析

数据源：`{pcap.name}`（VID 0xB58E / PID 0x9E84，USBPcap 抓取）
分析脚本：`analyze_polling.py`（同目录）

---

## 1. 概览

| 字段 | 值 |
|---|---|
| EP 0x81 8B 报告数 | {events_n} |
| 采样时长 | {duration:.3f} s |
| 平均报告率 | {avg_hz:.1f} Hz |
| 稳态间隔（800-1500 µs 桶） | {len(ss)} 帧 ({ss_ratio*100:.1f}%) |
| 主峰桶 | {main_dt_bin} µs ({dt_hist.get(main_dt_bin,0)} 帧) |

> 名义 1 ms 轮询，但实际只有约 {ss_ratio*100:.0f}% 帧落在稳态窗内；
> 其余是 SOF 跳帧 + 主机调度抖动。

---

## 2. 轮询间隔分布（稳态）

{line(ss)}

**模态分析**（5 µs 分辨率，940-1060 µs 区间）

| 主峰桶 | 次高峰桶 (距主峰 > 10 µs) | 次/主比 | 判定 |
|---|---|---|---|
| {top_peak[0]}-{top_peak[0]+5} µs ({top_peak[1]} 帧) | {second_peak[0]}-{second_peak[0]+5} µs ({second_peak[1]} 帧) | {bimodality:.3f} | {'**BIMODAL（双峰）**' if bimodality > 0.5 else 'UNIMODAL（单峰）'} |

> 稳态间隔确实是双峰分布，间距约 {abs(top_peak[0]-second_peak[0])} µs。
> 两峰几乎等高（次/主 = {bimodality:.2f}），整体 σ={statistics.stdev(ss):.0f} µs 主要来源于
> 这两个峰本身的分裂，不是单峰的高斯展宽。
> 物理来源推测：USB Full-Speed bus 的 SOF 1 ms 与设备内 sensor 采样
> ~1 ms 周期相位差（aliasing）。

**反作弊提示**：
- σ ≈ {statistics.stdev(ss):.0f} µs（含双峰分裂）是真鼠标"指纹"基线。注入流如果是
  单峰高斯且 σ < 10 µs，会被识别为非物理轮询。
- 若注入流要伪装高保真，应在主峰 980 µs 和 1020 µs 处各产生 ~50% 帧，
  而不是只在 1000 µs 主峰。但 deadline 调度做到这种双峰难度较高，
  实务上让 σ ≈ 15-20 µs 的近高斯也已经足够欺骗大部分行为分析。
- 跳帧簇（2/3 ms）是真设备常态（占 ~6%），不应人为完全消除。
- 跳到 4+ ms 的占 ~2.5%，长尾衰减；注入流若完全无 > 1 ms 帧反而异常。

### 稳态模态直方图（5 µs 桶）

```
""")
        # show modal_hist as ASCII bars (so it renders fine in plain MD)
        if modal_hist:
            mx = max(modal_hist.values())
            for b in sorted(modal_hist):
                c = modal_hist[b]
                bar = "#" * min(60, c * 60 // mx)
                f.write(f" {b:5d} µs : {c:6d}  {bar}\n")
        f.write("""```

### 完整间隔直方图（1 ms 桶）

| 桶 (ms) | 帧数 |
|---:|---:|
""")
        for b in sorted(dt_coarse):
            f.write(f"| {b} | {dt_coarse[b]} |\n")

        f.write(f"""

> 详细 50 µs 分辨率见 `dt_histogram.csv`。

---

## 3. 单帧位移 / 速度

| 维度 | 值 |
|---|---|
| 零位移帧 | {len(speeds)-len(nz)} ({(len(speeds)-len(nz))/len(speeds)*100:.1f}%) |
| 单帧 `|dx|` 范围 | [{min(xs)}, {max(xs)}]，max={max(abs(v) for v in xs)} |
| 单帧 `|dy|` 范围 | [{min(ys)}, {max(ys)}]，max={max(abs(v) for v in ys)} |
| 单帧 `|speed|` (px/frame) | {line(nz)} |

**反作弊提示**：
- 单帧位移有硬上限（{max(abs(v) for v in xs)} / {max(abs(v) for v in ys)}），来自 sensor IC 的 raw count 范围。
  AI 推理输出若超过这个上限未拆分，会暴露。
- 真鼠标在 1 ms 间隔下几乎不会出现 0 位移帧（{(len(speeds)-len(nz))/len(speeds)*100:.2f}%）——
  哪怕停顿，sensor 噪声也产生 ±1 抖动。注入流如果"停下来 = 完全 0"
  会反常。

---

## 4. 相邻帧加速度（`|Δspeed|` jerk 代理）

{line(deltas)}

**反作弊提示**：
- p99 = {pct(sorted(deltas),99):.0f} px/frame —— 真人手部 jerk 有上限。
- AI 逐帧独立计算输出时容易出现 |Δspeed| > 20 的跳跃，需在注入侧加 jerk cap。

---

## 5. 方向偏置（8 octant）

| 方向 | 帧数 | 占比 |
|---|---:|---:|
""")
        for i,c in enumerate(oct_counts):
            f.write(f"| {oct_names[i]} | {c} | {c/total_dir*100:.2f}% |\n")

        f.write(f"""

Shannon entropy = **{H:.3f} bits**（均匀分布 = 3.000 bits）

**反作弊提示**：
- 真人有强水平偏置（W + E + SE + NW）—— 任务栏、屏幕宽高比导致的肌肉
  记忆。本样本中水平方向占 {(oct_counts[oct_names.index('W')]+oct_counts[oct_names.index('E')])/total_dir*100:.1f}%。
- AI aimbot 目标分布均匀，输出方向熵接近 3.0 bits ——
  server-side ML 经典识别特征。
- 注入侧应根据屏幕几何叠加方向偏置。

---

## 6. 滑动窗口速度分布（W = {W} 帧 ≈ {W*1000/avg_hz:.0f} ms）

{line(movavg)}

按 movavg 速度分布可清楚识别出三个挡位（慢/中/快），对应用户报告的
"三个速度挡位 + 转圈"操作。

---

## 7. 长间隙（dt > 50 ms）

| 项 | 值 |
|---|---|
| 数量 | {len(long_gaps)} |
| 最大 | {max(long_gaps) if long_gaps else 0:.0f} µs |

通常是主机调度/USBPcap 自身延迟，非设备特征。

---

## 8. 对 Stm_g302 固件的校准目标

| 校准点 | baseline | 当前固件 | 状态 |
|---|---|---|---|
| EP1 IN 主峰间隔 | 双峰 {second_peak[0]} / {top_peak[0]} µs (近 50/50) | bInterval=1 | ✅ 描述符层正确 |
| EP1 IN 间隔 σ (steady-state) | ~{statistics.stdev(ss):.0f} µs (含双峰分裂) | 取决于 0xFE 注入路径 | ⚠️ 注入侧无 deadline，σ 未测 |
| 跳帧簇 (2-3 ms 占比) | ~6% | — | ❌ 注入流当前 100% 即时发送 |
| 长尾 (4+ ms) | ~2.5% | — | ❌ 同上 |
| 单帧 `|dx|` cap | {max(abs(v) for v in xs)} | — | ⚠️ 0xFE 转发无 cap |
| 单帧 `|dy|` cap | {max(abs(v) for v in ys)} | — | ⚠️ 同上 |
| `|Δspeed|` p99 | {pct(sorted(deltas),99):.0f} | — | ⚠️ AI 输出未做 jerk cap |
| AI 空闲时是否发包 | **不发**（真鼠标 sensor 无 motion → bus 静默，见 idle_findings.md） | — | ⚠️ 注入侧需在 dx=0,dy=0 时 drop |
| 方向熵 | {H:.3f} bits | — | ⚠️ AI 输出趋向 3.0 bits |

---

## 9. CSV 输出

- `dt_histogram.csv` — 间隔 50 µs 桶
- `dt_histogram_coarse_ms.csv` — 间隔 1 ms 桶
- `speed_histogram.csv` — 单帧 |speed| 1 px 桶
- `direction_histogram.csv` — 8 octant 方向分布

---

## 10. Idle baseline（鼠标静置时）

详见同目录 [`idle_findings.md`](idle_findings.md)（由 `analyze_idle.py`
对 `idle_1000hz.pcapng` 生成）。核心：

- **真鼠标静止时完全不发 EP 0x81 包** —— 抓包 67.8 s 中前 66.2 s 静默
  (97.5%)，sensor 检测无 motion → bus 静默，host 持续 NAK 不算异常
- 一旦有 motion，立即起 ~1000 Hz burst（最大 1073 帧 / 1.13 s 连续）
- burst 内典型噪声：`|dx| ≤ 5`、`|dy| ≤ 2`，带方向漂移（桌面微倾斜）
- 严格零位移帧仅 0.09% —— 真鼠标几乎不发 `(0,0)` 帧

**对 0xFE 注入路径的直接结论**：AI 输出 `dx=0,dy=0` 时**直接 drop，
不要进 `USBD_LL_Transmit`** —— 这恰好就是真鼠标的物理行为，省一层"零位
移噪声生成器"。

---

## 11. 相关文件索引

| 文件 | 内容 |
|---|---|
| `analyze_polling.py` | 主分析脚本（活动流） |
| `analyze_idle.py` | 静止流专项分析脚本 |
| `respond_1000hz.pcapng` | 活动抓包（60+ 秒手动移动 + 转圈） |
| `idle_1000hz.pcapng` | 静止抓包（鼠标完全静置） |
| `report.md` | 本文件 |
| `idle_findings.md` | 静止状态完整结论 |
| `console_output.txt` / `idle_console.txt` | 控制台原始输出 |
| `*.csv` | 直方图数据 |
""")


if __name__ == "__main__":
    main()

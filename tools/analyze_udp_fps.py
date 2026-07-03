#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统计 frame sender 发来的帧率，基于帧自带的 capture_timestamp_ms。
数据源: %USERPROFILE%/rn_ai/udp_receiver.csv 的 capture_ts_ms / frame_seq 列。

指标:
  - sender fps: 用相邻帧 capture_ts_ms 差值算 (sender 端真实采集速率, 不受网络/丢帧影响)
  - receiver fps: 用相邻行本地 time 差值算 (receiver 收到完整帧的速率)
  - 丢帧: frame_seq 跳变
  - 帧间隔分布 (capture_ts_ms diff): mean/min/max/p50/p95, 看采集是否稳定
  - capture_ts_ms 单调性检查 (乱序/回跳)

用法: python analyze_udp_fps.py [csv_path]
"""
import csv
import sys
import os
import re
import statistics
from collections import defaultdict

CSV = os.path.join(os.environ.get("USERPROFILE", ""), "rn_ai", "udp_receiver.csv")
if len(sys.argv) > 1:
    CSV = sys.argv[1]

if not os.path.exists(CSV):
    print(f"csv not found: {CSV}")
    sys.exit(1)

TIME_RE = re.compile(r"^\d{2}:\d{2}:\d{2}$")

rows = []
arrive_count = 0
discard_count = 0
with open(CSV, "r", encoding="utf-8", errors="replace") as f:
    reader = csv.reader(f)
    header = next(reader, None)
    # 列索引 (兼容新旧格式)
    idx = {name: i for i, name in enumerate(header)} if header else {}
    for r in reader:
        if not r or not r[0]:
            continue
        first = r[0].strip()
        if first == "ARRIVE":
            arrive_count += 1
            continue
        if first == "DISCARD":
            discard_count += 1
            continue
        if first == "FAIL":
            continue
        # 完整帧行: time 是 HH:MM:SS
        if not TIME_RE.match(first):
            continue
        try:
            row = {"time": first}
            def getcol(name, cast=None):
                i = idx.get(name)
                if i is None or i >= len(r):
                    return None
                v = r[i].strip()
                if v == "":
                    return None
                return cast(v) if cast else v
            row["frame_id"] = getcol("frame_id", int)
            row["frag_count"] = getcol("frag_count", int)
            row["assembly_ms"] = getcol("assembly_ms", float) or 0.0
            row["capture_ts_ms"] = getcol("capture_ts_ms", int)
            row["frame_seq"] = getcol("frame_seq", int)
            rows.append(row)
        except (ValueError, KeyError):
            continue

if not rows:
    print("no complete-frame rows (only ARRIVE/DISCARD events found)")
    sys.exit(1)

print(f"=== {CSV} ===")
print(f"完整帧行数: {len(rows)}  (ARRIVE事件: {arrive_count}, DISCARD事件: {discard_count})")
print(f"时间: {rows[0]['time']} -> {rows[-1]['time']}")
print()

# --- sender fps (基于 capture_ts_ms) ---
ts = [r["capture_ts_ms"] for r in rows if r["capture_ts_ms"] is not None]
if len(ts) >= 2:
    diffs = [ts[i+1] - ts[i] for i in range(len(ts)-1)]
    valid = [d for d in diffs if d > 0]   # 排除 0/负 (重复/乱序)
    # 全程 sender fps
    span_ms = ts[-1] - ts[0]
    sender_fps_total = (len(ts) - 1) * 1000.0 / span_ms if span_ms > 0 else 0
    # 逐帧间隔算的 fps
    inst_fps = [1000.0 / d for d in valid] if valid else []

    print("=== sender fps (基于 capture_timestamp_ms) ===")
    print(f"  有效帧数: {len(ts)}")
    print(f"  全程 span: {span_ms/1000:.2f}s -> 平均 fps = {sender_fps_total:.2f}")
    if inst_fps:
        inst_fps_sorted = sorted(inst_fps)
        n = len(inst_fps_sorted)
        print(f"  逐帧 fps: mean={statistics.mean(inst_fps):.2f} "
              f"min={min(inst_fps):.2f} max={max(inst_fps):.2f} "
              f"p50={inst_fps_sorted[n//2]:.2f} p95={inst_fps_sorted[int(n*0.95)]:.2f}")
    print(f"  帧间隔(ms): mean={statistics.mean(diffs):.2f} "
          f"min={min(diffs)} max={max(diffs)} std={statistics.pstdev(diffs):.2f}")
    # 乱序/回跳
    non_pos = [d for d in diffs if d <= 0]
    print(f"  非正间隔(重复/回跳): {len(non_pos)} 次")
    print()

# --- receiver fps (基于本地 time, 秒级精度) ---
times = []
for r in rows:
    try:
        h, m, s = r["time"].split(":")
        times.append(int(h)*3600 + int(m)*60 + int(s))
    except (ValueError, AttributeError):
        continue
if len(times) >= 2:
    # 按秒分桶, 每桶行数 = 该秒收到的帧数
    buckets = defaultdict(int)
    for t in times:
        buckets[t] += 1
    counts = list(buckets.values())
    span_s = max(buckets) - min(buckets) + 1
    print("=== receiver fps (基于本地 time, 秒级) ===")
    print(f"  总帧数: {len(times)}, span: {span_s}s, 平均 fps = {len(times)/span_s:.2f}")
    print(f"  每秒帧数: mean={statistics.mean(counts):.2f} "
          f"min={min(counts)} max={max(counts)} std={statistics.pstdev(counts):.2f}")
    print()

# --- 丢帧 (frame_seq) ---
seqs = [r["frame_seq"] for r in rows if r["frame_seq"] is not None]
if len(seqs) >= 2:
    gaps = [seqs[i+1] - seqs[i] for i in range(len(seqs)-1)]
    dropped = sum(max(0, g - 1) for g in gaps)
    total_expected = seqs[-1] - seqs[0] + 1
    print("=== 丢帧 (基于 frame_seq) ===")
    print(f"  seq 范围: {seqs[0]} -> {seqs[-1]}, 期望 {total_expected} 帧, 实收 {len(seqs)} 帧")
    print(f"  丢失: {dropped} 帧 ({100*dropped/max(1,total_expected):.1f}%)")
    print()

# --- 每秒 sender fps 明细 ---
if len(ts) >= 2:
    print("=== 每秒 sender fps 明细 ===")
    # capture_ts_ms 是 sender 端 epoch ms, 按秒分桶
    sec_buckets = defaultdict(list)
    for t in ts:
        sec_buckets[t // 1000].append(t)
    print(f"{'sender秒':>12} {'帧数':>5} {'fps':>6}")
    for sec in sorted(sec_buckets):
        b = sec_buckets[sec]
        # 该秒内 fps = (帧数-1)/((max-min)/1000)
        if len(b) >= 2 and (b[-1]-b[0]) > 0:
            fps = (len(b)-1) * 1000.0 / (b[-1]-b[0])
        else:
            fps = float(len(b))
        print(f"{sec:>12} {len(b):>5} {fps:>6.1f}")

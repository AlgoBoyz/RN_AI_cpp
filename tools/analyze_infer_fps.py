#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统计推理帧率, 分两类:
  - 待机(idle): 没按右键, 走 idle 降频路径. 用 [Target] no target selected 行
    (每 100 帧打 1 次) 的时间间隔 × 100 还原实际帧率.
  - 瞄准(aim): 按住右键. 用 [computeMove] 行的 fps 字段 (captureFps) 直接统计,
    这些行在 delta 非零时每帧都打.

也统计推理耗时 (infer 字段) 的分布.

用法: python analyze_infer_fps.py [log_path]
"""
import re
import sys
import os
import statistics
from collections import defaultdict

LOG = os.path.join(os.environ.get("USERPROFILE", ""), "rn_ai", "fusion.log")
if len(sys.argv) > 1:
    LOG = sys.argv[1]

# 00:03:54 [computeMove] ... fps=90 infer=7.6
COMPAT_RE = re.compile(
    r"^(\d{2}):(\d{2}):(\d{2})\s+\[computeMove\].*?fps=(\d+)\s+infer=([0-9.]+)"
)
NO_TARGET_RE = re.compile(r"^(\d{2}):(\d{2}):(\d{2})\s+\[Target\] no target selected")

def to_sec(h, m, s):
    return int(h)*3600 + int(m)*60 + int(s)

compute_rows = []   # (sec, fps, infer)  -- 瞄准时
no_target_times = []  # sec -- 待机时(每100帧打1次)

with open(LOG, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        m = COMPAT_RE.search(line)
        if m:
            compute_rows.append((
                to_sec(m.group(1), m.group(2), m.group(3)),
                int(m.group(4)),
                float(m.group(5))
            ))
            continue
        m2 = NO_TARGET_RE.search(line)
        if m2:
            no_target_times.append(to_sec(m2.group(1), m2.group(2), m2.group(3)))

print(f"=== {LOG} ===")
print(f"[computeMove] 行(瞄准时): {len(compute_rows)}")
print(f"[no target] 行(待机时, 每100帧1次): {len(no_target_times)}")
print()

# --- 待机帧率 ---
# no_target 每 100 帧打 1 次, 用相邻行间隔(秒)算: fps ≈ 100 / 间隔秒
if len(no_target_times) >= 2:
    # 按秒分桶(同一秒内多次打说明该秒帧率高)
    buckets = defaultdict(int)
    for t in no_target_times:
        buckets[t] += 1
    sorted_secs = sorted(buckets)
    # 相邻采样点间隔
    gaps = [no_target_times[i+1] - no_target_times[i] for i in range(len(no_target_times)-1)]
    gaps_pos = [g for g in gaps if g > 0]
    print("=== 待机(idle)帧率 ===")
    if gaps_pos:
        # 每个间隔对应的 fps = 100 / gap
        inst_fps = [100.0 / g for g in gaps_pos]
        # 全程: 总采样次数-1 个间隔, 每个间隔100帧
        total_frames = (len(no_target_times) - 1) * 100
        span_s = no_target_times[-1] - no_target_times[0]
        avg_fps = total_frames / span_s if span_s > 0 else 0
        print(f"  采样点: {len(no_target_times)}, span: {span_s}s")
        print(f"  全程平均: {avg_fps:.2f} fps (共 ~{total_frames} 帧)")
        inst_sorted = sorted(inst_fps)
        n = len(inst_sorted)
        print(f"  逐段 fps: mean={statistics.mean(inst_fps):.2f} "
              f"min={min(inst_fps):.2f} max={max(inst_fps):.2f} "
              f"p50={inst_sorted[n//2]:.2f} p95={inst_sorted[int(n*0.95)]:.2f}")
        print(f"  采样间隔(s): mean={statistics.mean(gaps_pos):.2f} min={min(gaps_pos)} max={max(gaps_pos)}")
    print()

# --- 瞄准帧率 ---
if compute_rows:
    fps_vals = [r[1] for r in compute_rows]
    infer_vals = [r[2] for r in compute_rows]
    print("=== 瞄准(按右键)帧率 ===")
    print(f"  样本: {len(compute_rows)}")
    fps_sorted = sorted(fps_vals)
    n = len(fps_sorted)
    print(f"  fps: mean={statistics.mean(fps_vals):.1f} min={min(fps_vals)} max={max(fps_vals)} "
          f"p50={fps_sorted[n//2]} p95={fps_sorted[int(n*0.95)]}")
    # 分布 (按 10fps 分桶)
    print("  fps 分布:")
    buckets10 = defaultdict(int)
    for v in fps_vals:
        buckets10[(v // 10) * 10] += 1
    for b in sorted(buckets10):
        bar = "#" * (buckets10[b] * 40 // max(buckets10.values()))
        print(f"    {b:>3}-{b+9:<3} : {buckets10[b]:>6} {bar}")
    print()

    print("=== 瞄准时推理耗时 infer (ms) ===")
    inf_sorted = sorted(infer_vals)
    print(f"  infer: mean={statistics.mean(infer_vals):.2f} min={min(infer_vals):.1f} "
          f"max={max(infer_vals):.1f} p50={inf_sorted[n//2]:.1f} p95={inf_sorted[int(n*0.95)]:.1f}")
    print()

    # 按秒分桶看瞄准帧率随时间
    print("=== 瞄准帧率随时间(每秒) ===")
    sec_buckets = defaultdict(list)
    for sec, fps, inf in compute_rows:
        sec_buckets[sec].append((fps, inf))
    print(f"{'时间':>8} {'帧数':>5} {'平均fps':>8} {'平均infer':>9}")
    for sec in sorted(sec_buckets):
        b = sec_buckets[sec]
        af = sum(x[0] for x in b) / len(b)
        ai = sum(x[1] for x in b) / len(b)
        print(f"{sec//3600:02d}:{sec%3600//60:02d}:{sec%60:02d}  {len(b):>5} {af:>8.1f} {ai:>9.2f}")

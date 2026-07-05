"""
ROI Selector — 在截图上框选区域，输出坐标。

支持多区域框选（如十分位+个分位数字区域）。

依赖:
  pip install opencv-python pillow numpy

用法:
  python tools/roi_selector.py                  # 先全屏截图再框选
  python tools/roi_selector.py --image path.png # 用已有图片

框选规则:
  第1次拖拽 → 区域0 (十分位, 绿色)
  第2次拖拽 → 区域1 (个分位, 蓝色)
  按 R 重置所有区域
  按 ESC / Q 退出并输出坐标
"""

import argparse
import cv2
import numpy as np
import os
import sys
from pathlib import Path
from tkinter import filedialog, Tk
from PIL import ImageGrab

# ── 颜色定义 ──────────────────────────────────────────────────────────────
COLORS = [
    (0, 255, 0),    # 区域0: 绿色
    (255, 128, 0),  # 区域1: 蓝色
]
LABELS = ["Tens (十分位)", "Ones (个分位)"]

# ── 全局状态 ──────────────────────────────────────────────────────────────
regions = []        # [(x, y, w, h), ...]  已确认的区域
drawing = False
start_pt = None
current_pt = None
image = None        # 原始分辨率图像
clone = None        # 当前显示用的副本
display = None      # 缩放到屏幕大小的显示图
scale = 1.0         # 原始→显示的缩放比例


def raw_to_display(x, y):
    """原始图像坐标 → 显示坐标"""
    return int(x * scale), int(y * scale)


def display_to_raw(x, y):
    """显示坐标 → 原始图像坐标"""
    return int(x / scale), int(y / scale)


def redraw():
    """重绘所有已确认区域 + 当前拖拽中的矩形"""
    global display, clone, regions, drawing, start_pt, current_pt

    display = clone.copy()
    h, w = display.shape[:2]

    # 绘制已确认的区域
    for i, (rx, ry, rw, rh) in enumerate(regions):
        dx, dy = raw_to_display(rx, ry)
        dw, dh = raw_to_display(rw, rh)
        color = COLORS[i % len(COLORS)]
        cv2.rectangle(display, (dx, dy), (dx + dw, dy + dh), color, 2)
        label = f"R{i} {LABELS[i]}: ({rx},{ry}) {rw}x{rh}"
        cv2.putText(display, label, (dx, dy - 8),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, color, 2)

    # 绘制拖拽中的矩形
    if drawing and start_pt is not None and current_pt is not None:
        color = COLORS[len(regions) % len(COLORS)]
        cv2.rectangle(display, start_pt, current_pt, color, 2)

        rx1, ry1 = display_to_raw(start_pt[0], start_pt[1])
        rx2, ry2 = display_to_raw(current_pt[0], current_pt[1])
        rw = abs(rx2 - rx1)
        rh = abs(ry2 - ry1)
        info = f"R{len(regions)} {LABELS[len(regions)]}: {min(rx1,rx2)},{min(ry1,ry2)} {rw}x{rh}"
        cv2.putText(display, info, (10, h - 10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, color, 2)

    # 顶部提示
    hint = f"Regions: {len(regions)}/2  |  Drag to select  |  R=reset  |  Q=quit"
    cv2.putText(display, hint, (10, 22),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1)


def on_mouse(event, x, y, flags, param):
    global drawing, start_pt, current_pt, regions

    if event == cv2.EVENT_LBUTTONDOWN:
        drawing = True
        start_pt = (x, y)
        current_pt = (x, y)

    elif event == cv2.EVENT_MOUSEMOVE and drawing:
        current_pt = (x, y)
        redraw()

    elif event == cv2.EVENT_LBUTTONUP and drawing:
        drawing = False
        current_pt = (x, y)

        # 计算原始坐标
        rx1, ry1 = display_to_raw(start_pt[0], start_pt[1])
        rx2, ry2 = display_to_raw(x, y)

        rw = abs(rx2 - rx1)
        rh = abs(ry2 - ry1)
        rx = min(rx1, rx2)
        ry = min(ry1, ry2)

        # 过滤太小（误触）
        if rw < 5 or rh < 5:
            start_pt = None
            current_pt = None
            redraw()
            return

        regions.append((rx, ry, rw, rh))
        print(f"\n  R{len(regions)-1} {LABELS[len(regions)-1]}: x={rx}  y={ry}  w={rw}  h={rh}")

        if len(regions) >= 2:
            print("\n  ✓ 两个区域已选定，按 Q 退出查看最终输出")

        start_pt = None
        current_pt = None
        redraw()


def print_results():
    """输出最终结果"""
    img_w, img_h = image.shape[1], image.shape[0]
    print(f"\n{'='*60}")
    print(f"Image: {img_w}x{img_h}")
    print(f"{'='*60}")

    for i, (rx, ry, rw, rh) in enumerate(regions):
        print(f"\n  R{i} [{LABELS[i]}]:")
        print(f"    Absolute:  x={rx}  y={ry}  w={rw}  h={rh}")
        print(f"    Relative:  rx={rx/img_w:.4f}  ry={ry/img_h:.4f}  rw={rw/img_w:.4f}  rh={rh/img_h:.4f}")

    # C++ 用
    if len(regions) >= 1:
        print(f"\n  // C++ constexpr:")
        for i, (rx, ry, rw, rh) in enumerate(regions):
            label_short = "TENS" if i == 0 else "ONES"
            print(f"  constexpr int AMMO_{label_short}_X = {rx};")
            print(f"  constexpr int AMMO_{label_short}_Y = {ry};")
            print(f"  constexpr int AMMO_{label_short}_W = {rw};")
            print(f"  constexpr int AMMO_{label_short}_H = {rh};")

    # frame_sender 用
    if len(regions) >= 1:
        print(f"\n  # frame_sender --crop-region (合并两个区域的最小外接矩形):")
        all_x = [r[0] for r in regions]
        all_y = [r[1] for r in regions]
        all_r = [r[0] + r[2] for r in regions]
        all_b = [r[1] + r[3] for r in regions]
        merged_x = min(all_x)
        merged_y = min(all_y)
        merged_w = max(all_r) - merged_x
        merged_h = max(all_b) - merged_y
        print(f"  --crop-region 1,{merged_x},{merged_y},{merged_w},{merged_h}")

    print(f"\n{'='*60}\n")


def main():
    parser = argparse.ArgumentParser(description="框选屏幕区域，支持多区域（十分位+个分位）")
    parser.add_argument("--image", help="使用已有图片代替截图")
    args = parser.parse_args()

    global image, clone, display, scale

    if args.image:
        image = cv2.imread(args.image)
        if image is None:
            print(f"无法读取图片: {args.image}")
            sys.exit(1)
        print(f"已加载图片: {args.image} ({image.shape[1]}x{image.shape[0]})")

    elif not args.image and not sys.stdin.isatty():
        raw = sys.stdin.read().strip()
        if raw:
            image = cv2.imread(raw)
            if image is not None:
                print(f"已加载: {raw} ({image.shape[1]}x{image.shape[0]})")

    if image is None:
        root = Tk()
        root.withdraw()
        root.attributes("-topmost", True)
        file_path = filedialog.askopenfilename(
            title="选择截图文件",
            filetypes=[("图片", "*.png *.jpg *.jpeg *.bmp"), ("所有文件", "*.*")]
        )
        root.destroy()

        if file_path:
            image = cv2.imread(file_path)
            if image is not None:
                print(f"已选择: {file_path} ({image.shape[1]}x{image.shape[0]})")
            else:
                print("无法读取图片")
                sys.exit(1)
        else:
            print("未选择文件，正在全屏截图...")
            screenshot = ImageGrab.grab()
            image = cv2.cvtColor(np.array(screenshot), cv2.COLOR_RGB2BGR)
            print(f"截图完成: {image.shape[1]}x{image.shape[0]}")

    # 等比例缩放到适合屏幕显示
    img_h, img_w = image.shape[:2]
    max_display = 1200
    if img_w > max_display or img_h > max_display:
        scale = min(max_display / img_w, max_display / img_h)
    else:
        scale = 1.0

    clone = cv2.resize(image,
                       (int(img_w * scale), int(img_h * scale)),
                       interpolation=cv2.INTER_AREA)
    display = clone.copy()

    cv2.namedWindow("ROI Selector")
    cv2.setMouseCallback("ROI Selector", on_mouse)

    print(f"\n{'='*60}")
    print(f"ROI Selector — 双区域框选模式")
    print(f"{'='*60}")
    print(f"  缩放比例: {scale:.2f} (输出坐标为原始分辨率)")
    print(f"")
    print(f"  操作:")
    print(f"    第1次拖拽 → {LABELS[0]} (绿色)")
    print(f"    第2次拖拽 → {LABELS[1]} (蓝色)")
    print(f"    R → 重置所有区域")
    print(f"    ESC / Q → 退出并输出坐标")
    print(f"{'='*60}\n")

    redraw()

    while True:
        cv2.imshow("ROI Selector", display)
        key = cv2.waitKey(1) & 0xFF

        if key == ord("r"):
            regions.clear()
            drawing = False
            start_pt = None
            current_pt = None
            print("已重置\n")
            redraw()
        elif key == 27 or key == ord("q"):
            break

    cv2.destroyAllWindows()
    print_results()


if __name__ == "__main__":
    main()

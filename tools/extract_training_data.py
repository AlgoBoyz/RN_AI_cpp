"""
从 collect/ 目录的弹药区域图像中提取单个数字，用于训练 CNN 分类器。

用法:
  python tools/extract_training_data.py
  python tools/extract_training_data.py --input path/to/collect --output training_data
  python tools/extract_training_data.py --invert   # 如果数字是黑底白字

ROI 坐标（由 roi_selector.py 框选，相对于弹药区域图像）:
  十分位: x=312  y=13  w=37  h=47
  个分位: x=351  y=13  w=34  h=48

输入文件名格式:
  frame_00001_ammo_25.png   → 提取 "2"(十分位) + "5"(个分位)
  25.png                    → 同上

输出结构:
  training_data/
    0/   ← 所有"0"的样本 (28x28 灰度 PNG)
    1/
    ...
    9/
"""

import argparse
import cv2
import os
import re
import glob
from pathlib import Path

# ── ROI 坐标（由 roi_selector.py 框选，相对于弹药区域图像） ──────────────
TENS_ROI = (312, 13, 37, 47)   # x, y, w, h
ONES_ROI = (351, 13, 34, 48)

TARGET_SIZE = 28  # MNIST 标准输入尺寸


def parse_number(filename):
    """从文件名中解析弹药数字。

    'frame_00005_ammo_25.png' → 25
    '25.png'                  → 25
    """
    stem = os.path.splitext(filename)[0]

    # 优先匹配 _ammo_NN 模式
    m = re.search(r'ammo[_]?(\d+)', stem)
    if m:
        return int(m.group(1))

    # 否则尝试取最后一段数字
    parts = re.findall(r'\d+', stem)
    if parts:
        return int(parts[-1])

    return None


def extract_and_save(img_path, number, output_dir, invert):
    """从一张弹药区域图提取十分位和个分位数字，保存为 28x28 样本。"""
    img = cv2.imread(img_path)
    if img is None:
        print(f"  SKIP: 无法读取 {img_path}")
        return 0

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    basename = os.path.basename(img_path)
    count = 0

    # ── 十分位（两位数才提取） ──
    if number >= 10:
        x, y, w, h = TENS_ROI
        if x + w <= gray.shape[1] and y + h <= gray.shape[0]:
            digit_img = gray[y:y+h, x:x+w]
            digit_val = number // 10
            save_sample(digit_img, digit_val, basename, "tens", output_dir, invert)
            count += 1
        else:
            print(f"  WARN: 十分位 ROI 越界 ({basename})")

    # ── 个分位 ──
    x, y, w, h = ONES_ROI
    if x + w <= gray.shape[1] and y + h <= gray.shape[0]:
        digit_img = gray[y:y+h, x:x+w]
        digit_val = number % 10
        save_sample(digit_img, digit_val, basename, "ones", output_dir, invert)
        count += 1
    else:
        print(f"  WARN: 个分位 ROI 越界 ({basename})")

    return count


def save_sample(img, digit_val, source_name, position, output_dir, invert):
    """保存单个数字样本：resize 28x28，可选反色，按标签分目录。"""
    resized = cv2.resize(img, (TARGET_SIZE, TARGET_SIZE),
                         interpolation=cv2.INTER_AREA)

    # 反色：白底黑字 → 黑底白字（MNIST 标准）
    if invert:
        resized = 255 - resized

    digit_dir = os.path.join(output_dir, str(digit_val))
    os.makedirs(digit_dir, exist_ok=True)

    stem = os.path.splitext(source_name)[0]
    out_name = f"{stem}_{position}.png"
    cv2.imwrite(os.path.join(digit_dir, out_name), resized)


def show_samples(output_dir):
    """预览每个数字的前几个样本。"""
    try:
        import numpy as np
    except ImportError:
        return

    tiles = []
    for d in range(10):
        d_dir = os.path.join(output_dir, str(d))
        if not os.path.isdir(d_dir):
            tiles.append(None)
            continue
        files = sorted(os.listdir(d_dir))[:8]  # 每个数字取前 8 个
        row = []
        for f in files:
            img = cv2.imread(os.path.join(d_dir, f), cv2.IMREAD_GRAYSCALE)
            if img is not None:
                # 放大到 56x56 便于查看
                row.append(cv2.resize(img, (56, 56),
                          interpolation=cv2.INTER_NEAREST))
        if row:
            tiles.append(np.hstack(row))
        else:
            tiles.append(None)

    # 拼成一张大图
    rows = []
    for d in range(10):
        if tiles[d] is not None:
            # 加标签条
            h, w = tiles[d].shape
            labeled = np.zeros((h + 20, w), dtype=np.uint8)
            labeled[20:, :] = tiles[d]
            cv2.putText(labeled, str(d), (5, 14),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, 255, 1)
            rows.append(labeled)

    if rows:
        preview = np.vstack(rows)
        preview_path = os.path.join(output_dir, "_preview.png")
        cv2.imwrite(preview_path, preview)
        print(f"  预览图: {preview_path}")


def main():
    parser = argparse.ArgumentParser(
        description="从弹药区域图提取单个数字训练样本")
    parser.add_argument("--input", default=None,
                        help="collect 目录路径 (默认 %%USERPROFILE%%\\rn_ai\\collect)")
    parser.add_argument("--output", default="training_data",
                        help="训练数据输出目录 (默认 training_data)")
    parser.add_argument("--invert", action="store_true",
                        help="反色: 白底黑字 → 黑底白字")
    args = parser.parse_args()

    # 默认输入路径
    input_dir = args.input
    if input_dir is None:
        profile = os.environ.get("USERPROFILE", "")
        input_dir = os.path.join(profile, "rn_ai", "debug")

    if not os.path.isdir(input_dir):
        print(f"[ERROR] 输入目录不存在: {input_dir}")
        print(f"  请先运行 test_udp_receiver_multi_region.exe 采集数据")
        return

    all_files = sorted(glob.glob(os.path.join(input_dir, "*.png")))
    # 只处理弹药数字文件（frame_xxxxx_ammo_NN.png），跳过 region_0 等
    files = [f for f in all_files if '_ammo_' in os.path.basename(f)]
    if not files:
        print(f"[ERROR] 未找到 _ammo_ PNG 文件: {input_dir}")
        print(f"  共有 {len(all_files)} 个 PNG，但没有 _ammo_ 命名的")
        return

    # 清空输出目录
    if os.path.exists(args.output):
        import shutil
        shutil.rmtree(args.output)

    print(f"输入: {input_dir} ({len(files)} 张)")
    print(f"输出: {args.output}/")
    print(f"ROI:  十分位={TENS_ROI}  个分位={ONES_ROI}")
    print(f"反色: {'是' if args.invert else '否'}")
    print()

    # 统计
    total_digits = 0
    label_counts = {d: 0 for d in range(10)}
    skipped = 0

    for f in files:
        basename = os.path.basename(f)
        number = parse_number(basename)

        if number is None:
            print(f"  SKIP: 无法解析数字 ({basename})")
            skipped += 1
            continue

        if number < 0 or number > 99:
            print(f"  SKIP: 数字超出范围 ({basename}: {number})")
            skipped += 1
            continue

        count = extract_and_save(f, number, args.output, args.invert)
        total_digits += count

        tens_str = f"{number // 10}" if number >= 10 else "-"
        ones_str = f"{number % 10}"
        print(f"  {basename}  [{number}] → 十分位={tens_str}  个分位={ones_str}")

        if number >= 10 and count == 2:
            label_counts[number // 10] += 1
        if count >= 1:
            label_counts[number % 10] += 1

    # ── 汇总 ──
    print(f"\n{'='*50}")
    print(f"共提取 {total_digits} 个数字样本 (跳过 {skipped} 张)")
    print(f"{'='*50}")
    for d in range(10):
        n = label_counts[d]
        bar = "#" * max(1, n // max(1, max(label_counts.values()) // 30))
        print(f"  {d}: {n:4d}  {bar}")
    print(f"{'='*50}")

    # 预览
    show_samples(args.output)

    print(f"\n下一步:")
    print(f"  1. 检查 {args.output}/_preview.png 确认提取正确")
    print(f"  2. 如有必要，手动清理错分的样本")
    print(f"  3. 运行 python tools/export_mnist_onnx.py 训练模型")


if __name__ == "__main__":
    main()

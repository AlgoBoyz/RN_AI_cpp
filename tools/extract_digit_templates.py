"""
从 digits_raw.png 中分割出单个数字，保存为模板文件。
生成的模板放在 ammo/templates/ 目录下，命名为 0.png ~ 9.png。

用法:
  python tools/extract_digit_templates.py                    # 用默认路径
  python tools/extract_digit_templates.py --input path/to/digits_raw.png
"""

import argparse
import cv2
import numpy as np
import os
from pathlib import Path


def extract_templates(input_path, output_dir):
    img = cv2.imread(input_path)
    if img is None:
        print(f"无法读取: {input_path}")
        return

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    # 二值化：白色数字保留
    _, binary = cv2.threshold(gray, 200, 255, cv2.THRESH_BINARY)

    # 找轮廓
    contours, _ = cv2.findContours(binary, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    # 过滤+排序（从左到右）
    boxes = []
    for c in contours:
        x, y, w, h = cv2.boundingRect(c)
        if w > 4 and h > 8 and w * h > 30:
            boxes.append((x, y, w, h))

    boxes.sort(key=lambda b: b[0])

    if not boxes:
        print("未找到任何数字轮廓")
        return

    os.makedirs(output_dir, exist_ok=True)

    print(f"找到 {len(boxes)} 个数字轮廓，保存模板到 {output_dir}")
    print("请在下方输入每个轮廓对应的数字 (0-9)，从左到右:")

    # 显示每个轮廓让用户标定
    for i, (x, y, w, h) in enumerate(boxes):
        digit = gray[y:y + h, x:x + w].copy()
        # 统一缩放到 28x28
        template = cv2.resize(digit, (14, 28), interpolation=cv2.INTER_AREA)
        # 反色（白底黑字 → 黑底白字模板）
        template = 255 - template

        # 显示当前轮廓
        display = img.copy()
        cv2.rectangle(display, (x, y), (x + w, y + h), (0, 255, 0), 2)
        cv2.putText(display, f"#{i}", (x, y - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)
        cv2.imshow("Digit - press key 0-9", display)

        # 显示模板
        tm_display = cv2.resize(template, (70, 140), interpolation=cv2.INTER_NEAREST)
        cv2.imshow("Template", tm_display)

        print(f"  轮廓 #{i} ({w}x{h}): ", end="", flush=True)
        while True:
            key = cv2.waitKey(0) & 0xFF
            if ord('0') <= key <= ord('9'):
                digit_val = key - ord('0')
                path = os.path.join(output_dir, f"{digit_val}.png")
                cv2.imwrite(path, template)
                print(f"{digit_val} -> 已保存 {path}")
                break
            elif key == ord('q') or key == 27:
                print("跳过")
                break

    cv2.destroyAllWindows()

    # 显示所有保存的模板
    print(f"\n模板已保存到 {output_dir}:")
    for f in sorted(os.listdir(output_dir)):
        if f.endswith(".png"):
            print(f"  {f}")

    print("\n然后在 C++ 测试中加载这些模板替换合成模板即可。")


def main():
    parser = argparse.ArgumentParser(description="从 digits_raw.png 提取数字模板")
    parser.add_argument("--input", default=None,
                        help="digits_raw.png 路径")
    parser.add_argument("--output", default="ammo/templates",
                        help="模板输出目录 (默认 ammo/templates)")
    args = parser.parse_args()

    input_path = args.input
    if input_path is None:
        user = os.environ.get("USERPROFILE", "")
        input_path = os.path.join(user, "rn_ai", "debug", "digits_raw.png")

    extract_templates(input_path, args.output)


if __name__ == "__main__":
    main()

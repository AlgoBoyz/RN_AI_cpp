"""
把 digits_xxxxx.png 批量标定为 0.png ~ 50.png。
每张图会弹窗显示，你按键盘数字键输入对应的弹药数。
按 Q 跳过，方向键 ← → 切换。
"""

import cv2
import os
import glob

src_dir = os.path.join(os.environ["USERPROFILE"], "rn_ai")
dst_dir = src_dir  # 保存到同一目录

files = sorted(glob.glob(os.path.join(src_dir, "digits_*.png")))
if not files:
    print(f"未找到 digits_*.png 在 {src_dir}")
    exit(1)

print(f"找到 {len(files)} 张图，按数字键 0-9 标定")
print("  两位数字: 按完十位后快速按个位")
print("  Q: 跳过, ESC: 退出并保存")

labeled = {}
idx = 0

while idx < len(files):
    img = cv2.imread(files[idx])
    if img is None:
        idx += 1
        continue

    # 缩放显示
    h, w = img.shape[:2]
    scale = min(400 / w, 300 / h, 1.0)
    display = cv2.resize(img, (int(w * scale), int(h * scale)))

    # 显示当前进度
    info = f"[{idx+1}/{len(files)}] 按数字键..."
    cv2.putText(display, info, (5, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)
    cv2.imshow("Label digits", display)

    key = cv2.waitKey(0) & 0xFF

    if key == 27:  # ESC
        break
    elif key == ord('q') or key == ord('Q'):
        idx += 1
        continue
    elif ord('0') <= key <= ord('9'):
        # 个位数字
        val = key - ord('0')
        labeled[files[idx]] = val
        print(f"  {os.path.basename(files[idx])} -> {val}.png")
        idx += 1

    # ← 上一张
    elif key == 81 or key == 2424832:  # Left arrow
        if idx > 0:
            idx -= 1
    # → 下一张
    elif key == 83 or key == 2555904:  # Right arrow
        if idx < len(files) - 1:
            idx += 1

cv2.destroyAllWindows()

# 保存为 0.png ~ N.png
for src_path, val in labeled.items():
    dst_path = os.path.join(dst_dir, f"{val}.png")
    img = cv2.imread(src_path)
    if img is not None:
        cv2.imwrite(dst_path, img)

# 显示统计
by_val = {}
for v in labeled.values():
    by_val[v] = by_val.get(v, 0) + 1

print(f"\n标定完成: {len(labeled)} 张")
for v in sorted(by_val.keys()):
    print(f"  {v}.png: {by_val[v]} 张")
print(f"保存到 {dst_dir}")

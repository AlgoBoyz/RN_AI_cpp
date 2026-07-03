# Logitech G302 私有协议参考（精简版）

为 Stm_g302 克隆固件实现服务，仅保留**最终结论 + 字节布局 + 时序 + 必须复刻点**。
完整推导过程（错误假设、迭代修正、Solaar/logiops 交叉验证、抓包元数据）见 `HIDPP_PROTOCOL.md`。

---

## 1. 设备身份

| 字段 | 值 |
|---|---|
| idVendor / idProduct / bcdDevice | `0x046D` / `0xC07F` / `0x9100` |
| bcdUSB / bDeviceClass | `0x0200` (FS) / `0x00` |
| iManufacturer / iProduct | `"Logitech"` / `"Gaming Mouse G302"` |
| iSerialNumber | 12 位 ASCII 十进制（真机：`"157939723536"`） |
| iConfiguration / iInterface | `"U89.01_B0024"` |
| bmAttributes / bMaxPower | `0xA0` (bus + wakeup) / `150`（300 mA） |
| wTotalLength / bNumInterfaces | `0x3B` (59) / `2` |

---

## 2. HID 接口与端点

| IF | 用途 | EP | wMaxPacket | bInterval | Class/Sub/Proto |
|----|------|----|----|----|----|
| 0 | Boot Mouse | `0x81` IN | 8 | 1 ms | `03/01/02` |
| 1 | Multimedia + HID++ vendor | `0x82` IN | 20 | 1 ms | `03/00/00` |

**IF1 无 OUT 中断端点**——HID++ 主机→设备方向通过 EP0 SET_REPORT 控制传输到达。

### IF0 鼠标报文（8B，无 Report ID）
```
[btn_lo][btn_hi][x_lo][x_hi][y_lo][y_hi][wheel_i8][pan_i8]
```
按键 16-bit、X/Y 16-bit 有符号相对量、滚轮 + 横向滚动 8-bit 有符号。

### IF1 报告

| Report ID | 长度 | 用途 | 方向 |
|---|---|---|---|
| `0x01` | 8 B | 键盘 | IN |
| `0x03` | 5 B | Consumer Control | IN |
| `0x04` | 1 B | System Control | IN |
| `0x10` | 7 B | **HID++ Short** | IN + OUT(EP0) |
| `0x11` | 20 B | **HID++ Long** | IN + OUT(EP0) |

完整 67B IF0 + 151B IF1 HID descriptor 见 `USB_DEVICE/Class/G302/usb_desc_g302.h`，
byte-for-byte 抄自真机。

---

## 3. HID++ 4.2 帧格式

```
Short (RID 0x10, 7B):  [0x10][dev][feat_idx][func<<4 | sw_id][p0][p1][p2]
Long  (RID 0x11, 20B): [0x11][dev][feat_idx][func<<4 | sw_id][p0..p15]

dev   = 0xFF (corded broadcast) / 0x01..0x06 (paired slot)
sw_id = 0xD (G HUB) / 0xF (Logitech FW)，固件回包必须 echo 请求的 sw_id
```

### Root.Ping（func=1）— **协议版本必须回 4.2**
```
请求  10 ff 00 1F 00 00 <XX>
回复  11 ff 00 1F 04 02 <XX> 00...     ; HID++ 4.2，echo payload
```

### Root.GetFeature（func=0）
```
请求  10 ff 00 0d <fid_hi> <fid_lo> 00
回复  11 ff 00 0d <feat_idx> <flags> <ver> 00...
```
不支持的 feature 回 `00 00 00`（idx=0 隐式表示 Root，即"不存在"）。

### 错误帧
```
[0x10][dev][feat_idx][0x8F][orig_func | sw][err_code][0x00]
err: 0x06=INVALID_FEATURE_INDEX, 0x07=INVALID_FUNCTION, 0x09=UNSUPPORTED
```

---

## 4. FeatureSet 完整表（17 entries，真机 dump）

G HUB 通过 `Root.GetFeature(fid)` 与 `FeatureSet.GetFeatureID(idx)` 双向枚举得到：

| feat_idx | feature_id | 名称 | 必须实现？ |
|---|---|---|---|
| `0x00` | `0x0000` | Root（隐式） | ✓ |
| `0x01` | `0x0001` | FeatureSet | ✓ |
| `0x02` | `0x0002` | DeviceInformation | ⚠ 推荐 |
| `0x03` | `0x0003` | DeviceFwVersion | ✓ |
| `0x04` | `0x0005` | DeviceName | ✓ |
| `0x05` | `0x0013` | DeviceFriendlyName | 推荐 |
| `0x06..0x0c` | `0x18xx` / `0x1exx` | 未识别（系统/sensor 私有） | 推荐 |
| `0x0d` | `0x2201` | **AdjustableDPI** | ✓ |
| `0x0e` | `0x8060` | **ReportRate** | ✓ |
| `0x0f` | `0x8100` | **OnboardProfiles** | ✓ |
| `0x10` | `0x8110` | MouseButtonSpy | ⚠ 可选（全 `ff` 即可） |
| `0x11` | `0x00c1` | 未知（G HUB 私有） | 推荐 |

**FeatureSet.GetCount** → `0x11` (=17)。
**GetFeatureID(idx)** 回 `[fid_hi][fid_lo][flags]`，flags 一般 `0x00`。

### Software ID 占用
| sw_id | 占用 |
|---|---|
| `0x02` / `0x03` | logiops |
| `0x07` | OpenRGB |
| `0x0B` | Solaar |
| `0x0D` | **G HUB**（主体） |
| `0x0F` | **Logitech FW**（首次 ping） |

> G HUB 在枚举开头会用 `sw_id=0xF` 发 2 次 ping，之后切到 `0xD`。**固件两个都要响应。**

---

## 5. Feature 字节布局

### 5.1 DeviceFwVersion (feat 0x03)

```
GetEntityCount  (func=0):  10 ff 03 0d 00 00 00  →  resp[4]=count=4
GetFwInfo(idx)  (func=1):  10 ff 03 1d <idx> 00 00  →  11 ff 03 1d <type><n0><n1><n2><ver_hi><ver_lo><build_hi><build_lo>...
```

**真机 4 entities 必须照抄**：

| idx | type | name3 | ver | build |
|---|---|---|---|---|
| `0x00` | `0x00` Main FW | `"U  "` (0x55 0x20 0x20) | `0x9100` | `0x0007` |
| `0x01` | `0x01` Bootloader | `"BOT"` (0x42 0x4F 0x54) | `0x1400` | `0x0007` |
| `0x02` | `0x02` Hardware | `"HW "` (0x48 0x57 0x20) | `0x0000` | `0x0000` |
| `0x03` | `0x04` Sensor | `"PIX"` (0x50 0x49 0x58) | `0x0000` | `0x0001` |

主 FW `ver=0x9100` 必须与 USB descriptor `bcdDevice=0x9100` 一致。

### 5.2 DeviceName (feat 0x04)

```
GetNameLength  (func=0):  →  resp[4] = 0x13 (19，含末尾 padding)
GetName(off)   (func=1):  10 ff 04 1d <off> 00 00  →  16B ASCII 块
```

字符串 = **`"G302 Daedalime"`**（注意：拼错的 `Daedalime` 不是 `Daedalus`，**必须 byte-for-byte 照抄**）。

### 5.3 AdjustableDPI (feat 0x0d)

```
GetSensorCount      (func=0):  →  sensor_count = 1
GetSensorDPIList    (func=1):  10 ff 0d 1d 00 00 <page>
                               →  [sensor_idx][dpi 0_hi][dpi 0_lo][dpi 1_hi][dpi 1_lo]...0x0000 终止
                               真机 list 用 range-step 编码：
                                   [0x00C8, 0xE032, 0x1F40]  = 200..8000 step 50
                                   （含 400/800/1600/1650/3200 等所有实测值）
GetSensorDPI        (func=2):  10 ff 0d 2d <sensor> 00 00
                               →  [sensor][cur_hi][cur_lo][def_hi][def_lo]...
SetSensorDPI        (func=3):  10 ff 0d 3d <sensor> <dpi_hi> <dpi_lo>
                               →  echo（20B Long）
```

> Range-step 编码：`dpi >= 0xE000` 时 `step = dpi - 0xE000`。

实测 DPI 切换值：`400, 800, 1600, 1650, 3200`（1650 = 非整百 → 证伪等距列表）。

### 5.4 ReportRate (feat 0x0e)

```
GetReportRateList (func=0):  →  resp[4] = bitmask, bit i = 支持 (i+1) ms
                                  bit 0 (0x01) = 1 ms = 1000 Hz
                                  bit 7 (0x80) = 8 ms = 125 Hz
GetReportRate     (func=1):  →  resp[4] = current_ms (1..8)
SetReportRate     (func=2):  10 ff 0e 2d <target_ms> 00 00  →  echo
```

伪装建议：list bitmask = `0x81`（1000Hz + 125Hz），current 固定 `0x01`。
USB bInterval=1 锁死在 1ms，Set 接受即可，不需要真改频率。

> **G HUB 切 DPI 前必发 `SetReportRate(1)` = 1000Hz**——不实现这个，DPI 切换流程会卡。

### 5.5 OnboardProfiles (feat 0x0f) — **最复杂**

```
GetProfileDirectory  (func=0):  10 ff 0f 0d 00 00 00
                                →  resp[4..15] = 模式 caps + profile 元数据
SetMode              (func=1):  10 ff 0f 1d <mode> 00 00
                                →  mode = 0x01 (onboard) / 0x02 (host)；回 ACK
GetMode              (func=2):  →  resp[4] = current mode
GetActiveProfile     (func=4):  →  resp[4..5] = 00 01 (profile 1)
GetFlashStatus       (func=b):  →  resp[4] = 0x03
MemoryRead16B        (func=5):  11 ff 0f 5d <bank> 01 00 <off> 00...
                                →  返回 16B flash 内容（bank ∈ {0,1}, off 步长 0x10）
```

**GetProfileDirectory 回复（必须照抄）**：
```
11 ff 0f 0d  01 01 01 01 01  06  10 01  00 0a  01  00...
            └─ caps ──────┘  │   │      │      │
                             │   │      │      └─ default_profile = 1
                             │   │      └─ default_dpi_idx = 0x0a (=10)
                             │   └─ profile_size = 0x0110 BE = 272 B
                             └─ num_profiles = 6
```

**Flash 内容（bank=0 off=00，G302 默认 DPI 表）**：
```
80 01 00 02  a4 01  48 03  3c 06  78 0c  00 00  ff ff
                ↑    ↑      ↑      ↑
                420  840    1596   3192   ← LE uint16
```
（PixArt sensor 单位换算，与 UI 显示的 400/800/1600/3200 偏 ±20。）

**Mode echo 检测**：G HUB 每次 SetMode 后立即 GetMode 校验；固件必须维护 `mode` 状态。

### 5.6 MouseButtonSpy (feat 0x10) — 可选

```
10 ff 10 0d 00 00 00  →  01 01 00 40 00...        (button info)
10 ff 10 1d 00 00 00  →  01 01 00 80 00...
10 ff 10 3d 00 00 00  →  ff ff ff ff ff ff...     ← 真机返回全 0xFF，照抄即可
```

### 5.7 Legacy subID 0x05（**RGB / 蓝色单灯 LED**）

**G302 是单色蓝色 LED**（2 zone），**不走 HID++ feature 路径**——走 Logitech 旧版 `subID=0x05` 私有通道，但承载在同样的 RID 0x10/0x11 上。

```
帧布局（Long 20B）:
  [0]  = 0x11
  [1]  = 0xff           DeviceIndex
  [2]  = 0x05           subID（不是 feat_idx！）
  [3]  = 0x5d           func=5, sw_id=0xD
  [4]  = zone           0x00 = 品牌 logo / 0x01 = 第二 zone（DPI 指示等）
  [5]  = 0x00           sub-mode（恒）
  [6]  = 0x80           mode flag（恒，"apply"）
  [7]  = 0x00           保留
  [8]  = blue_bright    蓝色 LED 亮度 0x00..0xFF
  [9]  = 0x00           恒 0（无 G 通道，G HUB 永远写 0）
  [10] = 0x00           恒 0（无 R 通道）
  [11..12] = period_ms  BE uint16，0 = 静态，≠0 = 呼吸周期 ms（实测 1000..20000）
  [13..19] = 0

回复:  11 ff 05 5d 00 00 00 00 00 00 ... 00   (echo subID/feat/func，其余 0)
```

**模式判定**：
```c
if (period_ms == 0 && bright != 0)  static_mode(bright);
else if (period_ms == 0)            off_mode();
else                                breathing_mode(bright, period_ms);
```

**两个 zone 能力对称**，都支持 OFF / 静态 / 呼吸。固件必须为两个 zone 各维护独立状态。

**byte[9]/[10] 写非 0 不应报错**——真机不校验，始终 ACK。

---

## 6. 时序复刻表（**必须实现，反作弊核心检测点**）

| 请求 | 真机延迟（mean ± σ） | 复刻策略 |
|---|---|---|
| Root.Ping | < 5 ms | 通用基线 |
| ReportRate.SetReportRate | **4.70 ± 0.23 ms** | 延迟 ~4.7 ms 再 Transmit |
| AdjustableDPI.SetSensorDPI | **4.81 ± 0.02 ms** | 延迟 ~4.8 ms（σ 极小，需用硬件 timer） |
| RGB Set（**内容变化**） | **84.4 ± 1.0 ms** | 延迟 ~84 ms，模拟 LED IC 写入 |
| RGB Set（**no-change skip**） | **4.6 ms** | 与上一次帧 `memcmp` 命中 → 跳过硬件写、4.6ms ACK |
| 两步请求间隔 | ~0.19 ms | 无需特殊处理（USB control 串行） |

**禁用**：在 SET_REPORT callback 内立即 `USBD_LL_Transmit`（< 1 ms 响应）—— STM32 168MHz 比真机快 100×，立即暴露。

**禁用**：`HAL_Delay`——busy-wait + 阻塞 SysTick，会卡住其他 EP0 请求。

**推荐**：DWT cycle counter (`micros()`) + main loop 轮询 deadline。

```c
// SET_REPORT callback（IRQ 上下文）
void on_set_report(uint8_t *buf, uint16_t len) {
    parse_and_build_reply(buf, reply_buf);
    reply_deadline_us = micros() + reply_delay_for(buf);
    reply_pending = true;
}

// main loop（线程上下文）
if (reply_pending && micros() >= reply_deadline_us) {
    USBD_LL_Transmit(&hUsbDevice, HIDPP_LONG_EP, reply_buf, 20);
    reply_pending = false;
}
```

> `USBD_LL_Transmit` **必须**从 main loop 调用，不能从 IRQ / DataOut callback
> （见 memory `project-stm32-usbd-transmit-thread-context`：silent host-side drop）。

---

## 7. G HUB 重新插拔时的探测流（replugin.pcapng）

**主观延迟来源**：装 G HUB 后插鼠标到可移动多 ~1-2s = G HUB 在 USB 枚举完成后
**1.018s** 内发 163 个 HID++ 请求探测设备。

```
13.724 s  USB Device descriptor 第一帧
13.732 s  SET_CONFIGURATION (+0.008s)
13.753 s  HID 驱动接管
13.892 s  G HUB 第一个 HID++ 请求
14.910 s  G HUB 最后一个探测请求
─────────
total enum    = 1.186 s
G HUB probe   = 1.018 s（163 请求 / 6.2 ms 平均间隔）
```

**探测内容**：
- 18 次 Root.Ping（混 sw_id=0xF + 0xD）— 等待固件就绪
- 3 次 Root.GetFeature（查 FeatureSet/DeviceFwVersion/DeviceName 的 idx）
- FeatureSet.GetFeatureID 枚举全部 17 个 feature
- DeviceFwVersion 4 entities 读
- DeviceName 字符串读
- **OnboardProfiles 完整 flash dump**（2 banks × 16 offsets × 16B = 512B）
- ReportRate / DPI / RGB 状态查询

**克隆固件正确响应所有请求 → 与真机延迟体验一致；任一帧错误或超时 → 体验劣化或被识别。**

---

## 8. 反作弊检测点（按可观察性排序）

1. **HID++ 版本必须 `04 02`**（不是 `02 00`）
2. **DeviceName = `"G302 Daedalime"`**（含拼写错误，照抄）
3. **DeviceFwVersion**：4 entities 全照抄，主 FW name3=`"U  "` ver=`0x9100`
4. **FeatureSet 17 个 feature**，顺序与 §4 表一致
5. **bcdDevice = 0x9100** 与 FwVersion 主 FW ver 字段一致
6. **响应延迟**：SetSensorDPI σ 仅 0.02 ms → 必须用硬件 timer 而不是软件 jitter
7. **RGB no-change skip**：同帧重复必须 4.6 ms 响应（不能 84 ms）
8. **OnboardProfiles SetMode** 后 GetMode 必须返回一致 mode
9. **GetProfileDirectory** num_profiles=6（不能 ≠6，否则 UI 异常）
10. **RGB 走 subID 0x05 旧通道**，不是新 LEDControl feature
11. **PixArt sensor entity 存在**（暴露真机硬件型号）
12. **sw_id 同时响应 0xF 和 0xD**

---

## 9. 复刻 TODO（按优先级）

### P0（决定能不能被 G HUB 识别为 G302）
1. Root.Ping 回 `04 02 <echo>`
2. Root.GetFeature 返回正确 idx（至少 0x0001/0x0003/0x0005）
3. FeatureSet.GetCount=17 + GetFeatureID 表
4. DeviceName 返回 `"G302 Daedalime"`
5. DeviceFwVersion 4 entities 全实现
6. **sw_id 0xD 和 0xF 都响应**

### P1（决定能不能配置）
7. AdjustableDPI 全套（List/Get/Set）+ range-step 编码
8. ReportRate.SetReportRate ACK
9. OnboardProfiles SetMode/GetMode + GetProfileDirectory 静态回复
10. MemoryRead16B 返回预填的 512B flash 镜像

### P2（决定 RGB 灯效）
11. Legacy subID 0x05 双 zone 状态机
12. RGB no-change skip + 84ms / 4.6ms 时序

### P3（提高 stealth）
13. 响应延迟用硬件 timer 复刻 4.7 / 4.8 / 84 ms ± σ
14. MouseButtonSpy 返回 `ff` 填充
15. feat 0x02 (DeviceInformation) / 0x05 (DeviceFriendlyName) / 0x10 / 0x11

---

## 10. 私有移动鼠标 opcode（克隆固件已实现）

Stm_g302 固件保留了一个**私有 HID++ Short opcode** 用于外部脚本注入鼠标移动：

```
10 FF FE [dx_lo][dx_hi][dy_lo][dy_hi]
└──┘  └─ feature = 0xFE（私有"move mouse"）
RID    └─ dev_idx 广播
```

收到后从 IF0 EP 0x81 IN 推送 8B 鼠标报文。配套脚本：`scripts/tools/send_hid.py`。

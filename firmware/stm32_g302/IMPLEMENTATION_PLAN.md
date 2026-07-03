# Stm_g302 克隆固件实现计划

**目标**：让 STM32F411 上的 Stm_g302 固件**能被 G HUB 识别成真 G302**，并复刻
真机响应 G HUB 探测、配置 DPI / ReportRate / RGB / OnboardProfiles 的全部行为。
最终通过测试 = 装好 G HUB 后插上固件 → G HUB 显示 G302 + 正常调档 + 灯效正常。

参考：[`HIDPP_PROTOCOL_LITE.md`](HIDPP_PROTOCOL_LITE.md)（精简协议规格）
当前固件入口：[`Core/Src/main.c:100 USBD_G302_HidppReceive`](Core/Src/main.c)

---

## 现状盘点

### 已完成（USB / 描述符层）
- ✅ USB 描述符 byte-for-byte 匹配真机（VID/PID/bcdDevice/字符串/serial）
  → `USB_DEVICE/App/usbd_desc.c`
- ✅ 2-iface HID 配置描述符（IF0 boot mouse + IF1 multimedia+HID++）
  → `USB_DEVICE/Class/G302/usbd_g302.c`
- ✅ HID Report Descriptor 67B + 151B 抄自真机
  → `USB_DEVICE/Class/G302/usb_desc_g302.h`
- ✅ EP0 SET_REPORT 接收路径，过滤 RID 0x10 / 0x11 → `USBD_G302_HidppReceive`
- ✅ EP 0x82 IN 发送 API：`USBD_G302_SendHidpp`
- ✅ 私有 move opcode (`10 FF FE ...`) 注入鼠标移动

### 未做（HID++ 协议层）
- ❌ HID++ 请求都被 `HidppReceive` 接住但**只识别私有 0xFE，其他全部 silent drop**
  → 见 `Core/Src/main.c:100-117`
- ❌ 无响应延迟机制（IRQ → main loop 调度只用于鼠标 dx/dy，未用于 HID++ TX）
- ❌ 无 feature table / state machine

---

## 实现策略

把 HID++ 协议层从 `main.c` 拆出来到独立模块，避免 `main.c` 膨胀。新模块：

```
USB_DEVICE/Class/G302/
  ├── usbd_g302.c / .h        (已有，USB 类层)
  ├── usb_desc_g302.h         (已有，descriptors)
  ├── hidpp_handler.c / .h    (新增，HID++ 协议状态机)
  ├── hidpp_features.c        (新增，每个 feature 的字节级实现)
  └── hidpp_timing.c / .h     (新增，DWT-based us 延迟 + 响应队列)
```

`main.c` 改：把 `USBD_G302_HidppReceive` 委托给 `hidpp_handler_on_request`；
main loop 调用 `hidpp_handler_poll_tx`。

---

## P0：让 G HUB 识别成 G302（必须，否则什么都不工作）

**验收**：装好 G HUB → 插固件 → G HUB UI 出现 G302 设备图标且显示设备名。

### P0-1: hidpp_timing 模块
- DWT cycle counter 初始化（`CoreDebug->DEMCR |= TRCENA; DWT->CTRL |= 1`）
- `uint32_t hidpp_micros(void)` → DWT->CYCCNT / 96
- 单插槽响应队列：`{uint8_t buf[20]; uint8_t len; uint32_t deadline_us; bool pending}`
  - `hidpp_schedule_reply(buf, len, delay_us)` — 从 IRQ 调
  - `hidpp_poll_tx(pdev)` — 从 main loop 调；deadline 到 → `USBD_G302_SendHidpp`
- 单插槽够用（HID++ 是请求-响应模型，G HUB 串行发送）

### P0-2: hidpp_handler 框架
- 解析 `[rid][dev][feat][func<<4|sw]`，分发到 `hidpp_feature_<idx>` handler
- 默认 fallback：发 INVALID_FUNCTION 错误帧
- echo sw_id / device_idx / feat_idx 到响应

### P0-3: Root (feat 0x00) — Ping + GetFeature
- **Ping** (func=1)：回 `04 02 <echo_payload>` (HID++ 4.2)
- **GetFeature** (func=0)：查表返回 `[feat_idx][flags][version]`
  - 编译期静态表：`{0x0000→0, 0x0001→1, 0x0003→3, 0x0005→4, 0x0013→5, 0x2201→13, 0x8060→14, 0x8100→15, 0x8110→16, ...}`
  - 未知 fid 回 `[0][0][0]`
- **关键**：响应 `sw_id=0xD` 和 `sw_id=0xF` 都要工作（G HUB 首 ping 用 0xF）

### P0-4: FeatureSet (feat 0x01)
- **GetCount** (func=0)：回 `0x11` (=17)
- **GetFeatureID(idx)** (func=1)：返回 `[fid_hi][fid_lo][flags]`
  - 复用 P0-3 的表，反向查询
  - 未识别的 0x06..0x0c 先填 `0x18xx` 占位（标记 TODO）

### P0-5: DeviceFwVersion (feat 0x03)
- **GetEntityCount** (func=0)：回 `0x04`
- **GetFwInfo(idx)** (func=1)：返回 4 entities 数据（照抄 `HIDPP_PROTOCOL_LITE.md §5.1`）：
  ```
  idx=0: 00 "U  " 91 00 00 07 00 00 00     ← MainFW
  idx=1: 01 "BOT" 14 00 00 07 00 00 00     ← Bootloader
  idx=2: 02 "HW " 00 00 00 00 00 00 00     ← Hardware
  idx=3: 04 "PIX" 00 00 00 01 00 00 00     ← PixArt sensor
  ```

### P0-6: DeviceName (feat 0x04)
- **GetNameLength** (func=0)：回 `0x13` (=19)
- **GetName(off)** (func=1)：返回 16B 字符串块
  - 全字符串 = `"G302 Daedalime"` (14 char) + 5B padding
  - 注意**拼错**的 `Daedalime`，不是 `Daedalus`

### P0-7: 集成 + 修改 main.c
- `USBD_G302_HidppReceive` → 转发到 `hidpp_handler_on_request(data, len)`
- main loop 加 `hidpp_handler_poll_tx(&hUsbDeviceFS)`
- 保留私有 0xFE move opcode（特殊路径，不走 HID++ feature 分发）

**P0 完成后预期**：插上去 G HUB 能识别为 G302，但 DPI / RGB 调不动。

---

## P1：能配置（DPI / ReportRate / OnboardProfiles 基本）

**验收**：G HUB 拖 DPI slider → 鼠标 LED 闪烁应答（用 diag 计数器看到 SetSensorDPI 被收到）；
"启用板载配置"切换不报错。

### P1-1: ReportRate (feat 0x0e)
- **GetReportRateList** (func=0)：回 bitmask `0x81` (1000Hz + 125Hz)
- **GetReportRate** (func=1)：回 `current_ms`（变量，默认 `0x01`）
- **SetReportRate** (func=2)：保存 `current_ms = req[4]`，ACK
- **响应延迟 ~4.7 ms**（用 hidpp_timing）

### P1-2: AdjustableDPI (feat 0x0d)
- **GetSensorCount** (func=0)：回 `0x01`
- **GetSensorDPIList** (func=1, page=0)：回 `00 00 C8 E0 32 1F 40 00 00 ...`
  - = `[sensor=0][0x00C8=200][0xE032=range step 50][0x1F40=8000][0x0000 终止]`
- **GetSensorDPI** (func=2)：回 `[sensor][cur_hi][cur_lo][def_hi][def_lo]`
  - 默认 cur=0x0640 (1600), def=0x0640
- **SetSensorDPI** (func=3)：保存当前 DPI，**echo 完整 20B Long**
- **响应延迟 ~4.81 ms**（极低 σ，σ < 0.05 ms）

### P1-3: OnboardProfiles (feat 0x0f) — 最小可行
- **GetProfileDirectory** (func=0)：照抄 12B
  ```
  01 01 01 01 01 06 10 01 00 0a 01 00
  ```
- **SetMode** (func=1)：保存 `mode = req[4]`（0x01 onboard / 0x02 host），ACK
- **GetMode** (func=2)：回当前 mode
- **GetActiveProfile** (func=4)：回 `00 01`
- **GetFlashStatus** (func=b)：回 `03`
- **MemoryRead16B** (func=5)：返回预填的 512B flash 镜像中的 16B 块
  - 静态 const 数组：`flash_image[2][16][16]` = `{bank0_offsets, bank1_offsets}`
  - bank=1 mostly `0xFF` (空 profile)
  - bank=0 off=00 填 DPI 表 `80 01 00 02 a4 01 48 03 3c 06 78 0c 00 00 ff ff`
  - 其余按需对齐真机 dump

### P1-4: 通用错误帧 fallback
- 未识别的 feat_idx：回 `[0x10][dev][feat][0x8F][orig_func|sw][0x06][0x00]` (INVALID_FEATURE_INDEX)
- 识别 feat 但未识别 func：err_code=`0x07` (INVALID_FUNCTION)
- 真机 MouseButtonSpy 路径返回全 `0xFF`：见 P3-1

**P1 完成后预期**：G HUB 完整 GUI 功能可用（DPI 滑块、报告率、启用板载配置）。
RGB 仍然不工作（走旧 subID 路径，未实现）。

---

## P2：RGB（蓝色单灯 + 双 zone）

**验收**：G HUB 切 RGB 静态色 / 呼吸 → 通过 diag 看到正确 zone / 亮度 / period 被记录。

### P2-1: Legacy subID 0x05 分发
- `hidpp_handler_on_request` 在 feature 分发之前检查：
  ```c
  if (data[2] == 0x05 && (data[3] >> 4) == 0x5) {
      hidpp_subid05_handle(data, len);
      return;
  }
  ```
- 注意：这是 `subID=0x05` 不是 `feat_idx=0x05`，与 DeviceFriendlyName 在协议层冲突
  - 实际真机如何区分？两个都用 `byte[2]=0x05`
  - 区分依据：**func 字段** — DeviceFriendlyName 用 func=0/1 (`0x0d`/`0x1d` byte[3])，
    RGB 用 func=5 (`0x5d` byte[3])
  - 也可以靠 RID 区分：RGB 全部 Long (0x11)，feature 查询用 Short (0x10)

### P2-2: hidpp_features_rgb 模块
- 状态：`struct { uint8_t bright, sub_mode, mode_flag; uint16_t period_ms; uint8_t valid; } zones[2];`
- 收 RGB Set 帧：
  1. `zone = buf[4]`（0 或 1，越界则 drop）
  2. 解析 `next = {buf[8], buf[5], buf[6], be16(buf[11..12])}`
  3. `changed = !zones[zone].valid || memcmp(&zones[zone], &next, ...) != 0`
  4. 保存 next，记录到 diag
  5. 准备 echo ACK 帧（20B Long，前 4B echo，其余 0）
  6. **changed → 延迟 84 ms + ±1 ms 随机**；**unchanged → 延迟 4.6 ms**
- 不用真控 LED（板上没接灯，但 diag 可读到）

### P2-3: hidpp_timing 增加随机 jitter
- 简单 LCG `static uint32_t rng = 1; rng = rng * 1103515245 + 12345; jitter = rng % 2000 - 1000;`
- 仅给 RGB 改帧用（DPI/ReportRate σ 太小，不要加 jitter）

**P2 完成后预期**：G HUB 灯效配置全功能可见（虽然物理上无 LED 输出）。

---

## P3：提高 stealth / 完整度

**验收**：G HUB 重新插拔体感与真机一致；反作弊驱动（OBS 检测之外）找不到差异。

### P3-1: 占位 feature
- **feat 0x02 DeviceInformation** (func=0)：照抄真机字节，需要补抓
- **feat 0x05 DeviceFriendlyName** (func=0)：返回友好名（如 `"G302 Daedalus Apex"`）
- **feat 0x10 MouseButtonSpy** func=3 路径：返回 12B 全 `0xFF`
- **feat 0x06..0x0c**（0x18xx/0x1exx 系列）：先按 INVALID_FUNCTION 兜底，
  等抓包确认具体语义

### P3-2: 响应延迟精校
- 当前 P0/P1 用固定 mean；P3 加 σ
  - DPI Set: σ = 20 us (用 DWT 噪声足够，不加 jitter)
  - ReportRate Set: σ = 230 us（加 LCG jitter）
  - RGB Set: σ = 1000 us（同上）
- 校验：抓包验证 ±2σ 内

### P3-3: 多请求并发处理
- 当前单插槽响应队列只能服务一个 in-flight 请求
- 如果 G HUB 发出 burst（实测 6.2 ms 间隔，单插槽 + 5 ms 延迟 = 没问题）
  → 暂不动；如果出现 drop 再扩展为环形队列

### P3-4: OnboardProfiles flash 写入
- 当前只读，G HUB 切换 profile 时可能写 flash
- 需要专门抓"修改 profile + 保存到鼠标"的 pcapng 才能定字节
- 实现：写入命令 ACK 即可，不必持久化到 STM32 flash

### P3-5: feat 0x0f 默认 `mode = 0x01` (onboard) 上电
- 真机出厂默认 onboard，固件应保持一致
- 影响：G HUB 首次连接看到 onboard 模式，会执行 §15.5 的"切到 host"流程

---

## 测试 / 验证流程

每个 P 完成后跑同一组测试：

| 测试 | 工具 | 验收 |
|---|---|---|
| 1. 设备识别 | Windows 设备管理器 + USBView | 显示 "Gaming Mouse G302"，描述符 byte-for-byte |
| 2. G HUB UI 出现 | G HUB | 设备列表显示 G302（P0 完成后） |
| 3. DPI 调节 | G HUB + diag counter | SetSensorDPI 被收（P1 后） |
| 4. ReportRate | G HUB | 调到 1000Hz 不报错 |
| 5. OnboardProfile 切换 | G HUB | "启用板载配置"toggle 流畅 |
| 6. RGB 配置 | G HUB + diag counter | 修改颜色/呼吸 → diag 看到 zone/bright/period |
| 7. 重新插拔时延 | 秒表 | 体感与真机一致（~1s 内 UI 显示设备） |
| 8. 反向抓包对比 | USBPcap + 真机 pcapng | 关键帧时序差异 < 2σ |

### diag SRAM 0x20004000 已有的计数器
- `hidpp_recv_cnt` — 收到 HID++ 数
- `hidpp_last_len` / `last_hidpp` — 最后一个帧
- `hidpp_move_cnt` — 私有 0xFE move 计数

**需要添加**：
- `hidpp_reply_cnt` — 已 schedule 响应数
- `hidpp_tx_cnt` — 真实 Transmit 数
- `hidpp_unknown_feat_cnt` / `hidpp_err_reply_cnt`
- `rgb_zone[2]` 状态镜像
- `dpi_current` / `report_rate_current` / `onboard_mode`

非侵入读：`telnet localhost 4444 && mdw 0x20004000 32`（OpenOCD 已在跑时；
否则用 OpenOCD 一次性 `read_memory` —— 见 [`feedback-never-run-openocd`](../../../../.claude/projects/F--Work-Code-PersonalProject-SWEET/memory/feedback-never-run-openocd.md)）。

---

## 风险 / 已知 trap

1. **SET_REPORT IRQ 上下文里 Transmit = silent drop**
   - 见 memory `project-stm32-usbd-transmit-thread-context`
   - 必须用 hidpp_timing 队列从 main loop 发
2. **GET_REPORT 返回非 0 会 deadlock EP1 IN**
   - 见 memory `project-mouse-mover-get-report-trap`
   - 当前 `usbd_g302.c:235-242` 已正确返回单 0 字节，不要改
3. **HAL_Delay 阻塞 SysTick**
   - 当前 main loop 用 `HAL_Delay(1)`，HID++ 响应延迟需用 DWT 而不是再叠一个 HAL_Delay
4. **subID 0x05 与 DeviceFriendlyName feat_idx 0x05 冲突**
   - 区分依据：func 字段（见 P2-1）
   - 实现顺序：先 feature 分发；feature 0x05 的 func≠5 路径走 DeviceFriendlyName；
     func=5 路径走 RGB subID（即使 `byte[2]==feat_idx==0x05`）
5. **flash dump 数据真机 vs 固件不一致 → G HUB 可能报"profile 损坏"**
   - 风险中等：先用全 `0xFF`（空 profile）+ 我们抓到的 bank=0 off=00 DPI 表
   - 如果 G HUB 拒绝，再细化抓包

---

## 实现顺序与里程碑

```
M1 (P0 完成)  G HUB 识别为 G302               预计 1-2 个开发会话
M2 (P1 完成)  G HUB 全配置功能可用            预计 2-3 个会话
M3 (P2 完成)  RGB 灯效配置 OK                 预计 1 个会话
M4 (P3 完成)  反作弊抗压稳定                  预计 2+ 会话（需多轮抓包对比）
```

每个 milestone 完成后 commit 一次，方便 bisect。

---

## 不在本计划范围

- ✗ 真实 RGB LED 输出（板上无硬件）
- ✗ PixArt sensor 集成（无传感器硬件，DPI 只在协议层模拟）
- ✗ Button mapping / Macro 实现（不在 G302 三大基本功能内）
- ✗ G HUB 之外的反作弊（如 EAC / Vanguard 的 USB 驱动级检测）
  → 待后续单独立项

---

## 下一步

完成本计划文档后，按用户指示决定：
- (a) 立即开始 P0 实现 → 先创建 `hidpp_timing.c/h` + `hidpp_handler.c/h` 骨架
- (b) 先提交本计划 + `HIDPP_PROTOCOL_LITE.md` + `HIDPP_PROTOCOL.md` 三件套，
  形成稳定基线再开工
- (c) 抓包补 §3 中未识别的 0x06..0x0c feature 后再开工

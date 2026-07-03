# TODO

## 鼠标回报率限制（1000Hz 鼠标只收到 125Hz）

**现象**：鼠标硬件为 1000Hz 回报率，但 RawInput `WM_INPUT` 收到的移动事件只有 ~125Hz。
已通过 `mouse_rate:` 诊断日志确认：
- `move_events`（RawInput 原始移动事件）= 125/秒 ← 系统层就只有 125Hz
- `drains`（worker drain 次数）= ~510/秒 ← 软件层无瓶颈，零丢事件

**结论**：不是软件问题，是鼠标/驱动/USB 层实际跑在 125Hz。软件层（RawInput + drain）
已到顶，drain 频率远高于事件率，无丢失。

**待办**（非代码，需在系统/硬件层解决）：
- [ ] 确认鼠标厂商软件（G Hub / Synapse 等）是否把回报率设为 1000Hz
- [ ] 检查鼠标硬件是否有回报率切换开关（125/500/1000Hz）
- [ ] 设备管理器 → 鼠标 → 属性 → 高级，看有无轮询率选项
- [ ] 关闭 Windows USB 选择性挂起（电源选项 → USB 设置）
- [ ] 用独立工具（如 Mouse Rate Test）验证真实回报率，排除 RN_AI 因素

**诊断日志**：`mouse_input.cpp` 里的 `mouse_rate:` 每秒输出 raw_events / move_events / drains，
保留用于后续验证回报率提升后是否生效。

// test/test_pid.cpp
//
// PID 控制器单测 —— 对应 plan.md 的 14 项指标 M1-M14。
// 纯算法、零外部依赖：cl /std:c++17 /EHsc /utf-8 /I. test\test_pid.cpp
//   RN_AI_cpp\mouse\pid_controller.cpp /Fe:test_pid.exe
//
// 沿用项目裸 if/printf/return 风格（无 GoogleTest/Catch2）。
// 任一指标失败 g_fail=1，main 返回 1；全过返回 0。
//
// 注意：compute() 现含 _apply_smoothing 层（移植自 Python pid.py）。
// 本套单测只验 P/I/D 数学，故每个用例构造后调 bypass_smoothing() 设
// smooth_x=smooth_y=1.0（直通：输出=raw P/I/D），把 smoothing 层短路掉。

#include "RN_AI_cpp/mouse/pid_controller.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <utility>

static int g_fail = 0;

// 直通 smoothing：final = 1*step + 0*Δerror = raw PID 输出
static void bypass_smoothing(DualAxisPID& p)
{
    p.set_smooth_params(1.0, 1.0, 0.0, 1.0);
}

#define EXPECT_NEAR(a, b, eps, name) do { \
    double _da = (double)(a); double _db = (double)(b); double _de = (double)(eps); \
    if (std::fabs(_da - _db) > _de) { \
        std::printf("[FAIL] %s: got %.12g, want %.12g (eps %.3g)\n", name, _da, _db, _de); \
        g_fail = 1; \
    } else { std::printf("[ OK ] %s\n", name); } } while (0)

#define EXPECT_TRUE(c, name) do { if (!(c)) { \
    std::printf("[FAIL] %s\n", name); g_fail = 1; \
} else { std::printf("[ OK ] %s\n", name); } } while (0)

static const double DT = 1.0 / 60.0;

// ── M1: P 项公式 ────────────────────────────────────────────────────
static void test_p_term()
{
    DualAxisPID pid(0.4, 0.4, 0, 0, 0, 0, 0, 0, nullptr, nullptr,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0); // deadzone=0
    bypass_smoothing(pid);
    auto [ox, oy] = pid.compute(100.0, 0.0, DT);
    EXPECT_NEAR(ox, 40.0, 1e-9, "M1 P-term output_x");
    EXPECT_NEAR(oy, 0.0, 1e-9, "M1 P-term output_y");
}

// ── M2: I 项累积 ────────────────────────────────────────────────────
static void test_i_term()
{
    DualAxisPID pid(0, 0, 0.001, 0.001, 0, 0, 0, 0, nullptr, nullptr,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(pid);
    for (int i = 0; i < 10; ++i) pid.compute(100.0, 0.0, DT);
    auto c = pid.get_components_x();
    EXPECT_NEAR(c.i, 0.001 * 100.0 * 10.0 * DT, 1e-9, "M2 I-term accumulation");
}

// ── M3: D 项公式 ────────────────────────────────────────────────────
static void test_d_term()
{
    DualAxisPID pid(0, 0, 0, 0, 0.05, 0.05, 0, 0, nullptr, nullptr,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(pid);
    auto [ox, oy] = pid.compute(100.0, 0.0, DT); // 首拍 last_error=0
    auto c = pid.get_components_x();
    EXPECT_NEAR(c.d, 0.05 * 100.0 / DT, 1e-9, "M3 D-term component");
    EXPECT_NEAR(ox, 0.05 * 100.0 / DT, 1e-9, "M3 D-term output");
}

// ── M4: 确定性 ──────────────────────────────────────────────────────
static void test_determinism()
{
    const std::vector<double> errs = { 100, 50, 30, -20, 10, 0, 80, -40, 25, -10 };
    DualAxisPID p1(0.4, 0.4, 0.001, 0.001, 0.05, 0.05, 100, 100, nullptr, nullptr,
                   DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    DualAxisPID p2(0.4, 0.4, 0.001, 0.001, 0.05, 0.05, 100, 100, nullptr, nullptr,
                   DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(p1); bypass_smoothing(p2);
    bool ok = true;
    for (double e : errs)
    {
        auto a = p1.compute(e, 0.0, DT);
        auto b = p2.compute(e, 0.0, DT);
        if (std::fabs(a.first - b.first) > 1e-12 || std::fabs(a.second - b.second) > 1e-12)
            ok = false;
    }
    EXPECT_TRUE(ok, "M4 determinism (identical runs)");
}

// ── M5: 闭环收敛（纯 P）─────────────────────────────────────────────
static void test_closed_loop_pure_p()
{
    DualAxisPID pid(0.5, 0.5, 0, 0, 0, 0, 0, 0, nullptr, nullptr,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(pid);
    const double plant = 1.0;
    double ex = 100.0, ey = 0.0;
    bool crossed = false; int ticks = 0;
    for (int i = 0; i < 60; ++i)
    {
        auto [ox, oy] = pid.compute(ex, ey, DT);
        ex -= ox * plant; ey -= oy * plant;
        ticks = i + 1;
        if (ex < 0.0) crossed = true;   // 越过 0 = 超调
        if (std::fabs(ex) < 1.0) break;
    }
    EXPECT_TRUE(std::fabs(ex) < 1.0, "M5 settled < 1px");
    EXPECT_TRUE(ticks <= 7, "M5 settled <= 7 ticks");
    EXPECT_TRUE(!crossed, "M5 no overshoot (no sign flip)");
}

// ── M6: 超调有界（D + output_limits）────────────────────────────────
static void test_overshoot_bounded()
{
    const double lim[2] = { -50.0, 50.0 };
    DualAxisPID pid(0.5, 0.5, 0, 0, 0.005, 0.005, 0, 0, lim, lim,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(pid);
    const double plant = 1.0;
    double ex = 100.0, ey = 0.0;
    double max_abs = 0.0, max_overshoot = 0.0;
    double e_at_tick12 = 0.0, e_at_end = 0.0;
    for (int i = 0; i < 30; ++i)
    {
        auto [ox, oy] = pid.compute(ex, ey, DT);
        ex -= ox * plant; ey -= oy * plant;
        double abs_e = std::fabs(ex);
        if (abs_e > max_abs) max_abs = abs_e;
        if (-ex > max_overshoot) max_overshoot = -ex; // 反向越过量
        if (i == 11) e_at_tick12 = ex;                // 第 12 拍（200ms @60fps）
        e_at_end = ex;
    }
    EXPECT_TRUE(std::fabs(e_at_tick12) < 5.0, "M6 settled < 5px within 200ms");
    EXPECT_TRUE(std::fabs(e_at_end) < 5.0, "M6 remains < 5px at end");
    EXPECT_TRUE(max_overshoot < 25.0, "M6 overshoot < 25%");
    EXPECT_TRUE(max_abs <= 125.0, "M6 no divergence (bounded)");
}

// ── M7: 抗饱和 Freeze ───────────────────────────────────────────────
static void test_anti_windup_freeze()
{
    const double lim[2] = { -50.0, 50.0 };
    DualAxisPID pid(0.5, 0.5, 0.5, 0.5, 0, 0, 100, 100, lim, lim,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(pid);
    bool bounded = true, saturated_high = true;
    for (int i = 0; i < 50; ++i)
    {
        auto [ox, oy] = pid.compute(1000.0, 0.0, DT);
        if (std::fabs(pid.get_components_x().i) > 100.0 + 1e-6) bounded = false;
        if (std::fabs(ox - 50.0) > 1e-9) saturated_high = false;
    }
    EXPECT_TRUE(bounded, "M7 freeze |i_term| <= windup_guard");
    EXPECT_TRUE(saturated_high, "M7 freeze output saturated at +50");
    // 误差反转后应立刻响应（P 主导，1 拍内 output<0）
    auto [ox2, oy2] = pid.compute(-1000.0, 0.0, DT);
    EXPECT_TRUE(ox2 < 0.0, "M7 freeze responds within 1 tick after reversal");
    bool bounded2 = std::fabs(pid.get_components_x().i) <= 100.0 + 1e-6;
    EXPECT_TRUE(bounded2, "M7 freeze |i_term| bounded after reversal");
}

// ── M8: 抗饱和 BackCalc ─────────────────────────────────────────────
static void test_anti_windup_backcalc()
{
    const double lim[2] = { -50.0, 50.0 };
    DualAxisPID pid(0.5, 0.5, 0.5, 0.5, 0, 0, 100, 100, lim, lim,
                    DualAxisPID::AntiWindup::BackCalc, 0.1, 0.1, 0.0);
    bypass_smoothing(pid);
    bool bounded = true;
    for (int i = 0; i < 50; ++i)
    {
        pid.compute(1000.0, 0.0, DT);
        if (std::fabs(pid.get_components_x().i) > 100.0 + 1e-6) bounded = false;
    }
    EXPECT_TRUE(bounded, "M8 backcalc |i_term| <= windup_guard");
    auto [ox2, oy2] = pid.compute(-1000.0, 0.0, DT);
    EXPECT_TRUE(ox2 < 0.0, "M8 backcalc responds within 1 tick after reversal");
    bool bounded2 = std::fabs(pid.get_components_x().i) <= 100.0 + 1e-6;
    EXPECT_TRUE(bounded2, "M8 backcalc |i_term| bounded after reversal");
}

// ── M9: windup_guard clamp（无 output_limits）──────────────────────
static void test_windup_guard()
{
    DualAxisPID pid(0, 0, 0.5, 0.5, 0, 0, 20, 20, nullptr, nullptr,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(pid);
    for (int i = 0; i < 1000; ++i) pid.compute(100.0, 0.0, DT);
    EXPECT_TRUE(std::fabs(pid.get_components_x().i) <= 20.0 + 1e-6, "M9 windup_guard clamps i_term");
}

// ── M10: reset 清状态 ───────────────────────────────────────────────
static void test_reset()
{
    DualAxisPID pid(0.4, 0.4, 0.01, 0.01, 0.05, 0.05, 0, 0, nullptr, nullptr,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(pid);
    for (int i = 0; i < 5; ++i) pid.compute(100.0, 0.0, DT); // 累积 I/D
    pid.reset();
    auto after = pid.compute(100.0, 0.0, DT);

    DualAxisPID fresh(0.4, 0.4, 0.01, 0.01, 0.05, 0.05, 0, 0, nullptr, nullptr,
                      DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(fresh);
    auto first = fresh.compute(100.0, 0.0, DT);

    EXPECT_NEAR(after.first, first.first, 1e-9, "M10 output after reset == fresh first");
    EXPECT_NEAR(pid.get_components_x().i, 0.01 * 100.0 * DT, 1e-12, "M10 i_term cleared by reset");
    EXPECT_NEAR(pid.get_components_x().d, 0.05 * 100.0 / DT, 1e-9, "M10 last_error cleared (D on first tick)");
}

// ── M11: 死区缩放 ───────────────────────────────────────────────────
static void test_deadzone()
{
    DualAxisPID pid(0.4, 0.4, 0, 0, 0, 0, 0, 0, nullptr, nullptr,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 5.0); // deadzone=5
    bypass_smoothing(pid);
    {
        auto [ox, oy] = pid.compute(3.0, 0.0, DT); // factor=max(0.1, 3/5)=0.6
        EXPECT_NEAR(ox, 0.4 * 3.0 * 0.6, 1e-9, "M11 deadzone e=3 scaled by e/5");
    }
    {
        auto [ox, oy] = pid.compute(0.0, 0.0, DT); // P=0 -> output 0
        EXPECT_NEAR(ox, 0.0, 1e-9, "M11 deadzone e=0 output 0");
    }
    {
        auto [ox, oy] = pid.compute(0.5, 0.0, DT); // factor=max(0.1, 0.1)=0.1 (floor)
        EXPECT_NEAR(ox, 0.4 * 0.5 * 0.1, 1e-9, "M11 deadzone floor 0.1");
    }
}

// ── M12: 轴独立 ─────────────────────────────────────────────────────
static void test_axis_independence()
{
    DualAxisPID pid(0.4, 0.4, 0, 0, 0, 0, 0, 0, nullptr, nullptr,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(pid);
    {
        auto [ox, oy] = pid.compute(100.0, 0.0, DT);
        EXPECT_NEAR(oy, 0.0, 1e-12, "M12 y output 0 when ey=0");
    }
    pid.set_pid_params(0.8, 0.4, 0, 0, 0, 0); // 改 kp_x
    {
        auto [ox, oy] = pid.compute(100.0, 0.0, DT);
        EXPECT_NEAR(oy, 0.0, 1e-12, "M12 y independent of kp_x");
        EXPECT_NEAR(ox, 80.0, 1e-9, "M12 x reflects new kp_x");
    }
}

// ── M13: output_limits 末端 clamp ───────────────────────────────────
static void test_output_limits()
{
    const double lim[2] = { -50.0, 50.0 };
    DualAxisPID pid(1.0, 1.0, 0, 0, 0, 0, 0, 0, lim, lim,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(pid);
    {
        auto [ox, oy] = pid.compute(10000.0, 0.0, DT);
        EXPECT_NEAR(ox, 50.0, 1e-9, "M13 high clamp");
    }
    {
        auto [ox, oy] = pid.compute(-10000.0, 0.0, DT);
        EXPECT_NEAR(ox, -50.0, 1e-9, "M13 low clamp");
    }
}

// ── M14: X/Y 独立增益 ───────────────────────────────────────────────
static void test_per_axis_gains()
{
    DualAxisPID pid(0.4, 0.6, 0, 0, 0, 0, 0, 0, nullptr, nullptr,
                    DualAxisPID::AntiWindup::Freeze, 0, 0, 0.0);
    bypass_smoothing(pid);
    auto [ox, oy] = pid.compute(100.0, 100.0, DT); // magnitude>5, no deadzone
    EXPECT_NEAR(ox, 40.0, 1e-9, "M14 x gain kp_x*100");
    EXPECT_NEAR(oy, 60.0, 1e-9, "M14 y gain kp_y*100");
}

int main()
{
    std::printf("=== PID controller tests ===\n");
    test_p_term();                 // M1
    test_i_term();                 // M2
    test_d_term();                 // M3
    test_determinism();            // M4
    test_closed_loop_pure_p();     // M5
    test_overshoot_bounded();      // M6
    test_anti_windup_freeze();     // M7
    test_anti_windup_backcalc();   // M8
    test_windup_guard();           // M9
    test_reset();                  // M10
    test_deadzone();               // M11
    test_axis_independence();      // M12
    test_output_limits();          // M13
    test_per_axis_gains();         // M14
    std::printf(g_fail ? "\n=== PID TESTS FAILED ===\n" : "\n=== PID TESTS PASSED ===\n");
    return g_fail;
}

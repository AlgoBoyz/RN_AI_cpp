// test/test_pid_demo.cpp
//
// 把 draw_pid_demo_canvas 的闭环逻辑原样搬到命令行，脱离 ImGui/overlay，
// 逐帧打印 红点(目标)/绿点(光标)/误差/PID输出，定位 demo 乱飞根因。
//
// 编译（VS 开发者命令行）:
//   cl /std:c++17 /EHsc /I. test\test_pid_demo.cpp RN_AI_cpp\mouse\pid_controller.cpp /Fe:test_pid_demo.exe
// 运行: test_pid_demo.exe

#include "RN_AI_cpp/mouse/pid_controller.h"

#include <cstdio>
#include <cmath>

int main()
{
    // —— 与 draw_pid_demo_canvas 完全一致的 PID 配置（config.pid_* 默认值）——
    // kp=0.4 ki=0.001 kd=0.05 windup=100 deadzone=5 无 output_limit
    DualAxisPID pid;
    pid.set_pid_params(0.4, 0.4, 0.001, 0.001, 0.05, 0.05);
    pid.set_windup_guard(100.0, 100.0);
    pid.set_anti_windup(DualAxisPID::AntiWindup::Freeze, 0.1, 0.1);
    pid.set_deadzone(5.0);
    pid.set_output_limits(nullptr, nullptr);
    pid.reset();

    // —— 与 demo 一致的闭环 ——
    const double dt = 1.0 / 60.0;                 // demo 的 dt 钳位值
    const double rad = 70.0;
    const double PI2 = 2.0 * 3.14159265358979323846;
    double angle = 0.0;
    double cx = 0.0, cy = 0.0;                    // 光标，初值与 demo 一致 (0,0)

    std::printf("frame  ang    tgt=(tx,ty)        cur=(cx,cy)          err=(ex,ey)         out=(ox,oy)          |err|\n");
    for (int i = 0; i < 300; ++i)
    {
        angle += dt * 1.0;
        if (angle > PI2) angle -= PI2;
        double tx = std::cos(angle) * rad;
        double ty = std::sin(angle) * rad;
        double ex = tx - cx, ey = ty - cy;
        auto out = pid.compute(ex, ey, dt);
        cx += out.first;
        cy += out.second;

        double errmag = std::hypot(ex, ey);
        std::printf("%4d  %.3f  (%7.2f,%7.2f)   (%10.2f,%10.2f)   (%8.2f,%8.2f)   (%9.2f,%9.2f)   %8.2f\n",
                    i, angle, tx, ty, cx, cy, ex, ey, out.first, out.second, errmag);

        if (std::fabs(cx) > 1e7 || std::fabs(cy) > 1e7) {
            std::printf(">>> DIVERGED at frame %d\n", i);
            break;
        }
    }
    return 0;
}

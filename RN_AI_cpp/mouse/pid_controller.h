// mouse/pid_controller.h
//
// 双轴 PID 控制器 —— 移植自 Python RN_AI/src/pid.py (DualAxisPID)。
//
// 关键改造（相对 Python 原版）：
//   - dt 由调用方传入（原版用 time.time()），保证确定性、可单测。
//   - 未移植 _apply_smoothing / is_uniform_motion / error_history 启发式层
//     （C++ 已有自己的 smoothing 路径；保持 PID 纯净，后续按需再加）。
//
// 输出 (out_x, out_y) 即鼠标移动计数，等价于 Python 版 execute_move 的 dx/dy。
// 零外部依赖（仅 <cmath> <algorithm> <array> <utility>），可裸编单测。
#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include <utility>
#include <array>
#include <deque>

class DualAxisPID
{
public:
    enum class AntiWindup { Freeze = 0, BackCalc = 1 };

    // 默认构造：inert（所有增益 0、无限幅、死区 0），输出恒 0。
    // 作为类成员时先默认构造，再用 setters / configure 配置。
    DualAxisPID() = default;

    // out_lim_x / out_lim_y: nullptr = 该轴不限幅；否则指向 [min, max]。
    DualAxisPID(double kp_x, double kp_y,
                double ki_x, double ki_y,
                double kd_x, double kd_y,
                double windup_x, double windup_y,
                const double* out_lim_x,
                const double* out_lim_y,
                AntiWindup mode = AntiWindup::Freeze,
                double backcalc_gain_x = 0.0,
                double backcalc_gain_y = 0.0,
                double deadzone = 5.0);

    // 返回 (out_x, out_y) 鼠标移动计数。dt 单位秒。
    std::pair<double, double> compute(double error_x, double error_y, double dt);

    void reset();

    void set_pid_params(double kpx, double kpy, double kix, double kiy, double kdx, double kdy);
    void set_windup_guard(double wx, double wy);
    void set_output_limits(const double* lx, const double* ly); // nullptr 清除该轴限幅
    void set_anti_windup(AntiWindup mode, double bcx, double bcy);
    void set_deadzone(double dz);
    void set_smooth_params(double sx, double sy, double sdz, double salg);

    struct Components { double p, i, d; };
    Components get_components_x() const { return { _p_term[0], _i_term[0], _d_term[0] }; }
    Components get_components_y() const { return { _p_term[1], _i_term[1], _d_term[1] }; }

private:
    // axis: 0=x, 1=y
    double calc_axis(int axis, double error, double dt);
    double apply_limits_and_anti_windup(int axis, double unsat);
    void   final_deadzone_scale(double& ox, double& oy, double ex, double ey) const;
    std::pair<double, double> apply_smoothing(double x_out, double y_out, double error_x, double error_y, double dt);

    // 参数（每轴）
    std::array<double, 2> _kp{ 0.0, 0.0 };
    std::array<double, 2> _ki{ 0.0, 0.0 };
    std::array<double, 2> _kd{ 0.0, 0.0 };
    std::array<double, 2> _windup{ 0.0, 0.0 };      // 积分 clamp 上限（>0 生效，0=不 clamp）
    std::array<double, 2> _backcalc_gain{ 0.0, 0.0 };
    std::array<bool, 2>   _has_limit{ false, false };
    std::array<double, 2> _out_min{ 0.0, 0.0 };
    std::array<double, 2> _out_max{ 0.0, 0.0 };
    AntiWindup            _mode{ AntiWindup::Freeze };
    double                _deadzone{ 0.0 };          // 末端死区半径（px），0=禁用

    // 状态
    std::array<double, 2> _i_term{ 0.0, 0.0 };
    std::array<double, 2> _last_error{ 0.0, 0.0 };
    std::array<double, 2> _last_integral_inc{ 0.0, 0.0 };
    std::array<double, 2> _p_term{ 0.0, 0.0 };
    std::array<double, 2> _d_term{ 0.0, 0.0 };

    // _apply_smoothing 状态（移植自 Python pid.py:157-222）
    // _err_hist 每项 {ex, ey, dt}，dt 为该拍间隔（等价 Python time_history 差）。
    // 瞬态（history 不足 / 非匀速）用 Δerror 混合输出压住 D 项发散；匀速时切回 PID 输出。
    std::deque<std::array<double, 3>> _err_hist;
    static constexpr int _history_size = 20;
    double _smooth_x{ 0.0 }, _smooth_y{ 0.0 }, _smooth_deadzone{ 0.0 }, _smooth_algorithm{ 1.0 };
    double _uniform_threshold{ 1.5 };
    double _min_velocity_threshold{ 10.0 };
    double _max_velocity_threshold{ 100.0 };
};

#endif // PID_CONTROLLER_H

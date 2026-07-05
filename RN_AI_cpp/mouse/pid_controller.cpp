// mouse/pid_controller.cpp
//
// 忠实移植 Python RN_AI/src/pid.py (DualAxisPID) 的核心控制逻辑：
//   - P/I/D 三项（_calculate_output）
//   - 抗饱和两模式：Freeze（饱和时减回本拍积分增量）/ BackCalc（反算）
//   - windup_guard 积分 clamp（calc_axis 内每拍生效）
//   - 末端死区缩放（|error|<deadzone 时按 error/deadzone 缩放，min 0.1）
//   - output_limits 末端再 clamp
// 未移植 _apply_smoothing 启发式层。

#include "pid_controller.h"

#include <cmath>
#include <algorithm>
#include <vector>

DualAxisPID::DualAxisPID(double kp_x, double kp_y,
                         double ki_x, double ki_y,
                         double kd_x, double kd_y,
                         double windup_x, double windup_y,
                         const double* out_lim_x,
                         const double* out_lim_y,
                         AntiWindup mode,
                         double backcalc_gain_x,
                         double backcalc_gain_y,
                         double deadzone)
    : _kp{ kp_x, kp_y }
    , _ki{ ki_x, ki_y }
    , _kd{ kd_x, kd_y }
    , _windup{ windup_x, windup_y }
    , _backcalc_gain{ backcalc_gain_x, backcalc_gain_y }
    , _mode(mode)
    , _deadzone(deadzone)
{
    _has_limit[0] = (out_lim_x != nullptr);
    _has_limit[1] = (out_lim_y != nullptr);
    if (_has_limit[0]) { _out_min[0] = out_lim_x[0]; _out_max[0] = out_lim_x[1]; }
    if (_has_limit[1]) { _out_min[1] = out_lim_y[0]; _out_max[1] = out_lim_y[1]; }
    reset();
}

void DualAxisPID::reset()
{
    _i_term = { 0.0, 0.0 };
    _last_error = { 0.0, 0.0 };
    _last_integral_inc = { 0.0, 0.0 };
    _p_term = { 0.0, 0.0 };
    _d_term = { 0.0, 0.0 };
    _err_hist.clear();
}

double DualAxisPID::calc_axis(int axis, double error, double dt)
{
    // P
    _p_term[axis] = _kp[axis] * error;

    // I（累加 + windup_guard clamp）
    double inc = _ki[axis] * error * dt;
    _i_term[axis] += inc;
    _last_integral_inc[axis] = inc;
    if (_windup[axis] > 0.0)
    {
        if (_i_term[axis] > _windup[axis]) _i_term[axis] = _windup[axis];
        else if (_i_term[axis] < -_windup[axis]) _i_term[axis] = -_windup[axis];
    }

    // D（对误差微分；首拍 last_error=0）
    if (dt > 0.0)
        _d_term[axis] = _kd[axis] * ((error - _last_error[axis]) / dt);
    else
        _d_term[axis] = 0.0;

    return _p_term[axis] + _i_term[axis] + _d_term[axis];
}

double DualAxisPID::apply_limits_and_anti_windup(int axis, double unsat)
{
    double value = unsat;
    bool saturated = false;

    if (_has_limit[axis])
    {
        if (value > _out_max[axis]) { value = _out_max[axis]; saturated = true; }
        else if (value < _out_min[axis]) { value = _out_min[axis]; saturated = true; }
    }

    if (saturated)
    {
        if (_mode == AntiWindup::BackCalc)
            _i_term[axis] += _backcalc_gain[axis] * (value - unsat);
        else // Freeze：撤销本拍积分增量
            _i_term[axis] -= _last_integral_inc[axis];

        if (_windup[axis] > 0.0)
        {
            if (_i_term[axis] > _windup[axis]) _i_term[axis] = _windup[axis];
            else if (_i_term[axis] < -_windup[axis]) _i_term[axis] = -_windup[axis];
        }
    }
    return value;
}

void DualAxisPID::final_deadzone_scale(double& ox, double& oy, double ex, double ey) const
{
    if (_deadzone <= 0.0) return;
    double mag = std::hypot(ex, ey);
    if (mag < _deadzone)
    {
        double factor = std::max(0.1, mag / _deadzone);
        ox *= factor;
        oy *= factor;
    }
}

std::pair<double, double> DualAxisPID::compute(double error_x, double error_y, double dt)
{
    double x_unsat = calc_axis(0, error_x, dt);
    double y_unsat = calc_axis(1, error_y, dt);

    double x_out = apply_limits_and_anti_windup(0, x_unsat);
    double y_out = apply_limits_and_anti_windup(1, y_unsat);

    // _apply_smoothing（Python pid.py:267）—— 瞬态用 Δerror 压住 D 项发散
    auto sm = apply_smoothing(x_out, y_out, error_x, error_y, dt);
    x_out = sm.first;
    y_out = sm.second;

    // 末端死区缩放（Python pid.py:268-272）
    final_deadzone_scale(x_out, y_out, error_x, error_y);

    // 死区缩放可能超出 limits，末端再 clamp 一次（Python pid.py:274-283）
    if (_has_limit[0]) x_out = std::clamp(x_out, _out_min[0], _out_max[0]);
    if (_has_limit[1]) y_out = std::clamp(y_out, _out_min[1], _out_max[1]);

    _last_error[0] = error_x;
    _last_error[1] = error_y;

    return { x_out, y_out };
}

void DualAxisPID::set_pid_params(double kpx, double kpy, double kix, double kiy, double kdx, double kdy)
{
    _kp = { kpx, kpy };
    _ki = { kix, kiy };
    _kd = { kdx, kdy };
}

void DualAxisPID::set_windup_guard(double wx, double wy)
{
    _windup = { wx, wy };
}

void DualAxisPID::set_output_limits(const double* lx, const double* ly)
{
    _has_limit[0] = (lx != nullptr);
    _has_limit[1] = (ly != nullptr);
    if (_has_limit[0]) { _out_min[0] = lx[0]; _out_max[0] = lx[1]; }
    if (_has_limit[1]) { _out_min[1] = ly[0]; _out_max[1] = ly[1]; }
}

void DualAxisPID::set_anti_windup(AntiWindup mode, double bcx, double bcy)
{
    _mode = mode;
    _backcalc_gain = { bcx, bcy };
}

void DualAxisPID::set_deadzone(double dz)
{
    _deadzone = dz;
}

// _apply_smoothing（移植自 Python pid.py:157-222）
// 瞬态（history 不足 / 非匀速）时用 Δerror 混合/替换 PID 输出，压住 D 项发散；
// 匀速跟踪时切回原始 PID 输出 × smooth_algorithm。
std::pair<double, double> DualAxisPID::apply_smoothing(double x_out, double y_out,
                                                       double error_x, double error_y, double dt)
{
    double error_distance = std::hypot(error_x, error_y);
    if (error_distance <= _smooth_deadzone)
        return { x_out, y_out };

    _err_hist.push_back({ error_x, error_y, dt });
    if (static_cast<int>(_err_hist.size()) > _history_size)
        _err_hist.pop_front();

    // is_uniform_motion（Python pid.py:172-190）
    bool uniform = false;
    const int need = std::min(3, _history_size);
    if (static_cast<int>(_err_hist.size()) >= need)
    {
        std::vector<double> velocities;
        for (size_t i = 1; i < _err_hist.size(); ++i)
        {
            double dti = _err_hist[i][2];
            if (dti <= 0.0) continue;
            double dx = _err_hist[i][0] - _err_hist[i - 1][0];
            double dy = _err_hist[i][1] - _err_hist[i - 1][1];
            double vx = dx / dti, vy = dy / dti;
            velocities.push_back(std::sqrt(vx * vx + vy * vy));
        }
        if (!velocities.empty())
        {
            double sum = 0.0;
            for (double v : velocities) sum += v;
            double avg_v = sum / static_cast<double>(velocities.size());
            if (avg_v >= _min_velocity_threshold && avg_v <= _max_velocity_threshold)
            {
                double var = 0.0;
                for (double v : velocities) { double d = v - avg_v; var += d * d; }
                var /= static_cast<double>(velocities.size());
                uniform = (var < _uniform_threshold);
            }
        }
    }

    double step_x = x_out, step_y = y_out;
    if (uniform)
    {
        double comp = std::max(1.0, _smooth_algorithm);
        return { step_x * comp, step_y * comp };
    }

    double dx_err = 0.0, dy_err = 0.0;
    if (_err_hist.size() >= 2)
    {
        const auto& prev = _err_hist[_err_hist.size() - 2];  // error_history[-2]
        dx_err = error_x - prev[0];
        dy_err = error_y - prev[1];
    }
    double s_x = std::max(0.0, std::min(1.0, _smooth_x));
    double s_y = std::max(0.0, std::min(1.0, _smooth_y));
    double final_x = s_x * step_x + (1.0 - s_x) * dx_err;
    double final_y = s_y * step_y + (1.0 - s_y) * dy_err;
    return { final_x, final_y };
}

void DualAxisPID::set_smooth_params(double sx, double sy, double sdz, double salg)
{
    _smooth_x = sx;
    _smooth_y = sy;
    _smooth_deadzone = sdz;
    _smooth_algorithm = salg;
}

#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include "imgui/imgui.h"
#include <imgui_internal.h>

#include "rn_ai_cpp.h"
#include "include/other_tools.h"
#include "kmbox_net/picture.h"
#include "async_logger.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <share.h>

int prev_fovX = config.fovX;
int prev_fovY = config.fovY;
float prev_minSpeedMultiplier = config.minSpeedMultiplier;
float prev_maxSpeedMultiplier = config.maxSpeedMultiplier;
float prev_predictionInterval = config.predictionInterval;
float prev_snapRadius = config.snapRadius;
float prev_nearRadius = config.nearRadius;
float prev_speedCurveExponent = config.speedCurveExponent;
float prev_snapBoostFactor = config.snapBoostFactor;

int prev_smoothness = config.smoothness;
static bool  prev_use_smoothing = config.use_smoothing;
static bool  prev_use_kalman = config.use_kalman;
static bool  prev_tracking_smoothing = config.tracking_smoothing;
float prev_kalman_process_noise = config.kalman_process_noise;
float prev_kalman_measure_noise = config.kalman_measurement_noise;
float prev_reset_threshold = config.resetThreshold;
int   prev_prediction_mode = config.prediction_mode;
float prev_prediction_lead_ms = config.prediction_kalman_lead_ms;
float prev_prediction_max_lead_ms = config.prediction_kalman_max_lead_ms;
float prev_prediction_velocity_smoothing = config.prediction_velocity_smoothing;
float prev_prediction_velocity_scale = config.prediction_velocity_scale;
float prev_prediction_kalman_q = config.prediction_kalman_process_noise;
float prev_prediction_kalman_r = config.prediction_kalman_measurement_noise;
bool  prev_prediction_use_future_for_aim = config.prediction_use_future_for_aim;
bool  prev_camera_compensation_enabled = config.camera_compensation_enabled;
float prev_camera_compensation_max_shift = config.camera_compensation_max_shift;
float prev_camera_compensation_strength = config.camera_compensation_strength;

bool  prev_wind_mouse_enabled = config.wind_mouse_enabled;
float prev_wind_G = config.wind_G;
float prev_wind_W = config.wind_W;
float prev_wind_M = config.wind_M;
float prev_wind_D = config.wind_D;

bool  prev_triggerbot = config.triggerbot;
float prev_triggerbot_bScope_multiplier = config.triggerbot_bScope_multiplier;
int   prev_triggerbot_reaction_ms = config.triggerbot_reaction_ms;

bool prev_auto_shoot = config.auto_shoot;
float prev_bScope_multiplier = config.bScope_multiplier;

static void draw_target_correction_demo_canvas()
{
    ImVec2 canvas_sz(220, 220);
    ImGui::InvisibleButton("##tc_canvas", canvas_sz);

    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImVec2 center{ (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(25, 25, 25, 255));

    const float scale = 4.0f;
    float near_px = config.nearRadius * scale;
    float snap_px = config.snapRadius * scale;
    near_px = ImClamp(near_px, 10.0f, canvas_sz.x * 0.45f);
    snap_px = ImClamp(snap_px, 6.0f, near_px - 4.0f);

    dl->AddCircle(center, near_px, IM_COL32(80, 120, 255, 180), 64, 2.0f);
    dl->AddCircle(center, snap_px, IM_COL32(255, 100, 100, 180), 64, 2.0f);

    static float  dist_px = near_px;
    static float  vel_px = 0.0f;
    static double last_t = ImGui::GetTime();
    double now = ImGui::GetTime();
    double dt = now - last_t;
    last_t = now;

    double dist_units = dist_px / scale;
    double speed_mult;
    if (dist_units < config.snapRadius)
        speed_mult = config.minSpeedMultiplier * config.snapBoostFactor;
    else if (dist_units < config.nearRadius)
    {
        double t = dist_units / config.nearRadius;
        double crv = 1.0 - pow(1.0 - t, config.speedCurveExponent);
        speed_mult = config.minSpeedMultiplier +
            (config.maxSpeedMultiplier - config.minSpeedMultiplier) * crv;
    }
    else
    {
        double norm = ImClamp(dist_units / config.nearRadius, 0.0, 1.0);
        speed_mult = config.minSpeedMultiplier +
            (config.maxSpeedMultiplier - config.minSpeedMultiplier) * norm;
    }

    double base_px_s = 60.0;
    vel_px = static_cast<float>(base_px_s * speed_mult);
    dist_px -= vel_px * static_cast<float>(dt);
    if (dist_px <= 0.0f) dist_px = near_px;

    ImVec2 dot{ center.x - dist_px, center.y };
    dl->AddCircleFilled(dot, 4.0f, IM_COL32(255, 255, 80, 255));

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextColored(ImVec4(0.31f, 0.48f, 1.0f, 1.0f), "Near radius");
    ImGui::SameLine(130);
    ImGui::TextColored(ImVec4(1.0f, 0.39f, 0.39f, 1.0f), "Snap radius");
}

struct DemoKalman1D {
    double x{ 0 }, v{ 0 }, P{ 1 }, Q, R;
    DemoKalman1D(double processNoise, double measurementNoise)
        : Q(processNoise), R(measurementNoise) {
    }
    double update(double z, double dt) {
        x += v * dt;
        P += Q * dt;
        double K = P / (P + R);
        x += K * (z - x);
        P *= (1 - K);
        v = (1 - K) * v + K * ((z - x) / std::max(dt, 1e-8));
        return x;
    }
};

static void draw_smoothing_kalman_demo_canvas()
{
    ImVec2 canvas_sz(220, 220);
    ImGui::InvisibleButton("##sk_canvas", canvas_sz);
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImVec2 center{ (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(30, 30, 30, 255));

    // ????? ? dt
    static double last_t = ImGui::GetTime();
    double now = ImGui::GetTime();
    double dt = now - last_t;
    last_t = now;

    // Raw-???? ?? ??????????
    static double angle = 0.0;
    angle += dt * 1.0;
    if (angle > 2 * 3.14159265358979323846) angle -= 2 * 3.14159265358979323846;
    double rad = canvas_sz.x * 0.4;
    double rawX = center.x + cos(angle) * rad;
    double rawY = center.y + sin(angle) * rad;

    // ??????-??????
    static DemoKalman1D kfX{ config.kalman_process_noise, config.kalman_measurement_noise },
        kfY{ config.kalman_process_noise, config.kalman_measurement_noise };
    static float lastQ = config.kalman_process_noise,
        lastR = config.kalman_measurement_noise;
    if (lastQ != config.kalman_process_noise || lastR != config.kalman_measurement_noise) {
        kfX = DemoKalman1D(config.kalman_process_noise, config.kalman_measurement_noise);
        kfY = DemoKalman1D(config.kalman_process_noise, config.kalman_measurement_noise);
        lastQ = config.kalman_process_noise;
        lastR = config.kalman_measurement_noise;
    }
    double kalX = kfX.update(rawX, dt);
    double kalY = kfY.update(rawY, dt);

    // ???????????????? ???????????
    static double smX = center.x, smY = center.y;
    int   N = config.smoothness > 0 ? config.smoothness : 1;
    double alpha = 1.0 / N;
    // ?????, ???? Kalman-?????????? ???? ? ??????? ??????
    const double resetThreshold = 5.0;
    if (hypot(kalX - smX, kalY - smY) > resetThreshold) {
        smX = kalX;
        smY = kalY;
    }
    else {
        smX += (kalX - smX) * alpha;
        smY += (kalY - smY) * alpha;
    }

    // ?????? ?????: ????? = raw, ??????? = kalman, ??????? = smoothed
    dl->AddCircleFilled({ (float)rawX, (float)rawY }, 4.0f, IM_COL32(255, 255, 255, 200));
    dl->AddCircleFilled({ (float)kalX, (float)kalY }, 4.0f, IM_COL32(255, 100, 100, 200));
    dl->AddCircleFilled({ (float)smX,  (float)smY }, 4.0f, IM_COL32(100, 255, 100, 200));

    // ???????
    dl->AddText({ p0.x + 5, p0.y + 5 },
        IM_COL32(200, 200, 200, 255),
        "W=Raw  R=Kalman  G=Smoothed");
}

// PID 跟踪可视化：目标沿圆周匀速滑动，PID 控制光标追，画误差曲线 + P/I/D 项。
// 参数绑定 config.pid_*（滑条变化时重配 + reset）。既是调参工具也是可视回归。
// 红点用连续滑动而非瞬移，避免 D 项对阶跃的放大爆炸。
static void draw_pid_demo_canvas()
{
    ImVec2 canvas_sz(220, 220);
    ImGui::InvisibleButton("##pid_canvas", canvas_sz);
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImVec2 center{ (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(25, 25, 25, 255));

    // 静态 PID 实例，参数绑定 config.pid_*（变化时重配 + reset）
    static DualAxisPID pid;
    static float s_kpx = -1, s_kpy = -1, s_kix = -1, s_kiy = -1, s_kdx = -1, s_kdy = -1;
    static float s_wx = -1, s_wy = -1, s_dz = -1, s_bcx = -1, s_bcy = -1, s_out = -1, s_sx = -1, s_sy = -1;
    static int   s_aw = -1;
    static bool  s_lim = false;
    bool ch = (s_kpx != config.pid_kp_x || s_kpy != config.pid_kp_y ||
               s_kix != config.pid_ki_x || s_kiy != config.pid_ki_y ||
               s_kdx != config.pid_kd_x || s_kdy != config.pid_kd_y ||
               s_wx  != config.pid_windup_x || s_wy  != config.pid_windup_y ||
               s_dz  != config.pid_deadzone || s_aw  != config.pid_anti_windup_mode ||
               s_bcx != config.pid_backcalc_gain_x || s_bcy != config.pid_backcalc_gain_y ||
               s_lim != config.pid_output_limit_enabled || s_out != config.pid_out_max ||
               s_sx != config.pid_smooth_x || s_sy != config.pid_smooth_y);
    if (ch)
    {
        pid.set_pid_params(config.pid_kp_x, config.pid_kp_y, config.pid_ki_x, config.pid_ki_y,
                           config.pid_kd_x, config.pid_kd_y);
        pid.set_windup_guard(config.pid_windup_x, config.pid_windup_y);
        pid.set_anti_windup(config.pid_anti_windup_mode == 1
                                ? DualAxisPID::AntiWindup::BackCalc
                                : DualAxisPID::AntiWindup::Freeze,
                            config.pid_backcalc_gain_x, config.pid_backcalc_gain_y);
        pid.set_deadzone(config.pid_deadzone);
        pid.set_smooth_params(config.pid_smooth_x, config.pid_smooth_y, 0.0, 1.0);
        if (config.pid_output_limit_enabled && config.pid_out_max > 0.0f)
        {
            const double lim[2] = { -(double)config.pid_out_max, (double)config.pid_out_max };
            pid.set_output_limits(lim, lim);
        }
        else
        {
            pid.set_output_limits(nullptr, nullptr);
        }
        pid.reset();
        s_kpx = config.pid_kp_x; s_kpy = config.pid_kp_y;
        s_kix = config.pid_ki_x; s_kiy = config.pid_ki_y;
        s_kdx = config.pid_kd_x; s_kdy = config.pid_kd_y;
        s_wx = config.pid_windup_x; s_wy = config.pid_windup_y;
        s_dz = config.pid_deadzone; s_aw = config.pid_anti_windup_mode;
        s_bcx = config.pid_backcalc_gain_x; s_bcy = config.pid_backcalc_gain_y;
        s_lim = config.pid_output_limit_enabled; s_out = config.pid_out_max;
        s_sx = config.pid_smooth_x; s_sy = config.pid_smooth_y;
    }

    // dt
    static double last_t = ImGui::GetTime();
    double now = ImGui::GetTime();
    double dt = now - last_t;
    last_t = now;
    if (dt <= 0.0 || dt > 0.1) dt = 1.0 / 60.0;

    // 目标：沿圆周匀速滑动（照搬 Kalman demo 红点逻辑，避免瞬移触发 D 项爆炸）
    static double angle = 0.0;
    angle += dt * 1.0;
    if (angle > 2.0 * 3.14159265358979323846) angle -= 2.0 * 3.14159265358979323846;
    const double rad = 70.0;
    double tx = std::cos(angle) * rad;
    double ty = std::sin(angle) * rad;

    // 光标（PID 控制下），plant_gain = 1.0
    static double cx = 0.0, cy = 0.0;
    double ex = tx - cx, ey = ty - cy;
    auto out = pid.compute(ex, ey, dt);
    cx += out.first;
    cy += out.second;

    // 独立日志文件（不与 fusion.log 共用）：每次进程启动 "w" 截断，
    // _SH_DENYNO 允许其它进程（含 Read 工具/编辑器）实时读。
    static FILE* demo_log = nullptr;
    if (!demo_log) {
        const char* up = getenv("USERPROFILE");
        if (up) {
            char path[260];
            sprintf_s(path, "%s\\rn_ai\\pid_demo.log", up);
            demo_log = _fsopen(path, "w", _SH_DENYNO);
        }
    }
    if (demo_log) {
        fprintf(demo_log, "[pid_demo] ang=%.3f tgt=(%.1f,%.1f) cur=(%.1f,%.1f) err=(%.1f,%.1f) out=(%.2f,%.2f) dt=%.4f\n",
                angle, tx, ty, cx, cy, ex, ey, out.first, out.second, dt);
        fflush(demo_log);
    }
    // ALOG 兜底：pid_demo.log 一直没生成，先走 fusion.log 拿数据（这路已验证通）
    ALOG("[pid_demo] ang=%.3f tgt=(%.1f,%.1f) cur=(%.1f,%.1f) err=(%.1f,%.1f) out=(%.2f,%.2f) dt=%.4f",
         angle, tx, ty, cx, cy, ex, ey, out.first, out.second, dt);

    // 目标（红）、光标（绿）
    dl->AddCircleFilled(ImVec2(center.x + (float)tx, center.y + (float)ty), 5.0f, IM_COL32(255, 80, 80, 255));
    dl->AddCircleFilled(ImVec2(center.x + (float)cx, center.y + (float)cy), 4.0f, IM_COL32(80, 255, 80, 255));
    dl->AddText({ p0.x + 5, p0.y + 5 }, IM_COL32(200, 200, 200, 255), "R=Target  G=PID");

    // 误差曲线（滚动 120 帧）
    static float err_hist[120];
    static int err_idx = 0;
    double err_mag = std::hypot(ex, ey);
    err_hist[err_idx] = (float)err_mag;
    err_idx = (err_idx + 1) % 120;
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.1fpx", err_mag);
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PlotLines("##pid_err", err_hist, 120, err_idx, overlay, 0.0f, 150.0f, ImVec2(210, 40));

    // P/I/D 项（X 轴）
    auto pc = pid.get_components_x();
    ImGui::Text("P=%.2f  I=%.3f  D=%.2f", pc.p, pc.i, pc.d);
}


void draw_mouse()
{
    ImGui::SeparatorText("FOV");
    ImGui::SliderInt("FOV X", &config.fovX, 10, 120);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 水平方向瞄准视野范围(像素)。调大: 锁定更远目标但增加误锁。调小: 仅锁定近处目标。默认106, 推荐100-120");
    ImGui::SliderInt("FOV Y", &config.fovY, 10, 120);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 垂直方向瞄准视野范围(像素)。调大: 锁定更高/更低目标。调小: 仅锁定水平线附近目标。默认74, 推荐70-80");

    ImGui::SeparatorText("Mouse Passthrough");
    if (ImGui::SliderFloat("Human Mouse Sensitivity", &config.human_mouse_sensitivity, 0.0f, 1.5f, "%.2f"))
        config.saveConfig();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 人类鼠标移动灵敏度倍率, 在转发到硬件前对dx/dy乘以此值。调大: 鼠标移动更快。调小: 鼠标移动更慢。默认1.0, 推荐0.8-1.2");

    ImGui::SeparatorText("Aim Trigger");
    const char* aim_modes[] = { "Hold (aim while pressed)", "Timed toggle (tap = N ms)", "Shoot (hold left button)", "Key hold (hold F12 to aim)" };
    int aim_m = config.aim_trigger_mode;
    if (ImGui::Combo("Aim Trigger Mode", &aim_m, aim_modes, IM_ARRAYSIZE(aim_modes))) {
        config.aim_trigger_mode = aim_m;
        config.saveConfig();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 瞄准触发方式。0=按住瞄准键时瞄准, 1=点击切换定时瞄准, 2=按住左键射击时瞄准, 3=按住F12瞄准。默认0, 推荐0(Hold)");
    if (config.aim_trigger_mode == 1) {
        if (ImGui::SliderInt("Timed Duration (ms)", &config.aim_timed_duration_ms, 100, 5000))
            config.saveConfig();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 定时瞄准模式的持续时间(毫秒)。调大: 单次点击瞄准更久。调小: 单次点击瞄准更短。默认2000, 推荐1000-3000");
        ImGui::SameLine();
        ImGui::TextDisabled("(tap=aim, tap again=cancel, hold=until release)");
    }

    ImGui::SeparatorText("Aim Controller");
    {
        const char* ctl[] = { "Sunone (Kalman/Smooth)", "PID" };
        int c = config.aim_controller;
        if (ImGui::Combo("Controller", &c, ctl, IM_ARRAYSIZE(ctl)))
        {
            config.aim_controller = c;
            config.saveConfig();
            input_method_changed.store(true);
            globalMouseThread->configurePidFromConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 瞄准控制器算法选择。Sunone=卡尔曼+平滑传统方案, PID=比例积分微分控制器(旁路卡尔曼/平滑/预测)。默认0(Sunone), 推荐0");
    }
    if (config.aim_controller == 1)
    {
        bool pc = false;
        pc |= ImGui::SliderFloat("Kp X", &config.pid_kp_x, 0.0f, 2.0f, "%.3f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID X轴比例系数, 响应当前位置误差。调大: 跟踪更快但可能过冲振荡。调小: 更平滑但响应慢。默认0.4, 推荐0.3-0.6");
        pc |= ImGui::SliderFloat("Kp Y", &config.pid_kp_y, 0.0f, 2.0f, "%.3f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID Y轴比例系数, 响应当前位置误差。调大: 跟踪更快但可能过冲振荡。调小: 更平滑但响应慢。默认0.4, 推荐0.3-0.6");
        pc |= ImGui::SliderFloat("Ki X", &config.pid_ki_x, 0.0f, 0.1f, "%.4f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID X轴积分系数, 消除稳态误差。调大: 更彻底消除残差但可能积分饱和。调小: 减少过冲。默认0.02, 推荐0.001-0.05");
        pc |= ImGui::SliderFloat("Ki Y", &config.pid_ki_y, 0.0f, 0.1f, "%.4f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID Y轴积分系数, 消除稳态误差。调大: 更彻底消除残差但可能积分饱和。调小: 减少过冲。默认0.02, 推荐0.001-0.05");
        pc |= ImGui::SliderFloat("Kd X", &config.pid_kd_x, 0.0f, 1.0f, "%.3f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID X轴微分系数, 阻尼速度变化预测趋势。调大: 减少过冲但可能放大噪声。调小: 更平滑但可能振荡。默认0.01, 推荐0.005-0.05");
        pc |= ImGui::SliderFloat("Kd Y", &config.pid_kd_y, 0.0f, 1.0f, "%.3f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID Y轴微分系数, 阻尼速度变化预测趋势。调大: 减少过冲但可能放大噪声。调小: 更平滑但可能振荡。默认0.01, 推荐0.005-0.05");
        pc |= ImGui::SliderFloat("Windup X", &config.pid_windup_x, 0.0f, 500.0f, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID X轴积分限幅上限, 防止积分项无限累积(windup)。调大: 允许更大积分累积。调小: 更严格限制积分。默认100, 推荐50-200");
        pc |= ImGui::SliderFloat("Windup Y", &config.pid_windup_y, 0.0f, 500.0f, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID Y轴积分限幅上限, 防止积分项无限累积(windup)。调大: 允许更大积分累积。调小: 更严格限制积分。默认100, 推荐50-200");
        pc |= ImGui::SliderFloat("Deadzone", &config.pid_deadzone, 0.0f, 30.0f, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID 末端死区半径(像素), 误差进入此范围停止输出避免抖动。调大: 更早停但可能不到位。调小/0: 更精准但可能微抖。默认5.0, 推荐3.0-10.0");
        const char* aw[] = { "Freeze", "BackCalc" };
        int awm = config.pid_anti_windup_mode;
        if (ImGui::Combo("Anti-windup", &awm, aw, IM_ARRAYSIZE(aw))) { config.pid_anti_windup_mode = awm; pc = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 抗积分饱和策略。Freeze=饱和时冻结积分, BackCalc=反向计算修正积分。默认0(Freeze), 推荐0");
        if (config.pid_anti_windup_mode == 1)
        {
            pc |= ImGui::SliderFloat("BackCalc gain X", &config.pid_backcalc_gain_x, 0.0f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: BackCalc抗饱和X轴增益, 控制积分回退速度。调大: 更快退出饱和。调小: 更慢退出饱和。默认0.1, 推荐0.05-0.3");
            pc |= ImGui::SliderFloat("BackCalc gain Y", &config.pid_backcalc_gain_y, 0.0f, 1.0f, "%.3f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: BackCalc抗饱和Y轴增益, 控制积分回退速度。调大: 更快退出饱和。调小: 更慢退出饱和。默认0.1, 推荐0.05-0.3");
        }
        pc |= ImGui::Checkbox("Output limit", &config.pid_output_limit_enabled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用PID输出限幅, 将输出钳制在±Out max范围内防止跳帧。默认开启, 推荐开启");
        if (config.pid_output_limit_enabled)
        {
            pc |= ImGui::SliderFloat("Out max", &config.pid_out_max, 1.0f, 500.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID输出最大值限制(像素/帧)。调大: 允许更大单帧移动。调小: 限制单帧移动量更平滑。默认40.0, 推荐20-80");
        }
        pc |= ImGui::SliderFloat("Smooth X", &config.pid_smooth_x, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID X轴输出平滑混合系数。0=纯delta误差(更直接), 1=纯P+I+D(更平滑)。默认0.5, 推荐0.3-0.7");
        pc |= ImGui::SliderFloat("Smooth Y", &config.pid_smooth_y, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: PID Y轴输出平滑混合系数。0=纯delta误差(更直接), 1=纯P+I+D(更平滑)。默认0.5, 推荐0.3-0.7");
        if (pc)
        {
            config.saveConfig();
            globalMouseThread->configurePidFromConfig();
        }
        ImGui::TextDisabled("(PID bypasses Kalman/Smoothing/Prediction)");
    }

    ImGui::SliderInt("Smoothness", &config.smoothness, 1, 200, "%d");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 鼠标移动平滑度, 值越大移动越平滑但响应越慢(EMA窗口大小)。调大: 更平滑但延迟更高。调小: 响应更快但可能抖动。默认100, 推荐60-150");
    if (ImGui::Checkbox("Enable Smooth Movement", &config.use_smoothing))
    {
        config.saveConfig();
        input_method_changed.store(true);
        globalMouseThread->setUseSmoothing(config.use_smoothing);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用EMA平滑移动, 配合Smoothness值使用。默认开启, 推荐开启");
    ImGui::SameLine();
    ImGui::Checkbox("Tracking Smoothing", &config.tracking_smoothing);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 对跟踪目标应用额外平滑, 减少目标切换时的跳变。默认关闭, 推荐关闭(如感觉跳变可开启)");
    ImGui::SameLine();
    if (ImGui::Checkbox("Enable Kalman Filter", &config.use_kalman))
    {
        config.saveConfig();
        input_method_changed.store(true);
        globalMouseThread->setUseKalman(config.use_kalman);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用卡尔曼滤波器预测目标位置, 减少检测噪声。默认关闭, 推荐开启");


    if (config.use_kalman)
    {
        bool changed = false;
        changed |= ImGui::SliderFloat("Kalman Process Noise", &config.kalman_process_noise, 0.10f, 1.0f, "%.4f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 卡尔曼过程噪声Q, 表示模型不确定性。调大: 更信任测量值响应更快。调小: 更信任预测值更平滑。默认0.01(实际滑条下限0.10), 推荐0.10-0.50");
        changed |= ImGui::SliderFloat("Kalman Measurement Noise", &config.kalman_measurement_noise, 0.40f, 1.0f, "%.4f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 卡尔曼测量噪声R, 表示传感器精度。调大: 更信任预测值更平滑。调小: 更信任测量值响应更快。默认0.10(实际滑条下限0.40), 推荐0.40-0.80");
        changed |= ImGui::SliderFloat("Kalman Speed Multiplier X", &config.kalman_speed_multiplier_x, 0.1f, 5.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 卡尔曼X轴速度倍率, 缩放卡尔曼输出的X方向移动量。调大: X轴移动更快更激进。调小: X轴移动更慢更保守。默认1.0, 推荐0.8-1.5");
        changed |= ImGui::SliderFloat("Kalman Speed Multiplier Y", &config.kalman_speed_multiplier_y, 0.1f, 5.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 卡尔曼Y轴速度倍率, 缩放卡尔曼输出的Y方向移动量。调大: Y轴移动更快更激进。调小: Y轴移动更慢更保守。默认1.0, 推荐0.8-1.5");
        changed |= ImGui::SliderFloat("Reset Threshold", &config.resetThreshold, 1.0f, 20.0f, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 卡尔曼重置阈值, 当预测位置与当前平滑位置差距超过此值(像素)时重置滤波器。调大: 减少重置更平滑但可能跟丢。调小: 更快重置但可能频繁跳变。默认5.0, 推荐3.0-10.0");

        if (changed)
        {
            config.saveConfig();
            input_method_changed.store(true);
            globalMouseThread->setKalmanParams(
                config.kalman_process_noise,
                config.kalman_measurement_noise
            );
            globalMouseThread->setKalmanSpeedMultiplierX(
                config.kalman_speed_multiplier_x);
            globalMouseThread->setKalmanSpeedMultiplierY(
                config.kalman_speed_multiplier_y);
        }
    }


    if (ImGui::CollapsingHeader("Demos"))
    {
        if (ImGui::BeginTable("mouse_demo_table", 3, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableNextColumn();
            ImGui::Text("Smooth + Kalman");
            draw_smoothing_kalman_demo_canvas();
            ImGui::TableNextColumn();
            ImGui::Text("Visual");
            draw_target_correction_demo_canvas();
            ImGui::TableNextColumn();
            ImGui::Text("PID");
            draw_pid_demo_canvas();
            ImGui::EndTable();
        }
    }

    ImGui::SeparatorText("Speed Multiplier");
    ImGui::SliderFloat("Min Speed Multiplier", &config.minSpeedMultiplier, 0.1f, 5.0f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 最小速度倍率, 控制瞄准起始/近处移动速度的下限。调大: 近处移动更快。调小: 近处移动更慢更精细。默认0.1, 推荐0.1-0.5");
    ImGui::SliderFloat("Max Speed Multiplier", &config.maxSpeedMultiplier, 0.1f, 5.0f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 最大速度倍率, 控制瞄准远端移动速度的上限。调大: 远处移动更快更激进。调小: 远处移动更慢更保守。默认0.1, 推荐0.1-0.5");

    if (config.use_kalman)
    {
        ImGui::SeparatorText("Prediction");
        ImGui::SliderFloat("Prediction Interval", &config.predictionInterval, 0.00f, 0.5f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 预测间隔(秒), 0=禁用预测。调大: 预测更远未来位置。调小: 预测更近未来位置。默认0.01, 推荐0.01-0.05");
        const char* mode_items[] = { "Standard", "Kalman Lead", "Kalman + Raw" };
        int mode_idx = config.prediction_mode;
        if (ImGui::Combo("Prediction Mode", &mode_idx, mode_items, IM_ARRAYSIZE(mode_items)))
        {
            config.prediction_mode = mode_idx;
            config.saveConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 预测算法模式。Standard=基于速度外推, Kalman Lead=卡尔曼超前预测, Kalman+Raw=卡尔曼与原始混合。默认0(Standard), 推荐0");
        ImGui::SliderFloat("Pred Kalman Lead (ms)", &config.prediction_kalman_lead_ms, 0.0f, 150.0f, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: Kalman预测模式下的超前时间(毫秒)。调大: 预测更远未来位置更激进。调小: 预测更保守。默认0, 推荐20-60");
        ImGui::SliderFloat("Pred Kalman Max Lead (ms)", &config.prediction_kalman_max_lead_ms, 0.0f, 250.0f, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: Kalman预测最大超前时间上限(毫秒)。调大: 允许更大预测范围。调小: 限制预测范围防止过冲。默认0, 推荐50-150");
        ImGui::SliderFloat("Pred Velocity Smoothing", &config.prediction_velocity_smoothing, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 预测速度平滑系数(EMA)。调大: 速度估计更平滑但响应慢。调小: 速度估计更灵敏但噪声大。默认0.4, 推荐0.3-0.6");
        ImGui::SliderFloat("Pred Velocity Scale", &config.prediction_velocity_scale, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 预测速度缩放倍率。调大: 预测位置更超前更激进。调小: 预测位置更保守。默认1.0, 推荐0.8-1.5");
        ImGui::SliderFloat("Pred Kalman Q", &config.prediction_kalman_process_noise, 0.001f, 5.0f, "%.3f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 预测用卡尔曼过程噪声Q。调大: 更信任测量值响应更快。调小: 更信任模型预测更平滑。默认0.01, 推荐0.005-0.05");
        ImGui::SliderFloat("Pred Kalman R", &config.prediction_kalman_measurement_noise, 0.001f, 5.0f, "%.3f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 预测用卡尔曼测量噪声R。调大: 更信任模型预测更平滑。调小: 更信任测量值响应更快。默认0.10, 推荐0.05-0.50");
        ImGui::Checkbox("Use Future For Aim", &config.prediction_use_future_for_aim);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 将预测的未来位置直接用于瞄准而非当前位置。开启后瞄准更超前。默认关闭, 推荐根据游戏需求开启");
        if (config.predictionInterval == 0.00f)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(255, 0, 0, 255), "-> Disabled");
        }
        else
        {
            if (ImGui::SliderInt("Future Positions", &config.prediction_futurePositions, 1, 40))
            {
                config.saveConfig();
                input_method_changed.store(true);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 绘制未来预测位置的数量。调大: 显示更多预测点。调小: 显示更少预测点。默认20, 推荐10-30");

            ImGui::SameLine();
            if (ImGui::Checkbox("Draw##draw_future_positions_button", &config.draw_futurePositions))
            {
                config.saveConfig();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 在游戏覆盖层上绘制未来预测位置点。默认开启, 推荐开启(方便调试)");
        }
    }

    ImGui::SeparatorText("Camera Compensation");
    ImGui::Checkbox("Enable Camera Compensation", &config.camera_compensation_enabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用相机移动补偿, 抵消游戏中镜头自身运动对瞄准的影响。默认关闭, 推荐需要时开启");
    if (config.camera_compensation_enabled)
    {
        ImGui::SliderFloat("Camera Max Shift", &config.camera_compensation_max_shift, 0.0f, 200.0f, "%.1f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 相机补偿最大偏移量(像素), 限制补偿范围防止异常跳变。调大: 允许更大补偿范围。调小: 限制补偿更安全。默认50, 推荐30-100");
        ImGui::SliderFloat("Camera Strength", &config.camera_compensation_strength, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 相机补偿强度系数。调大: 补偿更激进。调小: 补偿更保守。默认1.0, 推荐0.5-1.5");
    }

    ImGui::SeparatorText("Target corrention");
    ImGui::SliderFloat("Snap Radius", &config.snapRadius, 0.1f, 5.0f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 吸附半径, 目标进入此范围后速度降至最低并应用吸附增强。调大: 更大范围减速。调小: 更小范围减速更精准。默认1.5, 推荐1.0-3.0");
    ImGui::SliderFloat("Near Radius", &config.nearRadius, 1.0f, 40.0f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 近距半径, 目标距准星此范围内开始减速(SpeedCurve控制曲线)。调大: 更早开始减速。调小: 更晚减速更快速接近。默认25, 推荐15-35");
    ImGui::SliderFloat("Speed Curve Exponent", &config.speedCurveExponent, 0.1f, 10.0f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 速度曲线指数, 控制近距半径内减速曲线的陡峭程度。调大: 近处减速更急剧(更平滑停靠)。调小: 减速更线性。默认3.0, 推荐2.0-5.0");
    ImGui::SliderFloat("Snap Boost Factor", &config.snapBoostFactor, 0.01f, 4.00f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 吸附增强因子, 进入吸附半径后速度倍率乘以该值。调大: 吸附更强(更快锁上)。调小: 吸附更弱更自然。默认1.15, 推荐1.0-1.5");

    ImGui::SeparatorText("Game Profile");
    std::vector<std::string> profile_names;
    for (const auto& kv : config.game_profiles)
        profile_names.push_back(kv.first);

    static int selected_index = 0;
    for (size_t i = 0; i < profile_names.size(); ++i)
    {
        if (profile_names[i] == config.active_game)
        {
            selected_index = static_cast<int>(i);
            break;
        }
    }

    std::vector<const char*> profile_items;
    for (const auto& name : profile_names)
        profile_items.push_back(name.c_str());

    if (ImGui::Combo("Active Game Profile", &selected_index, profile_items.data(), static_cast<int>(profile_items.size())))
    {
        config.active_game = profile_names[selected_index];
        config.saveConfig();
        globalMouseThread->updateConfig(
            config.detection_resolution,
            config.fovX,
            config.fovY,
            config.minSpeedMultiplier,
            config.maxSpeedMultiplier,
            config.predictionInterval,
            config.auto_shoot,
            config.bScope_multiplier,
            config.triggerbot_bScope_multiplier
        );
        input_method_changed.store(true);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择当前激活的游戏配置文件, 切换sens/yaw/pitch等灵敏度参数。默认UNIFIED, 推荐按游戏创建独立配置");

    const auto& gp = config.currentProfile();

    ImGui::Text("Current profile: %s", gp.name.c_str());
    ImGui::Text("Sens: %.4f", gp.sens);
    ImGui::Text("Yaw:  %.4f", gp.yaw);
    ImGui::Text("Pitch: %.4f", gp.pitch);
    ImGui::Text("FOV Scaled: %s", gp.fovScaled ? "true" : "false");

    if (gp.name != "UNIFIED")
    {
        Config::GameProfile& modifiable = config.game_profiles[gp.name];
        bool changed = false;

        float sens_f = static_cast<float>(modifiable.sens);
        float yaw_f = static_cast<float>(modifiable.yaw);
        float pitch_f = static_cast<float>(modifiable.pitch);
        float baseFOV_f = static_cast<float>(modifiable.baseFOV);

        changed |= ImGui::SliderFloat("Sensitivity", &sens_f, 0.001f, 10.0f, "%.4f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 游戏内鼠标灵敏度设置, 用于将角度转换为鼠标计数。调大: 输出移动量减小(游戏更灵敏)。调小: 输出移动量增大。默认1.0, 需与游戏内设置匹配");
        changed |= ImGui::SliderFloat("Yaw", &yaw_f, 0.001f, 0.1f, "%.4f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 游戏水平偏航角每计数角度(度), 用于角度到鼠标计数的转换。调大: 水平移动更大。调小: 水平移动更小。默认0.022, 需与游戏引擎匹配");
        changed |= ImGui::SliderFloat("Pitch", &pitch_f, 0.001f, 0.1f, "%.4f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 游戏垂直俯仰角每计数角度(度), 用于角度到鼠标计数的转换。调大: 垂直移动更大。调小: 垂直移动更小。默认0.022, 需与游戏引擎匹配");

        changed |= ImGui::Checkbox("FOV Scaled", &modifiable.fovScaled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 根据当前FOV与Base FOV的比例缩放灵敏度, 使不同瞄准镜下手感一致。默认关闭, 推荐开启");
        if (modifiable.fovScaled)
        {
            changed |= ImGui::SliderFloat("Base FOV", &baseFOV_f, 10.0f, 180.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 基准FOV值, 灵敏度缩放的参考FOV(通常为腰射FOV)。调大: 缩放更激进。调小: 缩放更保守。默认90, 推荐与游戏腰射FOV一致");
        }

        if (changed)
        {
            modifiable.sens = static_cast<double>(sens_f);
            modifiable.yaw = static_cast<double>(yaw_f);

            if (gp.pitch == 0.0 || !gp.fovScaled)
                modifiable.pitch = modifiable.yaw;
            else
                modifiable.pitch = static_cast<double>(pitch_f);

            modifiable.baseFOV = static_cast<double>(baseFOV_f);

            config.saveConfig();
            input_method_changed.store(true);
        }
    }

    ImGui::SeparatorText("Manage Profiles");

    static char new_profile_name[64] = "";
    const float add_btn_w = 96.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth((std::max)(120.0f, ImGui::GetContentRegionAvail().x - add_btn_w - spacing));
    ImGui::InputTextWithHint("##new_profile_name", "New profile name", new_profile_name, sizeof(new_profile_name));
    ImGui::SameLine();
    if (ImGui::Button("Add Profile", ImVec2(add_btn_w, 0.0f)))
    {
        std::string name = std::string(new_profile_name);
        if (!name.empty() && config.game_profiles.count(name) == 0)
        {
            Config::GameProfile gp;
            gp.name = name;
            gp.sens = 1.0;
            gp.yaw = 0.022;
            gp.pitch = 0.022;
            gp.fovScaled = false;
            gp.baseFOV = 90.0;
            config.game_profiles[name] = gp;
            config.active_game = name;
            config.saveConfig();
            input_method_changed.store(true);
            new_profile_name[0] = '\0'; // clear
        }
    }

    if (gp.name != "UNIFIED")
    {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 50, 50, 255));
        if (ImGui::Button("Delete Current Profile"))
        {
            config.game_profiles.erase(gp.name);
            if (!config.game_profiles.empty())
                config.active_game = config.game_profiles.begin()->first;
            else
                config.active_game = "UNIFIED";

            config.saveConfig();
            input_method_changed.store(true);
        }
        ImGui::PopStyleColor();
    }

    // TODO: Easy No Recoil is currently non-functional; keep it hidden until fixed.

    if (ImGui::CollapsingHeader("Advanced"))
    {
        ImGui::SeparatorText("Performance");
        if (ImGui::Checkbox("Idle Throttle (~10 fps when not aiming)", &config.inference_idle_throttle))
            config.saveConfig();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 空闲时节流, 不瞄准时降低捕获+推理帧率至约10fps以节省GPU资源。默认开启, 推荐开启");

        ImGui::SeparatorText("Auto Shoot");
        ImGui::Checkbox("Auto Shoot", &config.auto_shoot);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 自动射击, 准星对准目标时自动按下射击键。默认关闭, 推荐需要时开启");
        if (config.auto_shoot)
        {
            ImGui::SliderFloat("bScope Multiplier", &config.bScope_multiplier, 0.2f, 2.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 开镜时自动射击距离倍率, 开镜后触发射击的距离乘以此值。调大: 开镜后更远触发射击。调小: 开镜后更近触发射击。默认1.0, 推荐1.0-1.5");
        }

        ImGui::SeparatorText("Triggerbot");
        ImGui::Checkbox("Triggerbot (only fire, no aim)", &config.triggerbot);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 扳机机器人, 仅自动射击不控制瞄准, 准星扫过目标时自动开火。默认关闭, 推荐需要时开启");
        if (config.triggerbot)
        {
            int reaction_ms = config.triggerbot_reaction_ms;
            if (ImGui::SliderInt("Reaction Time (ms)", &reaction_ms, 0, 500))
            {
                config.triggerbot_reaction_ms = reaction_ms;
                config.saveConfig();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 扳机反应延迟(毫秒), 从检测到目标到触发射击的延迟。调大: 更拟人但可能错过机会。调小/0: 即时反应。默认0, 推荐0-150");
            ImGui::SliderFloat("Triggerbot bScope", &config.triggerbot_bScope_multiplier, 0.5f, 2.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 扳机开镜倍率, 开镜时触发距离乘以此值。调大: 开镜后更远触发。调小: 开镜后更近触发。默认1.0, 推荐1.0-1.5");
        }

        ImGui::SeparatorText("Wind mouse");
        if (ImGui::Checkbox("Enable WindMouse", &config.wind_mouse_enabled))
        {
            config.saveConfig();
            input_method_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用WindMouse拟人鼠标移动算法, 模拟人类手部自然运动轨迹(带重力/风力效果)。默认开启, 推荐开启");
        if (config.wind_mouse_enabled)
        {
            if (ImGui::SliderFloat("Gravity force", &config.wind_G, 4.00f, 40.00f, "%.2f"))
            {
                config.saveConfig();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: WindMouse重力强度, 控制鼠标向目标移动的加速度。调大: 移动更快更直接。调小: 移动更慢更飘忽。默认18, 推荐12-25");

            if (ImGui::SliderFloat("Wind fluctuation", &config.wind_W, 1.00f, 40.00f, "%.2f"))
            {
                config.saveConfig();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: WindMouse风力波动幅度, 控制移动路径的随机摆动程度。调大: 路径更弯曲更拟人。调小: 路径更直线。默认15, 推荐10-20");

            if (ImGui::SliderFloat("Max step (velocity clip)", &config.wind_M, 1.00f, 40.00f, "%.2f"))
            {
                config.saveConfig();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: WindMouse最大步长(速度上限), 限制单步最大移动距离。调大: 更快但可能跳过目标。调小: 更慢更精细。默认10, 推荐5-15");

            if (ImGui::SliderFloat("Distance where behaviour changes", &config.wind_D, 1.00f, 40.00f, "%.2f"))
            {
                config.saveConfig();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: WindMouse行为切换距离, 距目标小于此值时切换为精细接近模式。调大: 更早进入精细模式。调小: 更晚切换快速接近。默认8, 推荐5-12");

            if (ImGui::Button("Reset Wind Mouse to default settings"))
            {
                config.wind_G = 18.0f;
                config.wind_W = 15.0f;
                config.wind_M = 10.0f;
                config.wind_D = 8.0f;
                config.saveConfig();
            }
        }
    }

    ImGui::SeparatorText("Input method");
    std::vector<std::string> input_methods = { "WIN32", "ARDUINO", "KMBOX_B", "KMBOX_NET", "MAKCU" };

    std::vector<const char*> method_items;
    method_items.reserve(input_methods.size());
    for (const auto& item : input_methods)
    {
        method_items.push_back(item.c_str());
    }

    std::string combo_label = "Mouse Input method";
    int input_method_index = 0;
    for (size_t i = 0; i < input_methods.size(); ++i)
    {
        if (input_methods[i] == config.input_method)
        {
            input_method_index = static_cast<int>(i);
            break;
        }
    }

    if (ImGui::Combo("Mouse Input Method", &input_method_index, method_items.data(), static_cast<int>(method_items.size())))
    {
        std::string new_input_method = input_methods[input_method_index];

        if (new_input_method != config.input_method)
        {
            config.input_method = new_input_method;
            config.saveConfig();
            input_method_changed.store(true);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择鼠标输入硬件方式。WIN32=系统API(可能被检测), ARDUINO=Arduino串口, KMBOX_B=串口KMBOX, KMBOX_NET=网络KMBOX, MAKCU=MAKCU串口。默认WIN32, 推荐ARDUINO或KMBOX");


    if (config.input_method == "MAKCU")
    {
        if (makcu && makcu->isOpen())
        {
            ImGui::TextColored(ImVec4(0, 255, 0, 255), "Makcu connected");
        }
        else
        {
            ImGui::TextColored(ImVec4(255, 0, 0, 255), "Makcu not connected");
        }

        std::vector<std::string> port_list;
        for (int i = 1; i <= 30; ++i)
        {
            port_list.push_back("COM" + std::to_string(i));
        }

        bool has_current_port = false;
        for (const auto& port : port_list)
        {
            if (port == config.makcu_port)
            {
                has_current_port = true;
                break;
            }
        }
        if (!config.makcu_port.empty() && !has_current_port)
        {
            port_list.insert(port_list.begin(), config.makcu_port);
        }

        std::vector<const char*> port_items;
        port_items.reserve(port_list.size());
        for (auto& p : port_list) port_items.push_back(p.c_str());

        int port_index = 0;
        for (size_t i = 0; i < port_list.size(); ++i)
        {
            if (port_list[i] == config.makcu_port)
            {
                port_index = (int)i;
                break;
            }
        }

        if (ImGui::Combo("Makcu Port", &port_index, port_items.data(), (int)port_items.size()))
        {
            config.makcu_port = port_list[port_index];
            config.saveConfig();
            input_method_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择MAKCU设备连接的串口号。默认COM1");

        std::vector<int> baud_list = { 115200, 1000000, 2000000, 4000000 };
        std::vector<std::string> baud_str_list;
        for (int b : baud_list) baud_str_list.push_back(std::to_string(b));
        std::vector<const char*> baud_items;
        baud_items.reserve(baud_str_list.size());
        for (auto& bs : baud_str_list) baud_items.push_back(bs.c_str());

        int baud_index = 0;
        for (size_t i = 0; i < baud_list.size(); ++i)
        {
            if (baud_list[i] == config.makcu_baudrate)
            {
                baud_index = (int)i;
                break;
            }
        }

        if (ImGui::Combo("Makcu Baudrate", &baud_index, baud_items.data(), (int)baud_items.size()))
        {
            config.makcu_baudrate = baud_list[baud_index];
            config.saveConfig();
            input_method_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择MAKCU串口通信波特率。默认115200, 推荐115200");
    }
    else if (config.input_method == "ARDUINO")
    {
        if (arduinoSerial)
        {
            if (arduinoSerial->isOpen())
            {
                ImGui::TextColored(ImVec4(0, 255, 0, 255), "Arduino connected");
            }
            else
            {
                ImGui::TextColored(ImVec4(255, 0, 0, 255), "Arduino not connected");
            }
        }

        std::vector<std::string> port_list;
        for (int i = 1; i <= 30; ++i)
        {
            port_list.push_back("COM" + std::to_string(i));
        }

        std::vector<const char*> port_items;
        port_items.reserve(port_list.size());
        for (const auto& port : port_list)
        {
            port_items.push_back(port.c_str());
        }

        int port_index = 0;
        for (size_t i = 0; i < port_list.size(); ++i)
        {
            if (port_list[i] == config.arduino_port)
            {
                port_index = static_cast<int>(i);
                break;
            }
        }

        if (ImGui::Combo("Arduino Port", &port_index, port_items.data(), static_cast<int>(port_items.size())))
        {
            config.arduino_port = port_list[port_index];
            config.saveConfig();
            input_method_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择Arduino设备连接的串口号。默认COM1");

        std::vector<int> baud_rate_list = { 9600, 19200, 38400, 57600, 115200 };
        std::vector<std::string> baud_rate_str_list;
        for (const auto& rate : baud_rate_list)
        {
            baud_rate_str_list.push_back(std::to_string(rate));
        }

        std::vector<const char*> baud_rate_items;
        baud_rate_items.reserve(baud_rate_str_list.size());
        for (const auto& rate_str : baud_rate_str_list)
        {
            baud_rate_items.push_back(rate_str.c_str());
        }

        int baud_rate_index = 0;
        for (size_t i = 0; i < baud_rate_list.size(); ++i)
        {
            if (baud_rate_list[i] == config.arduino_baudrate)
            {
                baud_rate_index = static_cast<int>(i);
                break;
            }
        }

        if (ImGui::Combo("Arduino Baudrate", &baud_rate_index, baud_rate_items.data(), static_cast<int>(baud_rate_items.size())))
        {
            config.arduino_baudrate = baud_rate_list[baud_rate_index];
            config.saveConfig();
            input_method_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择Arduino串口通信波特率。默认115200, 推荐115200");

        if (ImGui::Checkbox("Arduino 16-bit Mouse", &config.arduino_16_bit_mouse))
        {
            config.saveConfig();
            input_method_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用Arduino 16位鼠标报告, 支持更大的移动范围(-32768~32767)。默认关闭, 推荐需要大范围移动时开启");
        if (ImGui::Checkbox("Arduino Enable Keys", &config.arduino_enable_keys))
        {
            config.saveConfig();
            input_method_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 启用Arduino键盘按键支持, 允许Arduino发送键盘事件(如射击/换弹)。默认关闭, 推荐需要时开启");
    }
    else if (config.input_method == "WIN32")
    {
        ImGui::TextColored(ImVec4(255, 255, 255, 255), "This is a standard mouse input method, it may not work in most games. Use ARDUINO or KMBOX.");
        ImGui::TextColored(ImVec4(255, 0, 0, 255), "Use at your own risk, the method is detected in some games.");
    }
    else if (config.input_method == "KMBOX_B")
    {
        std::vector<std::string> port_list;
        for (int i = 1; i <= 30; ++i)
        {
            port_list.push_back("COM" + std::to_string(i));
        }
        std::vector<const char*> port_items;
        port_items.reserve(port_list.size());
        for (auto& p : port_list) port_items.push_back(p.c_str());

        int port_index = 0;
        for (size_t i = 0; i < port_list.size(); ++i)
        {
            if (port_list[i] == config.kmbox_b_port)
            {
                port_index = (int)i;
                break;
            }
        }

        if (ImGui::Combo("kmbox Port", &port_index, port_items.data(), (int)port_items.size()))
        {
            config.kmbox_b_port = port_list[port_index];
            config.saveConfig();
            input_method_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择KMBOX B串口设备连接的串口号。默认COM0");

        std::vector<int> baud_list = { 9600, 19200, 38400, 57600, 115200 };
        std::vector<std::string> baud_str_list;
        for (int b : baud_list) baud_str_list.push_back(std::to_string(b));
        std::vector<const char*> baud_items;
        baud_items.reserve(baud_str_list.size());
        for (auto& bs : baud_str_list) baud_items.push_back(bs.c_str());

        int baud_index = 0;
        for (size_t i = 0; i < baud_list.size(); ++i)
        {
            if (baud_list[i] == config.kmbox_b_baudrate)
            {
                baud_index = (int)i;
                break;
            }
        }

        if (ImGui::Combo("kmbox Baudrate", &baud_index, baud_items.data(), (int)baud_items.size()))
        {
            config.kmbox_b_baudrate = baud_list[baud_index];
            config.saveConfig();
            input_method_changed.store(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: 选择KMBOX B串口通信波特率。默认115200, 推荐115200");

       /* if (ImGui::Button("Run boot.py"))
        {
            kmboxSerial->start_boot();
        }

        if (ImGui::Button("Reboot KMBOX"))
        {
            kmboxSerial->reboot();
        }

        if (ImGui::Button("Send Stop"))
        {
            kmboxSerial->send_stop();
        }*/
    }
    else if (config.input_method == "KMBOX_NET")
    {
        static char ip[32], port[8], uuid[16];
        strncpy(ip, config.kmbox_net_ip.c_str(), sizeof(ip));
        strncpy(port, config.kmbox_net_port.c_str(), sizeof(port));
        strncpy(uuid, config.kmbox_net_uuid.c_str(), sizeof(uuid));

        ImGui::InputText("kmboxNet IP", ip, sizeof(ip));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: KMBOX Net设备的IP地址。默认127.0.0.1, 需与设备实际IP一致");
        ImGui::InputText("Port", port, sizeof(port));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: KMBOX Net设备的通信端口号。默认12345, 需与设备实际端口一致");
        ImGui::InputText("UUID", uuid, sizeof(uuid));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"功能: KMBOX Net设备的UUID标识符, 用于配对认证。需与设备UUID一致");

        if (ImGui::Button("Save & Reconnect"))
        {
            config.kmbox_net_ip = ip;
            config.kmbox_net_port = port;
            config.kmbox_net_uuid = uuid;
            config.saveConfig();
            input_method_changed.store(true);
        }

        if (kmboxNetSerial && kmboxNetSerial->isOpen())
        {
            ImGui::TextColored(ImVec4(0, 255, 0, 255), "kmboxNet connected");
        }
        else
        {
            ImGui::TextColored(ImVec4(255, 0, 0, 255), "kmboxNet not connected");
        }

        if (ImGui::Button("Reboot box"))
        {
            if (kmboxNetSerial)
            {
                kmboxNetSerial->reboot();
            }
        }

        if (ImGui::Button("Change Kmbox image"))
        {
            if (kmboxNetSerial)
            {
                kmboxNetSerial->lcdColor(0);
                kmboxNetSerial->lcdPicture(gImage_128x160);
            }
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(255, 255, 255, 100), "Do not test shooting and aiming with the overlay is open.");

    if (prev_fovX != config.fovX ||
        prev_fovY != config.fovY ||
        config.smoothness != prev_smoothness ||
        prev_use_smoothing != config.use_smoothing ||
        prev_tracking_smoothing != config.tracking_smoothing ||
        prev_use_kalman != config.use_kalman ||
        prev_kalman_process_noise != config.kalman_process_noise ||
        prev_kalman_measure_noise != config.kalman_measurement_noise ||
        prev_reset_threshold != config.resetThreshold ||
        prev_minSpeedMultiplier != config.minSpeedMultiplier ||
        prev_maxSpeedMultiplier != config.maxSpeedMultiplier ||
        prev_predictionInterval != config.predictionInterval ||
        prev_prediction_mode != config.prediction_mode ||
        prev_prediction_lead_ms != config.prediction_kalman_lead_ms ||
        prev_prediction_max_lead_ms != config.prediction_kalman_max_lead_ms ||
        prev_prediction_velocity_smoothing != config.prediction_velocity_smoothing ||
        prev_prediction_velocity_scale != config.prediction_velocity_scale ||
        prev_prediction_kalman_q != config.prediction_kalman_process_noise ||
        prev_prediction_kalman_r != config.prediction_kalman_measurement_noise ||
        prev_prediction_use_future_for_aim != config.prediction_use_future_for_aim ||
        prev_camera_compensation_enabled != config.camera_compensation_enabled ||
        prev_camera_compensation_max_shift != config.camera_compensation_max_shift ||
        prev_camera_compensation_strength != config.camera_compensation_strength ||
        prev_snapRadius != config.snapRadius ||
        prev_nearRadius != config.nearRadius ||
        prev_speedCurveExponent != config.speedCurveExponent ||
        prev_snapBoostFactor != config.snapBoostFactor)
    {
        prev_fovX = config.fovX;
        prev_fovY = config.fovY;
        prev_smoothness = config.smoothness;
        prev_use_smoothing            = config.use_smoothing;
        prev_tracking_smoothing       = config.tracking_smoothing;
        prev_use_kalman               = config.use_kalman;
        prev_kalman_process_noise     = config.kalman_process_noise;
        prev_kalman_measure_noise     = config.kalman_measurement_noise;
        prev_reset_threshold          = config.resetThreshold;
        prev_minSpeedMultiplier = config.minSpeedMultiplier;
        prev_maxSpeedMultiplier = config.maxSpeedMultiplier;
        prev_predictionInterval = config.predictionInterval;
        prev_prediction_mode = config.prediction_mode;
        prev_prediction_lead_ms = config.prediction_kalman_lead_ms;
        prev_prediction_max_lead_ms = config.prediction_kalman_max_lead_ms;
        prev_prediction_velocity_smoothing = config.prediction_velocity_smoothing;
        prev_prediction_velocity_scale = config.prediction_velocity_scale;
        prev_prediction_kalman_q = config.prediction_kalman_process_noise;
        prev_prediction_kalman_r = config.prediction_kalman_measurement_noise;
        prev_prediction_use_future_for_aim = config.prediction_use_future_for_aim;
        prev_camera_compensation_enabled = config.camera_compensation_enabled;
        prev_camera_compensation_max_shift = config.camera_compensation_max_shift;
        prev_camera_compensation_strength = config.camera_compensation_strength;
        prev_snapRadius = config.snapRadius;
        prev_nearRadius = config.nearRadius;
        prev_speedCurveExponent = config.speedCurveExponent;
        prev_snapBoostFactor = config.snapBoostFactor;

        globalMouseThread->updateConfig(
            config.detection_resolution,
            config.fovX,
            config.fovY,
            config.minSpeedMultiplier,
            config.maxSpeedMultiplier,
            config.predictionInterval,
            config.auto_shoot,
            config.bScope_multiplier,
            config.triggerbot_bScope_multiplier);

        config.saveConfig();
        globalMouseThread->setSmoothnessValue(config.smoothness);
        globalMouseThread->setUseSmoothing(config.use_smoothing);
        globalMouseThread->setUseKalman(config.use_kalman);
        globalMouseThread->setKalmanParams(
            config.kalman_process_noise,
            config.kalman_measurement_noise
        );
    }

    if (prev_wind_mouse_enabled != config.wind_mouse_enabled ||
        prev_wind_G != config.wind_G ||
        prev_wind_W != config.wind_W ||
        prev_wind_M != config.wind_M ||
        prev_wind_D != config.wind_D)
    {
        prev_wind_mouse_enabled = config.wind_mouse_enabled;
        prev_wind_G = config.wind_G;
        prev_wind_W = config.wind_W;
        prev_wind_M = config.wind_M;
        prev_wind_D = config.wind_D;

        globalMouseThread->updateConfig(
            config.detection_resolution,
            config.fovX,
            config.fovY,
            config.minSpeedMultiplier,
            config.maxSpeedMultiplier,
            config.predictionInterval,
            config.auto_shoot,
            config.bScope_multiplier,
            config.triggerbot_bScope_multiplier);

        config.saveConfig();
    }

    if (prev_auto_shoot != config.auto_shoot ||
        prev_bScope_multiplier != config.bScope_multiplier ||
        prev_triggerbot != config.triggerbot ||
        prev_triggerbot_bScope_multiplier != config.triggerbot_bScope_multiplier ||
        prev_triggerbot_reaction_ms != config.triggerbot_reaction_ms)
    {
        prev_auto_shoot = config.auto_shoot;
        prev_bScope_multiplier = config.bScope_multiplier;
        prev_triggerbot = config.triggerbot;
        prev_triggerbot_bScope_multiplier = config.triggerbot_bScope_multiplier;
        prev_triggerbot_reaction_ms = config.triggerbot_reaction_ms;

        globalMouseThread->updateConfig(
            config.detection_resolution,
            config.fovX,
            config.fovY,
            config.minSpeedMultiplier,
            config.maxSpeedMultiplier,
            config.predictionInterval,
            config.auto_shoot,
            config.bScope_multiplier,
            config.triggerbot_bScope_multiplier);

        config.saveConfig();
    }
}

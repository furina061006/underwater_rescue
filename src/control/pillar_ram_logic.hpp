#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

/**
 * @file pillar_ram_logic.hpp
 * @brief 水中救援的纯逻辑状态机：搜索、对准、轻撞、冷却、事件判定、重试。
 *
 * 本文件不依赖 OpenCV、MAVLink 或硬件，可在开发机上用 g++ 直接测试。
 */

/**
 * @brief 救援冲撞模式的全部可调参数。
 *
 * @note 默认轻撞档为 ±100 us / 5 Hz / 0.6 s，和现有已验证巡线幅度一致；
 * ram_amp 可降到 50，也可升到 300，最终输出由 pwm_min/pwm_max 钳位。
 */
struct PillarTuning {
    double pwm_min = 1200, pwm_max = 1800;
    double aim_x = .50;
    double trigger_width = .20, deadband = .08;
    double steer_gain = 300, cruise_amp = 100, cruise_freq = 4;
    double scan_bias = 150, scan_period = 4;
    double lost_s = .30, confirm_s = .10;
    double ram_amp = 100, ram_freq = 5, ram_burst = .60;
    double recover_s = .60, verify_s = .50;
    double avoid_s = 1.5, avoid_bias = 200;
    double hit_width_delta = .30, hit_x_delta = .15;
    int retry_max = 3;
};

/**
 * @brief 轻撞支柱的状态机。
 *
 * 状态：SEARCH=0、ALIGN=1、RAM=2、RECOVER=3、VERIFY=4、AVOID=5。
 * 每次 step() 返回当前周期应下发的 CH10 PWM（us）。
 */
struct PillarRamLogic {
    enum State { SEARCH = 0, ALIGN = 1, RAM = 2, RECOVER = 3, VERIFY = 4, AVOID = 5 };

    State state = SEARCH;
    int hits = 0, retries = 0, last_event = 0;
    double state_since = 0;
    double last_seen = -1, last_x = -1, last_width = 0;
    double ram_start = 0, recover_start = 0, verify_start = 0, avoid_start = 0;
    double candidate_since = -1;
    double before_width = 0, before_x = 0;
    int last_pwm = 0;

    /**
     * @brief 把值钳位到 [lo,hi]。
     * @param v 输入值。
     * @param lo 下限。
     * @param hi 上限。
     * @return 钳位后的值。
     */
    static double clamp_value(double v, double lo, double hi) {
        return std::max(lo, std::min(hi, v));
    }

    /**
     * @brief 推进一步救援状态机。
     * @param now 当前单调时钟，单位秒。
     * @param t 本次运行已进行的秒数，用于巡航/扫描正弦相位。
     * @param stamp 视觉状态时间戳，单位秒；必须新鲜（0 < now-stamp <= 0.5）。
     * @param found 当前帧是否找到支柱。
     * @param x 支柱中心横向比例 [0,1]；found=false 时可传 -1。
     * @param width_frac 支柱短边占画面宽度比例 [0,1]；found=false 时可传 0。
     * @param aspect 支柱长宽比 H/W；found=false 时可传 0。
     * @param c 可调参数。
     * @return CH10 目标 PWM，单位 us。
     * @exception std::runtime_error 视觉状态过期/非法，或 found=true 时目标字段非法。
     */
    int step(double now, double t, double stamp, bool found, double x,
             double width_frac, double aspect, const PillarTuning &c) {
        static const double PI = 3.14159265358979323846;

        if (!std::isfinite(now) || !std::isfinite(stamp) || stamp <= 0 ||
            stamp > now || now - stamp > .5) {
            throw std::runtime_error("Rescue vision stale/invalid");
        }
        if (found) {
            if (!std::isfinite(x) || x < 0 || x > 1 ||
                !std::isfinite(width_frac) || width_frac <= 0 || width_frac > 1 ||
                !std::isfinite(aspect) || aspect <= 0) {
                throw std::runtime_error("Rescue target invalid");
            }
        }

        auto output = [&](double v) {
            return int(std::lround(clamp_value(v, c.pwm_min, c.pwm_max)));
        };

        const bool visible = found && width_frac >= .02;
        if (visible) {
            last_seen = now;
            last_x = x;
            last_width = width_frac;
        }

        auto start_ram = [&](double width, double tx, double when) {
            state = RAM;
            ram_start = when;
            before_width = width;
            before_x = tx;
            ++hits;
            ++retries;
            last_event = 0;
            state_since = when;
            candidate_since = -1;
        };

        if (state == SEARCH) {
            if (last_seen < 0 || now - last_seen > c.avoid_s) retries = 0;
            const double phase = 2 * PI * t / std::max(.1, c.scan_period);
            int pwm = output(1500 + c.scan_bias * std::sin(phase) +
                             c.cruise_amp * std::sin(2 * PI * c.cruise_freq * t));
            if (visible) {
                state = ALIGN;
                state_since = now;
                candidate_since = -1;
            }
            last_pwm = pwm;
            return pwm;
        }

        if (state == ALIGN) {
            if (visible) {
                const double err = x - c.aim_x;
                int pwm = output(1500 + c.steer_gain * err +
                                 c.cruise_amp * std::sin(2 * PI * c.cruise_freq * t));
                if (std::fabs(err) <= c.deadband && width_frac >= c.trigger_width) {
                    if (candidate_since < 0) candidate_since = now;
                    if (now - candidate_since >= c.confirm_s) {
                        start_ram(width_frac, x, now);
                        last_pwm = output(1500);
                        return last_pwm;
                    }
                } else {
                    candidate_since = -1;
                }
                last_pwm = pwm;
                return pwm;
            }

            /* 近距离目标可能被画面裁掉：最近一次宽度已达阈值，短暂丢失也盲撞。 */
            if (last_seen >= 0 && last_width >= c.trigger_width && now - last_seen <= .2) {
                start_ram(last_width, last_x, now);
                last_pwm = output(1500);
                return last_pwm;
            }
            if (last_seen >= 0 && now - last_seen <= c.lost_s) {
                return last_pwm;
            }
            state = SEARCH;
            state_since = now;
            candidate_since = -1;
            last_pwm = 0;
            return 0;
        }

        if (state == RAM) {
            int pwm = output(1500 + c.ram_amp *
                             std::sin(2 * PI * c.ram_freq * (now - ram_start)));
            if (now - ram_start >= c.ram_burst) {
                state = RECOVER;
                recover_start = now;
                state_since = now;
                last_pwm = 0;
                return 0;
            }
            last_pwm = pwm;
            return pwm;
        }

        if (state == RECOVER) {
            if (now - recover_start >= c.recover_s) {
                state = VERIFY;
                verify_start = now;
                state_since = now;
            }
            last_pwm = 0;
            return 0;
        }

        if (state == VERIFY) {
            bool event = !visible;
            if (!event) {
                const double width_change =
                    std::fabs(width_frac - before_width) / std::max(before_width, .01);
                const double x_change = std::fabs(x - before_x);
                event = width_change > c.hit_width_delta || x_change > c.hit_x_delta;
            }
            if (now - verify_start >= c.verify_s) {
                last_event = event ? 1 : 0;
                if (event) {
                    retries = 0;
                    state = SEARCH;
                    state_since = now;
                    candidate_since = -1;
                } else if (retries <= c.retry_max) {
                    state = ALIGN;
                    state_since = now;
                    candidate_since = -1;
                } else {
                    state = AVOID;
                    avoid_start = now;
                    state_since = now;
                }
            }
            last_pwm = 0;
            return 0;
        }

        if (state == AVOID) {
            int pwm = output(1500 + c.avoid_bias +
                             c.cruise_amp * std::sin(2 * PI * c.cruise_freq * t));
            if (now - avoid_start >= c.avoid_s) {
                state = SEARCH;
                state_since = now;
                retries = 0;
            }
            last_pwm = pwm;
            return pwm;
        }

        last_pwm = 0;
        return 0;
    }
};

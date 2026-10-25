/**
 * @file test_pillar_ram.cpp
 * @brief 验证救援轻撞状态机的搜索、对准、轻撞、事件判定、重试与钳位。
 *
 * 纯逻辑测试：不依赖 OpenCV、MAVLink、相机或飞控。
 */
#include "pillar_ram_logic.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

/**
 * @brief 用 now 同时作为 t 和 stamp 推进一步。
 */
static int feed(PillarRamLogic &r, double now, bool found, double x,
                double width_frac, double aspect, const PillarTuning &c) {
    return r.step(now, now, now, found, x, width_frac, aspect, c);
}

/**
 * @brief 把状态机推进到 RAM 起点。
 * @param r 待推进的状态机。
 * @param c 参数。
 * @param start 起始时间。
 * @return RAM 起始时间。
 */
static double enter_ram(PillarRamLogic &r, const PillarTuning &c, double start) {
    feed(r, start, true, .50, .05, 2.4, c);
    assert(r.state == PillarRamLogic::ALIGN);
    feed(r, start + .10, true, .50, .25, 2.4, c);
    assert(r.state == PillarRamLogic::ALIGN);
    feed(r, start + .25, true, .50, .25, 2.4, c);
    assert(r.state == PillarRamLogic::RAM);
    return start + .25;
}

/**
 * @brief 执行全部断言。
 * @return 0 表示通过。
 */
int main() {
    PillarTuning c;

    /* 1. 无目标时在 SEARCH 摆动，输出在 1200..1800 且不为 0。 */
    {
        PillarRamLogic r;
        int p = feed(r, 1.0, false, -1, 0, 0, c);
        assert(r.state == PillarRamLogic::SEARCH);
        assert(p >= c.pwm_min && p <= c.pwm_max && p != 0);
    }

    /* 2. 居中且宽度达阈值持续 0.1 s -> RAM；轻撞后事件命中 -> SEARCH。 */
    {
        PillarRamLogic r;
        double ram = enter_ram(r, c, 1.0);
        assert(r.hits == 1 && r.retries == 1);
        int p = feed(r, ram + .05, true, .50, .25, 2.4, c);
        assert(r.state == PillarRamLogic::RAM);
        assert(p >= 1400 && p <= 1600);
        p = feed(r, ram + .61, true, .50, .25, 2.4, c);
        assert(r.state == PillarRamLogic::RECOVER && p == 0);
        p = feed(r, ram + .61 + .61, true, .50, .25, 2.4, c);
        assert(r.state == PillarRamLogic::VERIFY && p == 0);
        p = feed(r, ram + .61 + .61 + .51, false, -1, 0, 0, c);
        assert(r.state == PillarRamLogic::SEARCH && r.last_event == 1 && r.retries == 0);
        assert(p == 0);
    }

    /* 3. VERIFY 中目标仍在且没变化 -> 未命中；retry_max=0 时进入 AVOID。 */
    {
        PillarTuning d;
        d.retry_max = 0;
        PillarRamLogic r;
        double ram = enter_ram(r, d, 2.0);
        feed(r, ram + .61, true, .50, .25, 2.4, d);
        assert(r.state == PillarRamLogic::RECOVER);
        feed(r, ram + 1.22, true, .50, .25, 2.4, d);
        assert(r.state == PillarRamLogic::VERIFY);
        feed(r, ram + 1.73, true, .50, .25, 2.4, d);
        assert(r.state == PillarRamLogic::AVOID && r.last_event == 0);
        feed(r, ram + 1.73 + 1.51, true, .50, .25, 2.4, d);
        assert(r.state == PillarRamLogic::SEARCH && r.retries == 0);
    }

    /* 4. 近距离目标刚丢失（<=0.2 s）且上次宽度达标 -> 盲撞 RAM。 */
    {
        PillarRamLogic r;
        feed(r, 1.0, true, .50, .25, 2.4, c);
        assert(r.state == PillarRamLogic::ALIGN);
        feed(r, 1.1, false, -1, 0, 0, c);
        assert(r.state == PillarRamLogic::RAM);
    }

    /* 5. 视觉陈旧 / 非法目标 -> 抛异常。 */
    {
        PillarRamLogic r;
        bool threw = false;
        try {
            r.step(1.0, 1.0, .4, false, -1, 0, 0, c);
        } catch (const std::runtime_error &) {
            threw = true;
        }
        assert(threw);

        threw = false;
        try {
            r.step(1.0, 1.0, 1.0, true, 1.2, .2, 2.4, c);
        } catch (const std::runtime_error &) {
            threw = true;
        }
        assert(threw);
    }

    /* 6. 轻撞幅度钳位：ram_amp=300 到 1800/1200，ram_amp=50 到 1550。 */
    {
        PillarTuning hi = c;
        hi.ram_amp = 300;
        PillarRamLogic r;
        double ram = enter_ram(r, hi, 3.0);
        assert(feed(r, ram + .05, true, .50, .25, 2.4, hi) == 1800);
        assert(feed(r, ram + .15, true, .50, .25, 2.4, hi) == 1200);
    }
    {
        PillarTuning lo = c;
        lo.ram_amp = 50;
        PillarRamLogic r;
        double ram = enter_ram(r, lo, 4.0);
        assert(feed(r, ram + .05, true, .50, .25, 2.4, lo) == 1550);
    }

    std::cout << "PILLAR_RAM_LOGIC_TEST_PASS" << std::endl;
    return 0;
}

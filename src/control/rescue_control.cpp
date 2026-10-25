#include <mavlink/v2.0/ardupilotmega/mavlink.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <vector>

/**
 * @file rescue_control.cpp
 * @brief 水中救援的 CH10 鱼尾控制器：轻触支柱、撞后事件判定、收尾。
 *
 * @details 程序独占飞控串口，完成 CH10 预检后以 50 Hz 读取视觉状态，
 * 通过 MAVLink 下发 CH10 PWM。只允许在未解锁状态运行；不解锁、不切模式、
 * 不发 RC override，也不操作 CH11 云台或 CH12 灯。
 */

static const char *DEFAULT_PORT = "/dev/serial/by-id/usb-ArduPilot_Pixhawk6X-bdshot_20001F001551333234343538-if00";
static volatile sig_atomic_t interrupted = 0;

/**
 * @brief 记录中断标志。
 * @param 忽略的信号编号。
 */
static void on_signal(int) { interrupted = 1; }

/**
 * @brief 返回单调时钟秒数。
 * @return steady_clock 秒数。
 */
static double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/**
 * @brief 飞控串口与 MAVLink 会话。
 */
struct Link {
    int fd = -1;
    termios previous{};
    bool saved = false;
    uint8_t system = 0, component = 0;
    bool armed = true;
    double heartbeat = 0, output_time = 0;
    int output = -1;
    unsigned output_seq = 0, rejected = 0;
    double ack_time = 0;
    std::map<std::string, float> params;
    std::map<std::string, unsigned> param_seq;
    std::map<int, std::pair<unsigned, int>> acks;
    mavlink_message_t message{};
    mavlink_status_t parser{};

    /**
     * @brief 打开并独占飞控串口。
     * @param port 串口设备路径。
     * @exception std::runtime_error 打开、flock、TIOCEXCL 或 termios 失败。
     */
    explicit Link(const std::string &port) {
        fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) throw std::runtime_error("Cannot open serial port: " + std::string(strerror(errno)));
        if (flock(fd, LOCK_EX | LOCK_NB) < 0 || ioctl(fd, TIOCEXCL) < 0 || tcgetattr(fd, &previous) < 0) {
            close(fd); fd = -1; throw std::runtime_error("Cannot exclusively lock/configure serial port");
        }
        saved = true;
        termios config = previous;
        cfmakeraw(&config);
        cfsetispeed(&config, B115200); cfsetospeed(&config, B115200);
        config.c_cflag |= CLOCAL | CREAD;
        config.c_cflag &= ~(CRTSCTS | HUPCL);
        if (tcsetattr(fd, TCSANOW, &config) < 0) {
            ioctl(fd, TIOCNXCL); close(fd); fd = -1;
            throw std::runtime_error("Cannot configure serial port");
        }
    }

    /**
     * @brief 恢复 termios 并释放串口。
     */
    ~Link() {
        if (fd >= 0) {
            if (saved) tcsetattr(fd, TCSANOW, &previous);
            ioctl(fd, TIOCNXCL); close(fd);
        }
    }

    /**
     * @brief 完整写出一条 MAVLink 消息。
     * @param msg 消息。
     * @exception std::runtime_error 写失败或 20 ms 超时。
     */
    void send(mavlink_message_t &msg) {
        uint8_t data[MAVLINK_MAX_PACKET_LEN];
        int len = mavlink_msg_to_send_buffer(data, &msg), done = 0;
        double deadline = seconds() + .02;
        while (done < len && seconds() < deadline) {
            int n = write(fd, data + done, len - done);
            if (n > 0) done += n;
            else if (errno != EAGAIN && errno != EINTR) throw std::runtime_error("Serial write failed");
            else { pollfd p{fd, POLLOUT, 0}; poll(&p, 1, 1); }
        }
        if (done != len) throw std::runtime_error("Serial write timeout");
    }

    /**
     * @brief 轮询并解析串口数据。
     * @param milliseconds poll 超时毫秒数。
     * @exception std::runtime_error poll/read 失败或串口断开。
     */
    void pump(int milliseconds = 50) {
        pollfd p{fd, POLLIN, 0};
        int ready = poll(&p, 1, milliseconds);
        if (ready < 0 && errno != EINTR) throw std::runtime_error("Serial poll failed");
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("Serial disconnected");
        if (!(p.revents & POLLIN)) return;
        uint8_t bytes[4096];
        int n = read(fd, bytes, sizeof(bytes));
        if (n < 0 && errno != EAGAIN && errno != EINTR) throw std::runtime_error("Serial read failed");
        for (int i = 0; i < n; ++i) {
            if (!mavlink_parse_char(MAVLINK_COMM_0, bytes[i], &message, &parser)) continue;
            if (!system && message.msgid == MAVLINK_MSG_ID_HEARTBEAT && message.compid == 1) {
                mavlink_heartbeat_t h{};
                mavlink_msg_heartbeat_decode(&message, &h);
                if (h.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA && h.type == MAV_TYPE_SUBMARINE) {
                    system = message.sysid; component = message.compid;
                }
            }
            if (!system || message.sysid != system || message.compid != component) continue;
            if (message.msgid == MAVLINK_MSG_ID_HEARTBEAT) {
                mavlink_heartbeat_t h{};
                mavlink_msg_heartbeat_decode(&message, &h);
                heartbeat = seconds();
                armed = (h.base_mode & MAV_MODE_FLAG_SAFETY_ARMED) != 0;
            } else if (message.msgid == MAVLINK_MSG_ID_PARAM_VALUE) {
                mavlink_param_value_t v{};
                mavlink_msg_param_value_decode(&message, &v);
                std::string key(v.param_id, strnlen(v.param_id, 16));
                params[key] = v.param_value;
                ++param_seq[key];
            } else if (message.msgid == MAVLINK_MSG_ID_COMMAND_ACK) {
                mavlink_command_ack_t v{};
                mavlink_msg_command_ack_decode(&message, &v);
                ++acks[v.command].first;
                acks[v.command].second = v.result;
                if (v.command == MAV_CMD_DO_SET_SERVO) {
                    ack_time = seconds();
                    if (v.result != MAV_RESULT_ACCEPTED) ++rejected;
                }
            } else if (message.msgid == MAVLINK_MSG_ID_SERVO_OUTPUT_RAW) {
                mavlink_servo_output_raw_t v{};
                mavlink_msg_servo_output_raw_decode(&message, &v);
                if (v.port == 0) {
                    output = v.servo10_raw;
                    output_time = seconds();
                    ++output_seq;
                }
            }
        }
    }

    /**
     * @brief 校验未中断、心跳新鲜、飞控未解锁。
     * @exception std::runtime_error 任一条件不满足。
     */
    void require_idle() {
        if (interrupted) throw std::runtime_error("Interrupted");
        if (!system || seconds() - heartbeat > 2.5)
            throw std::runtime_error("Heartbeat timeout");
        if (armed) throw std::runtime_error("Flight controller is armed; test stopped");
    }

    /**
     * @brief 发送 COMMAND_LONG。
     * @param cmd 命令号。
     * @param p1 参数 1。
     * @param p2 参数 2。
     */
    void command(int cmd, float p1, float p2 = 0) {
        mavlink_message_t m{};
        mavlink_msg_command_long_pack(245, 190, &m, system, component, cmd, 0, p1, p2, 0, 0, 0, 0, 0);
        send(m);
    }

    /**
     * @brief 读取参数并等待回读。
     * @param name 参数名。
     * @return 参数值。
     * @exception std::runtime_error 3 次请求都没有回复。
     */
    float parameter(const std::string &name) {
        char id[16]{};
        name.copy(id, sizeof(id));
        for (int attempt = 0; attempt < 3; ++attempt) {
            unsigned seq = param_seq[name];
            mavlink_message_t m{};
            mavlink_msg_param_request_read_pack(245, 190, &m, system, component, id, -1);
            send(m);
            double end = seconds() + 1;
            while (seconds() < end) {
                pump();
                if (param_seq[name] > seq) return params[name];
            }
        }
        throw std::runtime_error("No parameter reply: " + name);
    }

    /**
     * @brief 设置 SERVO10_FUNCTION 并等待回读确认。
     * @param value 目标功能值。
     * @exception std::runtime_error 3 次设置后回读仍不一致。
     */
    void function(int value) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            unsigned seq = param_seq["SERVO10_FUNCTION"];
            mavlink_message_t m{};
            mavlink_msg_param_set_pack(245, 190, &m, system, component, "SERVO10_FUNCTION",
                                       float(value), MAV_PARAM_TYPE_INT16);
            send(m);
            double end = seconds() + 1;
            while (seconds() < end) {
                pump();
                if (param_seq["SERVO10_FUNCTION"] > seq && params["SERVO10_FUNCTION"] == value) return;
            }
        }
        throw std::runtime_error("SERVO10_FUNCTION change was not confirmed");
    }

    /**
     * @brief 向 CH10 下发 DO_SET_SERVO 并等待 ACK。
     * @param value PWM，单位 us。
     * @exception std::runtime_error 命令被拒绝或 1.5 s 内无 ACK。
     */
    void pwm(int value) {
        unsigned seq = acks[MAV_CMD_DO_SET_SERVO].first;
        command(MAV_CMD_DO_SET_SERVO, 10, value);
        double end = seconds() + 1.5;
        while (seconds() < end) {
            pump();
            if (acks[MAV_CMD_DO_SET_SERVO].first > seq) {
                if (acks[MAV_CMD_DO_SET_SERVO].second != MAV_RESULT_ACCEPTED)
                    throw std::runtime_error("SET_SERVO rejected");
                return;
            }
        }
        throw std::runtime_error("SET_SERVO acknowledgement timeout");
    }

    /**
     * @brief 请求 SERVO_OUTPUT_RAW 并校验 CH10 实际输出。
     * @param expected 期望值。
     * @param timeout 超时秒数。
     * @return true 表示收到新遥测且等于期望值。
     */
    bool verify_output(int expected, double timeout = 1.5) {
        unsigned seq = output_seq;
        command(MAV_CMD_REQUEST_MESSAGE, MAVLINK_MSG_ID_SERVO_OUTPUT_RAW);
        double end = seconds() + timeout;
        while (seconds() < end) {
            pump();
            if (output_seq > seq && output == expected) return true;
        }
        return false;
    }
};

/**
 * @brief 一个 20 ms 控制周期的采样。
 */
struct Sample {
    double t, late;
    int pwm, actual;
    unsigned seq;
    int state;
    double x, width;
    int event;
};

#include "pillar_ram_logic.hpp"

static PillarTuning cfg;
static PillarRamLogic logic;
static int g_state = 0;
static double g_x = -1, g_width = 0;
static int g_event = 0;

/**
 * @brief 状态编号转日志名称。
 * @param s 状态编号。
 * @return 静态字符串。
 */
static const char *state_name(int s) {
    switch (s) {
        case PillarRamLogic::SEARCH: return "SEARCH";
        case PillarRamLogic::ALIGN: return "ALIGN";
        case PillarRamLogic::RAM: return "RAM";
        case PillarRamLogic::RECOVER: return "RECOVER";
        case PillarRamLogic::VERIFY: return "VERIFY";
        case PillarRamLogic::AVOID: return "AVOID";
        default: return "?";
    }
}

/**
 * @brief 去掉首尾空白。
 * @param s 输入字符串。
 * @return 去空白后的副本。
 */
static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/**
 * @brief 从 rescue_tuning.txt 读取并校验参数。
 * @param path 文件路径。
 * @return 生效参数。
 * @exception std::runtime_error 键值非法或越界。
 * @note 缺失键用默认值；未知键（视觉参数）忽略。
 */
static PillarTuning load_tuning(const std::string &path) {
    PillarTuning c;
    std::ifstream f(path);
    if (!f) {
        std::cout << "RESCUE_TUNING default (no " << path << ")" << std::endl;
        return c;
    }
    std::map<std::string, double> values;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key.empty() || val.empty()) continue;
        try {
            values[key] = std::stod(val);
        } catch (...) {
            throw std::runtime_error("RESCUE_TUNING_INVALID " + key);
        }
    }
    auto get = [&](const char *k, double def) {
        auto it = values.find(k);
        return it == values.end() ? def : it->second;
    };
    c.pwm_min = get("pwm_min", c.pwm_min);
    c.pwm_max = get("pwm_max", c.pwm_max);
    c.aim_x = get("aim_x", c.aim_x);
    c.trigger_width = get("trigger_width", c.trigger_width);
    c.deadband = get("deadband", c.deadband);
    c.steer_gain = get("steer_gain", c.steer_gain);
    c.cruise_amp = get("cruise_amp", c.cruise_amp);
    c.cruise_freq = get("cruise_freq", c.cruise_freq);
    c.scan_bias = get("scan_bias", c.scan_bias);
    c.scan_period = get("scan_period", c.scan_period);
    c.lost_s = get("lost_s", c.lost_s);
    c.confirm_s = get("confirm_s", c.confirm_s);
    c.ram_amp = get("ram_amp", c.ram_amp);
    c.ram_freq = get("ram_freq", c.ram_freq);
    c.ram_burst = get("ram_burst", c.ram_burst);
    c.recover_s = get("recover_s", c.recover_s);
    c.verify_s = get("verify_s", c.verify_s);
    c.avoid_s = get("avoid_s", c.avoid_s);
    c.avoid_bias = get("avoid_bias", c.avoid_bias);
    c.hit_width_delta = get("hit_width_delta", c.hit_width_delta);
    c.hit_x_delta = get("hit_x_delta", c.hit_x_delta);
    double retry = get("retry_max", c.retry_max);
    if (!std::isfinite(retry) || retry < 0 || retry > 5)
        throw std::runtime_error("RESCUE_TUNING_INVALID retry_max");
    c.retry_max = int(retry);

    auto bad = [](const char *k) {
        throw std::runtime_error(std::string("RESCUE_TUNING_INVALID ") + k);
    };
    if (!(c.pwm_min >= 1000 && c.pwm_min <= 1500)) bad("pwm_min");
    if (!(c.pwm_max >= 1500 && c.pwm_max <= 2000)) bad("pwm_max");
    if (!(c.pwm_min < c.pwm_max)) bad("pwm_range");
    if (!(c.aim_x >= .2 && c.aim_x <= .8)) bad("aim_x");
    if (!(c.trigger_width >= .02 && c.trigger_width <= .8)) bad("trigger_width");
    if (!(c.deadband >= 0 && c.deadband <= .5)) bad("deadband");
    if (!(c.steer_gain >= 0 && c.steer_gain <= 1000)) bad("steer_gain");
    if (!(c.cruise_amp >= 0 && c.cruise_amp <= 300)) bad("cruise_amp");
    if (!(c.cruise_freq >= 1 && c.cruise_freq <= 10)) bad("cruise_freq");
    if (!(c.scan_bias >= 0 && c.scan_bias <= 400)) bad("scan_bias");
    if (!(c.scan_period >= 1 && c.scan_period <= 10)) bad("scan_period");
    if (!(c.lost_s >= .1 && c.lost_s <= 2)) bad("lost_s");
    if (!(c.confirm_s >= .02 && c.confirm_s <= 1)) bad("confirm_s");
    if (!(c.ram_amp >= 0 && c.ram_amp <= 300)) bad("ram_amp");
    if (!(c.ram_freq >= 1 && c.ram_freq <= 10)) bad("ram_freq");
    if (!(c.ram_burst >= .1 && c.ram_burst <= 3)) bad("ram_burst");
    if (!(c.recover_s >= 0 && c.recover_s <= 3)) bad("recover_s");
    if (!(c.verify_s >= .1 && c.verify_s <= 3)) bad("verify_s");
    if (!(c.avoid_s >= 0 && c.avoid_s <= 5)) bad("avoid_s");
    if (!(c.avoid_bias >= 0 && c.avoid_bias <= 400)) bad("avoid_bias");
    if (!(c.hit_width_delta >= .01 && c.hit_width_delta <= 1)) bad("hit_width_delta");
    if (!(c.hit_x_delta >= .01 && c.hit_x_delta <= 1)) bad("hit_x_delta");
    if (!(c.retry_max >= 0 && c.retry_max <= 5)) bad("retry_max");
    if (c.ram_amp > (c.pwm_max - c.pwm_min) / 2.0) bad("ram_amp_vs_range");
    std::cout << "RESCUE_TUNING ram_amp=" << c.ram_amp << " freq=" << c.ram_freq
              << " burst=" << c.ram_burst << " trigger=" << c.trigger_width
              << " pwm=" << c.pwm_min << ".." << c.pwm_max << std::endl;
    return c;
}

/**
 * @brief 读取视觉状态、推进状态机并发布状态。
 * @param t 本次运行已进行的秒数。
 * @return 本周期 CH10 目标 PWM。
 * @exception std::runtime_error 状态文件缺失或视觉状态过期/非法。
 */
static int target_rescue(double t) {
    double stamp = 0, x = -1, width = 0, aspect = 0;
    int found = 0;
    std::ifstream f("rescue_state.txt");
    if (!(f >> stamp >> found >> x >> width >> aspect))
        throw std::runtime_error("Missing rescue state");
    int before = logic.state;
    int value = logic.step(seconds(), t, stamp, found != 0, x, width, aspect, cfg);
    g_state = logic.state;
    g_x = x;
    g_width = width;
    g_event = logic.last_event;
    if (before != logic.state) {
        std::cout << "PILLAR " << state_name(logic.state) << " time=" << t
                  << " x=" << x << " width=" << width << " pwm=" << value
                  << " hits=" << logic.hits << " retries=" << logic.retries
                  << " event=" << logic.last_event << std::endl;
    }
    std::ofstream status("rescue_status.tmp");
    status << std::setprecision(12) << seconds() << " " << logic.state << " "
           << value << " " << t << " " << width << " " << logic.last_event << "\n";
    status.close();
    if (rename("rescue_status.tmp", "rescue_status.txt") != 0)
        throw std::runtime_error("Cannot publish rescue status");
    return value;
}

/**
 * @brief 写 CSV 并打印采样统计。
 * @param v 采样。
 * @param skipped 跳过的周期数。
 * @param file CSV 路径。
 * @exception std::runtime_error 文件无法写入。
 */
static void report(const std::vector<Sample> &v, unsigned skipped, const std::string &file) {
    std::ofstream f(file);
    f << "time_s,lateness_ms,target_us,telemetry_us,telemetry_seq,state,x,width_frac,event\n";
    std::vector<double> intervals;
    double late = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        const Sample &s = v[i];
        f << std::fixed << std::setprecision(6) << s.t << "," << s.late * 1000 << ","
          << s.pwm << "," << s.actual << "," << s.seq << "," << s.state << ","
          << s.x << "," << s.width << "," << s.event << "\n";
        late = std::max(late, s.late);
        if (i) intervals.push_back((s.t - v[i - 1].t) * 1000);
    }
    std::sort(intervals.begin(), intervals.end());
    std::cout << "SAMPLES=" << v.size() << " SKIPPED=" << skipped
              << " MAX_LATENESS_MS=" << late * 1000;
    if (!intervals.empty())
        std::cout << " INTERVAL_P50_MS=" << intervals[intervals.size() / 2]
                  << " P99_MS=" << intervals[std::min(intervals.size() - 1,
                                                      size_t(intervals.size() * .99))]
                  << " MAX_MS=" << intervals.back();
    std::cout << " LOG=" << file << std::endl;
    if (!f) throw std::runtime_error("Log write failed");
}

/**
 * @brief 控制器入口：预检、50 Hz 循环、收尾与报告。
 * @param argc 参数个数。
 * @param argv 参数数组；支持 --port、--recovery-file、--log、--duration、--dry-run。
 * @return 0 正常；1 运行/收尾失败；2 参数非法。
 */
int main(int argc, char **argv) {
    std::string port = DEFAULT_PORT;
    std::string recovery = "rescue_recovery.txt";
    std::string log = "rescue_run.csv";
    std::string tuning = "rescue_tuning.txt";
    double duration = 290;
    bool dry = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = argv[++i];
        else if (a == "--recovery-file" && i + 1 < argc) recovery = argv[++i];
        else if (a == "--log" && i + 1 < argc) log = argv[++i];
        else if (a == "--duration" && i + 1 < argc) duration = std::stod(argv[++i]);
        else if (a == "--dry-run") dry = true;
        else return 2;
    }
    if (!std::isfinite(duration) || duration < 2 || duration > 300) return 2;
    std::cout.setf(std::ios::unitbuf);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);
    std::vector<Sample> samples;
    samples.reserve(600);
    unsigned skipped = 0;
    try {
        cfg = load_tuning(tuning);
        if (dry) {
            double start = seconds();
            for (unsigned k = 0; k <= unsigned(std::round(duration / .02)); ++k) {
                double deadline = start + k * .02;
                timespec ts{time_t(deadline), long((deadline - std::floor(deadline)) * 1e9)};
                clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
                double now = seconds();
                int value = target_rescue(std::min(duration, now - start));
                samples.push_back({now - start, std::max(0.0, now - deadline), value,
                                   -1, 0, g_state, g_x, g_width, g_event});
            }
            report(samples, 0, log);
            return 0;
        }

        Link l(port);
        double end = seconds() + 6;
        while (!l.system && seconds() < end && !interrupted) l.pump();
        l.require_idle();

        /* 旧巡线恢复记录也视为不安全状态，必须人工检查。 */
        if (std::ifstream("route_recovery.txt").good())
            throw std::runtime_error("Old route_recovery.txt exists; inspect manually");

        int original = int(l.parameter("SERVO10_FUNCTION"));
        int mount = int(l.parameter("MNT1_TYPE"));
        int rate = int(l.parameter("SERVO_RATE"));
        float minimum = l.parameter("SERVO10_MIN");
        float maximum = l.parameter("SERVO10_MAX");
        if (minimum > cfg.pwm_min || maximum < cfg.pwm_max)
            throw std::runtime_error("SERVO10 range excludes configured rescue PWM");
        if (rate != 50)
            throw std::runtime_error("Expected SERVO_RATE=50");
        if (!l.verify_output(0) || (original != 0 && original != 7) ||
            (original == 7 && mount != 0))
            throw std::runtime_error("CH10 not idle");

        int lock = open(recovery.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (lock < 0)
            throw std::runtime_error("Recovery record exists or cannot create");
        std::string saved = "SERVO10_FUNCTION=" + std::to_string(original) +
                            "\nSERVO10_PWM=0\nSERVO_RATE=" + std::to_string(rate) + "\n";
        bool saved_ok = write(lock, saved.data(), saved.size()) == ssize_t(saved.size()) &&
                        fsync(lock) == 0;
        close(lock);
        if (!saved_ok)
            throw std::runtime_error("Cannot persist recovery record");

        int rc = 0;
        try {
            if (original) l.function(0);
            l.require_idle();
            l.pwm(0);
            if (!l.verify_output(0))
                throw std::runtime_error("CH10 zero not confirmed");
            end = seconds() + .3;
            while (seconds() < end) {
                l.pump(5);
                l.require_idle();
            }
            const unsigned reject_base = l.rejected;
            double start = seconds(), next_query = start;
            unsigned tick = 0;
            while (true) {
                double deadline = start + tick * .02;
                while (seconds() < deadline) {
                    l.pump(std::max(0, std::min(2, int((deadline - seconds()) * 1000))));
                    l.require_idle();
                    if (l.rejected > reject_base)
                        throw std::runtime_error("Command rejected");
                    if (seconds() - std::max(start, l.ack_time) > .30)
                        throw std::runtime_error("ACK stalled");
                }
                l.pump(0);
                l.require_idle();
                double now = seconds();
                if (now - deadline > .06)
                    throw std::runtime_error("Scheduling stalled >60 ms");
                if (now - deadline >= .02) {
                    unsigned current = unsigned((now - start) / .02);
                    skipped += current - tick;
                    tick = current;
                    deadline = start + tick * .02;
                }
                double elapsed = std::min(duration, now - start);
                if (elapsed >= duration) break;
                int value = target_rescue(elapsed);
                l.command(MAV_CMD_DO_SET_SERVO, 10, value);
                samples.push_back({now - start, std::max(0.0, now - deadline), value,
                                   l.output, l.output_seq, g_state, g_x, g_width, g_event});
                if (now >= next_query) {
                    l.command(MAV_CMD_REQUEST_MESSAGE, MAVLINK_MSG_ID_SERVO_OUTPUT_RAW);
                    next_query = now + .1;
                }
                if (elapsed >= duration) break;
                ++tick;
            }
            if (l.rejected > reject_base)
                throw std::runtime_error("Command rejected");
            l.pwm(0);
            if (!l.verify_output(0))
                throw std::runtime_error("Final CH10 zero not confirmed");
            end = seconds() + .2;
            while (seconds() < end) {
                l.pump(5);
                l.require_idle();
            }
            std::cout << "RESCUE_TIME_LIMIT duration=" << duration
                      << "s state=" << logic.state << " hits=" << logic.hits
                      << " PWM_RATE=" << rate << "Hz\n";
        } catch (const std::exception &e) {
            std::cerr << "TEST_STOPPED " << e.what() << std::endl;
            rc = 1;
        }

        bool off = false, restored = false;
        try {
            l.pwm(0);
            off = l.verify_output(0);
        } catch (const std::exception &e) {
            std::cerr << "STOP " << e.what() << std::endl;
        }
        try {
            l.function(original);
            restored = int(l.parameter("SERVO10_FUNCTION")) == original;
        } catch (const std::exception &e) {
            std::cerr << "RESTORE " << e.what() << std::endl;
        }
        std::cout << "STOP=" << off << " RESTORE_FUNCTION=" << original
                  << " confirmed=" << restored << std::endl;
        if (off && restored) unlink(recovery.c_str());
        else rc = 1;
        report(samples, skipped, log);
        return rc;
    } catch (const std::exception &e) {
        std::cerr << "ERROR " << e.what() << std::endl;
        return 1;
    }
}

#include <mavlink/v2.0/ardupilotmega/mavlink.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <poll.h>
#include <sstream>
#include <set>
#include <vector>
#include <iomanip>
#include <time.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// Only SERVO10 is addressed. Never arm, change mode, or send RC overrides.
// Run with cwd = runtime directory (run_wide_search.py does this): the vision
// state files, status file and route_recovery.txt are all relative to cwd.
// Build: make control (needs mavlink headers, usually /opt/ros/noetic/include).
static const char *DEFAULT_PORT = "/dev/serial/by-id/usb-ArduPilot_Pixhawk6X-bdshot_20001F001551333234343538-if00";
static volatile sig_atomic_t interrupted = 0;
static void on_signal(int) { interrupted = 1; }
static double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Link {
    int fd = -1;
    termios previous{};
    bool saved = false;
    uint8_t system = 0, component = 0;
    bool armed = true;
    double heartbeat = 0, output_time = 0;
    int output = -1;
    unsigned output_seq = 0; unsigned rejected=0; double ack_time=0;
    std::map<std::string, float> params;
    std::map<std::string, unsigned> param_seq;
    std::map<int, std::pair<unsigned, int>> acks;
    std::set<std::string> pending_notices, shown_notices;
    mavlink_message_t message{};
    mavlink_status_t parser{};

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
    ~Link() {
        if (fd >= 0) {
            if (saved) tcsetattr(fd, TCSANOW, &previous);
            ioctl(fd, TIOCNXCL); close(fd);
        }
    }
    void send(mavlink_message_t &msg) {
        uint8_t data[MAVLINK_MAX_PACKET_LEN];
        int len = mavlink_msg_to_send_buffer(data, &msg), done = 0;
        double deadline = seconds() + 0.02;
        while (done < len && seconds() < deadline) {
            int n = write(fd, data + done, len - done);
            if (n > 0) done += n;
            else if (errno != EAGAIN && errno != EINTR) throw std::runtime_error("Serial write failed");
            else { pollfd p{fd, POLLOUT, 0}; poll(&p, 1, 1); }
        }
        if (done != len) throw std::runtime_error("Serial write timeout");
    }
    void pump(int milliseconds = 50) {
        pollfd p{fd, POLLIN, 0};
        int ready = poll(&p, 1, milliseconds);
        if (ready < 0 && errno != EINTR) throw std::runtime_error("Serial poll failed");
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("Serial disconnected");
        if (!(p.revents & POLLIN)) return;
        uint8_t bytes[4096]; int n = read(fd, bytes, sizeof(bytes));
        if (n < 0 && errno != EAGAIN && errno != EINTR) throw std::runtime_error("Serial read failed");
        for (int i = 0; i < n; ++i) {
            if (!mavlink_parse_char(MAVLINK_COMM_0, bytes[i], &message, &parser)) continue;
            if (!system && message.msgid == MAVLINK_MSG_ID_HEARTBEAT && message.compid == 1) {
                mavlink_heartbeat_t h{}; mavlink_msg_heartbeat_decode(&message, &h);
                if (h.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA && h.type == MAV_TYPE_SUBMARINE) {
                    system = message.sysid; component = message.compid;
                }
            }
            if (!system || message.sysid != system || message.compid != component) continue;
            if (message.msgid == MAVLINK_MSG_ID_HEARTBEAT) {
                mavlink_heartbeat_t h{}; mavlink_msg_heartbeat_decode(&message, &h);
                heartbeat = seconds(); armed = (h.base_mode & MAV_MODE_FLAG_SAFETY_ARMED) != 0;
            } else if (message.msgid == MAVLINK_MSG_ID_PARAM_VALUE) {
                mavlink_param_value_t v{}; mavlink_msg_param_value_decode(&message, &v);
                std::string key(v.param_id, strnlen(v.param_id, 16));
                params[key] = v.param_value; ++param_seq[key];
            } else if (message.msgid == MAVLINK_MSG_ID_COMMAND_ACK) {
                mavlink_command_ack_t v{}; mavlink_msg_command_ack_decode(&message, &v);
                ++acks[v.command].first; acks[v.command].second = v.result; if(v.command==MAV_CMD_DO_SET_SERVO){ack_time=seconds(); if(v.result!=MAV_RESULT_ACCEPTED)++rejected;}
            } else if (message.msgid == MAVLINK_MSG_ID_SERVO_OUTPUT_RAW) {
                mavlink_servo_output_raw_t v{}; mavlink_msg_servo_output_raw_decode(&message, &v);
                if (v.port == 0) { output = v.servo10_raw; output_time = seconds(); ++output_seq; }
            } else if (message.msgid == MAVLINK_MSG_ID_STATUSTEXT) {
                mavlink_statustext_t v{}; mavlink_msg_statustext_decode(&message, &v);
                std::string notice(v.text, strnlen(v.text, 50));
                if (!shown_notices.count(notice) && pending_notices.size() < 64)
                    pending_notices.insert(notice);
            }
        }
    }
    void print_notices() {
        // Only call between input lines, never while the user is typing.
        for (const auto &notice : pending_notices) {
            std::cout << "FC: " << notice << '\n';
            if (shown_notices.size() < 256) shown_notices.insert(notice);
        }
        pending_notices.clear();
    }
    void require_idle() {
        if (interrupted) throw std::runtime_error("Interrupted");
        if (!system || seconds() - heartbeat > 2.5)
            throw std::runtime_error("Heartbeat timeout (system=" + std::to_string(system) + ", parse_errors=" + std::to_string(parser.parse_error) + ")");
        if (armed) throw std::runtime_error("Flight controller is armed; test stopped");
    }
    void command(int cmd, float p1, float p2 = 0) {
        mavlink_message_t m{};
        mavlink_msg_command_long_pack(245, 190, &m, system, component, cmd, 0, p1, p2, 0, 0, 0, 0, 0);
        send(m);
    }
    float parameter(const std::string &name) {
        char id[16]{};
        name.copy(id, sizeof(id));
        for (int attempt = 0; attempt < 3; ++attempt) {
            unsigned seq = param_seq[name];
            mavlink_message_t m{};
            mavlink_msg_param_request_read_pack(245, 190, &m, system, component, id, -1);
            send(m);
            double end = seconds() + 1;
            while (seconds() < end) { pump(); if (param_seq[name] > seq) return params[name]; }
        }
        throw std::runtime_error("No parameter reply: " + name);
    }
    void function(int value) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            unsigned seq = param_seq["SERVO10_FUNCTION"];
            mavlink_message_t m{};
            mavlink_msg_param_set_pack(245, 190, &m, system, component, "SERVO10_FUNCTION", value, MAV_PARAM_TYPE_INT16);
            send(m);
            double end = seconds() + 1;
            while (seconds() < end) {
                pump();
                if (param_seq["SERVO10_FUNCTION"] > seq && params["SERVO10_FUNCTION"] == value) return;
            }
        }
        throw std::runtime_error("SERVO10_FUNCTION change was not confirmed");
    }
    void pwm(int value) {
        unsigned seq = acks[MAV_CMD_DO_SET_SERVO].first;
        command(MAV_CMD_DO_SET_SERVO, 10, value);
        double end = seconds() + 1.5;
        while (seconds() < end) {
            pump();
            if (acks[MAV_CMD_DO_SET_SERVO].first > seq) {
                if (acks[MAV_CMD_DO_SET_SERVO].second != MAV_RESULT_ACCEPTED)
                    throw std::runtime_error("SET_SERVO rejected, result=" + std::to_string(acks[MAV_CMD_DO_SET_SERVO].second));
                return;
            }
        }
        throw std::runtime_error("SET_SERVO acknowledgement timeout");
    }
    bool verify_output(int expected, double timeout = 1.5) {
        unsigned seq = output_seq;
        command(MAV_CMD_REQUEST_MESSAGE, MAVLINK_MSG_ID_SERVO_OUTPUT_RAW);
        double end = seconds() + timeout;
        while (seconds() < end) { pump(); if (output_seq > seq && output == expected) return true; }
        return false;
    }
};


struct Sample {double t,late; int pwm,actual; unsigned seq;};
#include "wide_search_logic.hpp"
#include "gap_light_logic.hpp"
static GapLamp lamp;
static unsigned relay_seq=0;static bool relay_pending=false;static double relay_sent=0;
static void lamp_command(Link& l,int value){
 relay_seq=l.acks[MAV_CMD_DO_SET_RELAY].first;
 l.command(MAV_CMD_DO_SET_RELAY,0,value);relay_sent=seconds();relay_pending=true;
 std::cout<<"CH12_LIGHT="<<value<<" time="<<relay_sent<<std::endl;
}
static void lamp_update(Link& l){
 double now=seconds();
 if(relay_pending){
  if(l.acks[MAV_CMD_DO_SET_RELAY].first>relay_seq){relay_pending=false;if(l.acks[MAV_CMD_DO_SET_RELAY].second!=MAV_RESULT_ACCEPTED)throw std::runtime_error("Lamp command rejected");}
  else if(now-relay_sent>.3)throw std::runtime_error("Lamp ACK timeout");
 }
 double stamp=0;int event=0;std::ifstream f("gap_light_state.txt");f>>stamp>>event;
 int action=lamp.step(now,stamp,event);if(action>=0)lamp_command(l,action);
}
static bool lamp_off(Link& l){
 lamp_command(l,0);double end=seconds()+1;
 while(seconds()<end){l.pump(5);if(l.acks[MAV_CMD_DO_SET_RELAY].first>relay_seq){relay_pending=false;return l.acks[MAV_CMD_DO_SET_RELAY].second==MAV_RESULT_ACCEPTED;}}
 return false;
}
static HorizontalLossRoute route;
static int target(double t,double duration){
 double stamp=0,x=-1;int horizontal=0,any=0;std::ifstream f("wide_search_state.txt");
 if(!(f>>stamp>>x>>horizontal>>any))throw std::runtime_error("Missing vision state");
 double ds=0,strength=0;int left=0;std::ifstream down("down_curve_state.txt");down>>ds>>left>>strength;
 int before=route.stage;int value=route.step(seconds(),t,stamp,x,horizontal,any,ds,left,strength);
 if(before!=route.stage)std::cout<<(route.stage==2?"DOWN LEFT ARC; FIXED 2s TURN center=850..900us +/-100 5Hz":"FIXED 2s TURN COMPLETE; FOLLOW")<<" time="<<t<<std::endl;
 std::ofstream status("wide_search_status.tmp");status<<std::setprecision(12)<<seconds()<<" "<<route.stage<<" "<<value<<" "<<t<<"\n";status.close();
 if(rename("wide_search_status.tmp","wide_search_status.txt")!=0)throw std::runtime_error("Cannot publish status");
 return value;
}
static void report(const std::vector<Sample>&v,unsigned skipped,const std::string&file){
 std::ofstream f(file);f<<"time_s,lateness_ms,target_us,telemetry_us,telemetry_seq\n";
 std::vector<double> intervals;double late=0;
 for(size_t i=0;i<v.size();++i){auto s=v[i];f<<std::fixed<<std::setprecision(6)<<s.t<<","<<s.late*1000<<","<<s.pwm<<","<<s.actual<<","<<s.seq<<"\n";late=std::max(late,s.late);if(i)intervals.push_back((s.t-v[i-1].t)*1000);}
 std::sort(intervals.begin(),intervals.end());
 std::cout<<"SAMPLES="<<v.size()<<" SKIPPED="<<skipped<<" MAX_LATENESS_MS="<<late*1000;
 if(!intervals.empty())std::cout<<" INTERVAL_P50_MS="<<intervals[intervals.size()/2]<<" P99_MS="<<intervals[std::min(intervals.size()-1,size_t(intervals.size()*0.99))]<<" MAX_MS="<<intervals.back();
 std::cout<<" LOG="<<file<<std::endl;
 if(!f)throw std::runtime_error("Log write failed");
}
int main(int argc,char**argv){
 std::string port=DEFAULT_PORT,state="route_recovery.txt",log="route_run.csv";double duration=300;bool dry=false;
 for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--port"&&i+1<argc)port=argv[++i];else if(a=="--state-file"&&i+1<argc)state=argv[++i];else if(a=="--log"&&i+1<argc)log=argv[++i];else if(a=="--duration"&&i+1<argc)duration=std::stod(argv[++i]);else if(a=="--dry-run")dry=true;else return 2;}
 if(!std::isfinite(duration)||duration<2||duration>300)return 2;
 std::cout.setf(std::ios::unitbuf);signal(SIGINT,on_signal);signal(SIGTERM,on_signal);signal(SIGHUP,on_signal);signal(SIGPIPE,SIG_IGN);
 std::vector<Sample> samples;samples.reserve(600);unsigned skipped=0;
 try{
 if(dry){double start=seconds();for(unsigned k=0;k<=unsigned(std::round(duration/0.02));++k){double deadline=start+k*0.02;timespec ts{time_t(deadline),long((deadline-std::floor(deadline))*1e9)};clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&ts,nullptr);double now=seconds();samples.push_back({now-start,std::max(0.0,now-deadline),target(std::min(duration,now-start),duration),-1,0});}report(samples,0,log);return 0;}
 Link l(port);double end=seconds()+6;while(!l.system&&seconds()<end&&!interrupted)l.pump();l.require_idle();
 if(l.parameter("SERVO12_FUNCTION")!=-1||l.parameter("RELAY1_FUNCTION")!=1||l.parameter("RELAY1_PIN")!=53||l.parameter("RELAY1_DEFAULT")!=0)throw std::runtime_error("CH12 lamp configuration mismatch");
 if(!lamp_off(l))throw std::runtime_error("Initial lamp off not confirmed");
 {double ts=0;std::ifstream f("gap_light_state.txt");f>>ts>>lamp.seen;}
 int original=int(l.parameter("SERVO10_FUNCTION"));int mount=int(l.parameter("MNT1_TYPE"));int rate=int(l.parameter("SERVO_RATE"));
 if(l.parameter("SERVO10_MIN")>1200||l.parameter("SERVO10_MAX")<1800)throw std::runtime_error("Configured range excludes test");
 std::cout<<"DOWN_CURVE_TURN center=850..900 amplitude=100 freq=5Hz duration=2s; FRONT_FOLLOW left/right=5Hz center=4Hz\n";
 if(rate!=50)throw std::runtime_error("Expected 50 Hz for this verified schedule");
 if(!l.verify_output(0)||(original!=0&&original!=7)||(original==7&&mount!=0))throw std::runtime_error("CH10 not idle");
 int lock=open(state.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);if(lock<0)throw std::runtime_error("Recovery record exists or cannot create");
 std::string saved="SERVO10_FUNCTION="+std::to_string(original)+"\nSERVO10_PWM=0\nSERVO_RATE="+std::to_string(rate)+"\n";
 bool saved_ok=write(lock,saved.data(),saved.size())==ssize_t(saved.size())&&fsync(lock)==0;close(lock);if(!saved_ok)throw std::runtime_error("Cannot persist recovery record");
 int rc=0;
 try{
 if(original)l.function(0);l.require_idle();l.pwm(0);if(!l.verify_output(0))throw std::runtime_error("Center not confirmed");
 end=seconds()+0.3;while(seconds()<end){l.pump(5);l.require_idle();}
 const unsigned reject_base=l.rejected;double start=seconds(),next_query=start;unsigned tick=0;
 while(true){
 double deadline=start+tick*0.02;
 while(seconds()<deadline){l.pump(std::max(0,std::min(2,int((deadline-seconds())*1000))));l.require_idle();if(l.rejected>reject_base)throw std::runtime_error("Command rejected");if(seconds()-std::max(start,l.ack_time)>0.30)throw std::runtime_error("ACK stalled");}
 l.pump(0);l.require_idle();double now=seconds();
 if(now-deadline>0.06)throw std::runtime_error("Scheduling stalled >60 ms");
 if(now-deadline>=0.02){unsigned current=unsigned((now-start)/0.02);skipped+=current-tick;tick=current;deadline=start+tick*0.02;}
 double elapsed=std::min(duration,now-start);if(elapsed>=duration)break;int value=target(elapsed,duration);
 lamp_update(l);
 l.command(MAV_CMD_DO_SET_SERVO,10,value);
 samples.push_back({now-start,std::max(0.0,now-deadline),value,l.output,l.output_seq});
 if(now>=next_query){l.command(MAV_CMD_REQUEST_MESSAGE,MAVLINK_MSG_ID_SERVO_OUTPUT_RAW);next_query=now+0.1;}
 if(elapsed>=duration)break;
 ++tick;
 }
 if(l.rejected>reject_base)throw std::runtime_error("Command rejected");
 l.pwm(0);if(!l.verify_output(0))throw std::runtime_error("Final center not confirmed");
 end=seconds()+0.2;while(seconds()<end){l.pump(5);l.require_idle();}
 std::cout<<"ROUTE_TIME_LIMIT duration="<<duration<<"s stage="<<route.stage<<" turns_completed="<<route.turns<<" PWM_RATE="<<rate<<"Hz\n";
 }catch(const std::exception&e){std::cerr<<"TEST_STOPPED "<<e.what()<<std::endl;rc=1;}
 bool off=false,restored=false;
 try{if(!lamp_off(l))throw std::runtime_error("Lamp off ACK missing");std::cout<<"LAMP_OFF_CONFIRMED"<<std::endl;}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;rc=1;}
 try{l.pwm(0);off=l.verify_output(0);}catch(const std::exception&e){std::cerr<<"STOP "<<e.what()<<std::endl;}
 try{l.function(original);restored=int(l.parameter("SERVO10_FUNCTION"))==original;}catch(const std::exception&e){std::cerr<<"RESTORE "<<e.what()<<std::endl;}
 std::cout<<"STOP="<<off<<" RESTORE_FUNCTION="<<original<<" confirmed="<<restored<<std::endl;
 if(off&&restored)unlink(state.c_str());else rc=1;
 report(samples,skipped,log);return rc;
 }catch(const std::exception&e){std::cerr<<"ERROR "<<e.what()<<std::endl;return 1;}
}

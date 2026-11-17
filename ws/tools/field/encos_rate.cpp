// Single-thread CAN5/ID1 zero-torque sweep probe. No motion/brake commands.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef S100_RATE_CANHAL
#include <arpa/inet.h>
#include <hobot_can_hal.h>
#include <sched.h>
#include <sys/prctl.h>
#include <cerrno>
#include <cstdlib>
#endif
namespace {
using Clock=std::chrono::steady_clock;
volatile std::sig_atomic_t stopped=0;
[[maybe_unused]] void stop(int) { stopped=1; }
struct Stats {
  uint64_t attempted=0,accepted=0,tx_errors=0,replies=0,rx_errors=0,empty=0,other=0,faults=0,skipped=0;
  double max_rpm=0,max_current=0,rx_call_max_us=0;
  std::vector<double> intervals_us,late_us;
};
[[maybe_unused]] double percentile(std::vector<double> v,double p) {
  if(v.empty()) return 0;
  std::sort(v.begin(),v.end());return v[static_cast<std::size_t>((v.size()-1)*p)];
}
#ifdef S100_RATE_CANHAL
float read_float(const uint8_t* d) {
  uint32_t bits=(uint32_t(d[0])<<24)|(uint32_t(d[1])<<16)|(uint32_t(d[2])<<8)|d[3];
  float x;std::memcpy(&x,&bits,4);return x;
}
class Bus {
 public:
  Bus() { int rc=canInit();if(rc<0) throw std::runtime_error("canInit="+std::to_string(rc)); }
  ~Bus() {canDeInit();}
  int send() {
    canframe f{};f.canid=htonl(1);f.can_channel=5;f.can_type=CANType_Can;f.len=3;
    f.data[0]=0x67; // direct torque = 0.00 Nm, ask for type-3 feedback
    f.count=seq_++;
    f.time_stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    pack_info p{};p.data_num=1;p.length=1;return canSendMsgFrame("can5",&f,&p);
  }
  void receive(Stats& s) {
    canframe f[32]{};pack_info p{};p.length=32;
    const auto begin=Clock::now();int rc=canRecvMsgFrame("can5",f,&p);
    s.rx_call_max_us=std::max(s.rx_call_max_us,std::chrono::duration<double,std::micro>(Clock::now()-begin).count());
    if(rc==-303) {++s.empty;return;}
    if(rc<0 || p.data_num>32) {++s.rx_errors;last_rx_error=rc;return;}
    for(unsigned i=0;i<p.data_num;++i) {
      const auto& r=f[i];
      if(r.can_channel!=5 || r.can_type!=CANType_Can || r.canid!=1 || r.len!=8 || (r.data[0]>>5)!=3) {++s.other;continue;}
      ++s.replies;if(r.data[0]&31) ++s.faults;
      float rpm=read_float(r.data+1);
      if(!std::isfinite(rpm)) {++s.faults;continue;}
      s.max_rpm=std::max(s.max_rpm,std::abs(double(rpm)));
      int16_t current=static_cast<int16_t>((unsigned(r.data[5])<<8)|r.data[6]);
      s.max_current=std::max(s.max_current,std::abs(current/100.0));
    }
  }
  int last_rx_error=0;
 private:uint8_t seq_=0;
};
#endif
}

#ifdef S100_RATE_CANHAL
int run_stage(Bus& bus,int hz,double seconds,int spin_us) {
    Stats baseline;
    auto until=Clock::now()+std::chrono::milliseconds(200);
    while(Clock::now()<until && !stopped) {bus.receive(baseline);std::this_thread::sleep_for(std::chrono::microseconds(100));}
    if(baseline.rx_errors || baseline.replies || baseline.faults) throw std::runtime_error("RX error or unsolicited type3 feedback in baseline; cannot assume 1:1 replies");
    if(baseline.rx_call_max_us>10000) throw std::runtime_error("Receive is not sufficiently nonblocking; check recv_timeout=0");
    Stats s;const auto expected=static_cast<uint64_t>(std::ceil(seconds*hz));
    // Allocate and touch sample storage before entering the timed path.
    s.intervals_us.resize(expected);s.late_us.resize(expected);
    const auto period=std::chrono::nanoseconds(1000000000LL/hz);
    const auto start=Clock::now();const auto end=start+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
    auto due=start;auto previous=start;bool has_previous=false;
    while(Clock::now()<end && !stopped) {
      auto now=Clock::now();
      if(now>=due) {
        // Skip expired schedule slots rather than hide scheduling delays with a catch-up burst.
        if(now-due>=period) {auto n=(now-due)/period;s.skipped+=n;due+=period*n;}
        if(due>=end || s.attempted>=expected) break;
        s.late_us[s.attempted]=std::chrono::duration<double,std::micro>(now-due).count();
        ++s.attempted;int rc=bus.send();
        if(rc<0) ++s.tx_errors;
        else {++s.accepted;if(has_previous)s.intervals_us[s.accepted-2]=std::chrono::duration<double,std::micro>(now-previous).count();previous=now;has_previous=true;}
        due+=period;
      }
      bus.receive(s);
      if(s.faults || s.rx_errors || s.tx_errors) break;
      now=Clock::now();
      const auto wake=due-std::chrono::microseconds(spin_us);
      if(now<wake) std::this_thread::sleep_until(std::min(wake,now+std::chrono::microseconds(50)));
    }
    const auto run_end=Clock::now();
    s.late_us.resize(s.attempted);s.intervals_us.resize(s.accepted ? s.accepted-1 : 0);
    // No further TX during drain: delayed responses remain attributed to this stage.
    until=Clock::now()+std::chrono::milliseconds(500);
    while(Clock::now()<until && !stopped) {bus.receive(s);std::this_thread::sleep_for(std::chrono::microseconds(100));}
    const double elapsed=std::chrono::duration<double>(run_end-start).count();
    const auto missing=s.accepted>s.replies?s.accepted-s.replies:0;
    std::printf("RESULT {\"hz_requested\":%d,\"seconds_requested\":%.3f,\"elapsed_s\":%.6f,\"schedule_slots\":%llu,\"attempted\":%llu,\"accepted\":%llu,\"actual_hz\":%.3f,\"replies\":%llu,\"reply_deficit\":%llu,\"tx_errors\":%llu,\"rx_errors\":%llu,\"last_rx_error\":%d,\"skipped_slots\":%llu,\"interval_p99_us\":%.3f,\"interval_max_us\":%.3f,\"lateness_p99_us\":%.3f,\"rx_call_max_us\":%.3f,\"fault_frames\":%llu,\"max_abs_rpm\":%.4f,\"max_abs_current_A\":%.3f,\"baseline_replies\":%llu,\"interrupted\":%d}\n",
       hz,seconds,elapsed,(unsigned long long)expected,(unsigned long long)s.attempted,(unsigned long long)s.accepted,s.accepted/elapsed,
       (unsigned long long)s.replies,(unsigned long long)missing,(unsigned long long)s.tx_errors,(unsigned long long)s.rx_errors,bus.last_rx_error,
       (unsigned long long)s.skipped,percentile(s.intervals_us,.99),percentile(s.intervals_us,1),percentile(s.late_us,.99),s.rx_call_max_us,
       (unsigned long long)s.faults,s.max_rpm,s.max_current,(unsigned long long)baseline.replies,static_cast<int>(stopped));
    return s.tx_errors || s.rx_errors || s.faults || stopped ? 1:0;
}
#endif
#ifdef S100_RATE_CANHAL
void configure_timing(const std::string& profile,int cpu) {
  const unsigned long slack=profile=="baseline" ? 50000UL : 1UL;
  if(prctl(PR_SET_TIMERSLACK,slack,0,0,0)!=0) throw std::runtime_error("Set timer slack failed: "+std::string(std::strerror(errno)));
  if(profile=="rt") {
    cpu_set_t set;CPU_ZERO(&set);CPU_SET(cpu,&set);
    if(sched_setaffinity(0,sizeof(set),&set)!=0) throw std::runtime_error("CPU affinity failed: "+std::string(std::strerror(errno)));
  }
  sched_param param{};param.sched_priority=profile=="rt" ? 40 : 0;
  if(sched_setscheduler(0,profile=="rt" ? SCHED_FIFO:SCHED_OTHER,&param)!=0) throw std::runtime_error("Scheduler setup failed: "+std::string(std::strerror(errno)));
  if(sched_getparam(0,&param)!=0) throw std::runtime_error("Read scheduler failed");
  std::printf("CONFIG {\"profile\":\"%s\",\"timer_slack_ns\":%d,\"policy\":%d,\"priority\":%d,\"cpu\":%d,\"spin_us\":%d,\"sdk_log_level\":\"%s\"}\n",
      profile.c_str(),prctl(PR_GET_TIMERSLACK,0,0,0,0),sched_getscheduler(0),param.sched_priority,profile=="rt"?cpu:-1,profile=="rt"?20:0,std::getenv("CAN_HAL_DEBUG_LEVEL")?std::getenv("CAN_HAL_DEBUG_LEVEL"):"unset");
}
#endif
int main(int argc,char**argv) {
  setvbuf(stdout,nullptr,_IOLBF,0);
  int hz=50,cpu=5;double seconds=5;bool hardware=false,sweep=false;
  std::string profile="rt";
  try {
    for(int i=1;i<argc;++i) {
      std::string a=argv[i];
      if(a=="--hardware") hardware=true;
      else if(a=="--sweep") sweep=true;
      else if(a=="--profile" && i+1<argc) {
        profile=argv[++i];if(profile!="baseline" && profile!="tuned" && profile!="rt") throw std::invalid_argument("Use baseline, tuned or rt profile");
      }
      else if(a=="--cpu" && i+1<argc) {std::string v=argv[++i];std::size_t n;cpu=std::stoi(v,&n);if(n!=v.size() || cpu<0 || cpu>=1024) throw std::invalid_argument("Invalid CPU index");}
      else if(a=="--hz" && i+1<argc) {std::string v=argv[++i];std::size_t n;hz=std::stoi(v,&n);if(n!=v.size() || hz<1 || hz>2000) throw std::invalid_argument("rate probe supports 1..2000 Hz");}
      else if(a=="--seconds" && i+1<argc) {std::string v=argv[++i];std::size_t n;seconds=std::stod(v,&n);if(n!=v.size() || !std::isfinite(seconds) || seconds<=0 || seconds>30) throw std::invalid_argument("probe duration must be >0 and <=30s");}
      else throw std::invalid_argument("Usage: encos_rate [--hz N] [--seconds S] [--sweep] [--profile baseline|tuned|rt] [--cpu N] [--hardware]");
    }
    if(!hardware) {std::printf("OFFLINE: CAN5 ID1 classic CAN TX=67 00 00, type3 reply requested, hz=%d seconds=%.3f sweep=%d profile=%s cpu=%d; no CANHAL init\n",hz,seconds,static_cast<int>(sweep),profile.c_str(),cpu);return 0;}
#ifdef S100_RATE_CANHAL
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    Bus bus;
    configure_timing(profile,cpu);
    const int spin_us=profile=="rt" ? 20 : 0;
    if(sweep) {
      for(int rate:{50,100,250,500,1000,2000}) {
        int rc=run_stage(bus,rate,seconds,spin_us);
        if(rc) return rc;
      }
      return 0;
    }
    return run_stage(bus,hz,seconds,spin_us);
#else
    throw std::runtime_error("Offline build; no hardware backend");
#endif
  }catch(const std::exception& e) {std::fprintf(stderr,"ERROR: %s\n",e.what());return 1;}
}

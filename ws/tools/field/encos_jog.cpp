// ENCOS V1.16 9.1.4, 9.3, 10.3: bounded direct torque test, CAN5 / motor 1.
#include "encos_id_protocol.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef S100_ID_CANHAL
#include <arpa/inet.h>
#include <hobot_can_hal.h>
#endif
namespace {
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t interrupted = 0;
[[maybe_unused]] void on_signal(int) { interrupted = 1; }
encos_id::Frame query(unsigned code) { return {1,2,{0xe1,static_cast<uint8_t>(code)}}; }
encos_id::Frame torque(double nm) {
  const auto value=static_cast<int16_t>(std::lround(nm*100));
  return {1,3,{0x67,static_cast<uint8_t>(value>>8),static_cast<uint8_t>(value)}};
}
void print(const char* kind, const encos_id::Frame& f) {
  std::printf("%s CAN5 id=0x%03X len=%u data=",kind,f.id,f.length);
  for (unsigned i=0;i<f.length;++i) std::printf("%02X ",f.data[i]);
  std::puts("");
}
[[maybe_unused]] float decode_float(const uint8_t* p) {
  uint32_t bits=(uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
  float value; std::memcpy(&value,&bits,sizeof(value)); return value;
}
#ifdef S100_ID_CANHAL
class Bus {
 public:
  Bus() { int rc=canInit(); if(rc<0) throw std::runtime_error("canInit failed: "+std::to_string(rc)); }
  ~Bus() { canDeInit(); }
  void send(const encos_id::Frame& f) {
    canframe tx{}; pack_info p{}; p.data_num=1; p.length=1;
    tx.time_stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    tx.canid=htonl(f.id);tx.can_type=CANType_Can;tx.can_channel=5;tx.len=f.length;tx.count=sequence_++;
    std::copy_n(f.data.begin(),f.length,tx.data); print("TX",f);
    int rc=canSendMsgFrame("can5",&tx,&p);
    std::printf("CANHAL submit ret=%d\n",rc);
    if(rc<0) throw std::runtime_error("Send failed: "+std::to_string(rc));
  }
  template<class Callback> void receive_for(std::chrono::milliseconds duration, Callback callback) {
    auto end=Clock::now()+duration;
    while(Clock::now()<end && !interrupted) {
      canframe frames[32]{};pack_info p{};p.length=32;
      int rc=canRecvMsgFrame("can5",frames,&p);
      if(rc==-303) continue;
      if(rc<0 || p.data_num>32) throw std::runtime_error("Receive failed: "+std::to_string(rc));
      for(unsigned i=0;i<p.data_num;++i) {
        auto& r=frames[i];
        if(r.can_channel!=5 || r.can_type!=CANType_Can || r.canid!=1 || r.len>8) continue;
        encos_id::Frame f{1,r.len,{}};std::copy_n(r.data,r.len,f.data.begin());print("RX",f);callback(f);
      }
    }
  }
 private: uint8_t sequence_{0};
};
struct Status {
  int brake=-1;
  bool speed_seen=false;
  float rpm=0;
  unsigned control_feedback=0;
  int timeout_ms=-1;
  double torque_constant=0;
  int fault=0;
  void update(const encos_id::Frame& f) {
    if(!f.length) return;
    unsigned type=f.data[0]>>5;
    fault=fault ? fault : f.data[0]&0x1f;
    if(type==5 && f.length>=3 && f.data[1]==37 && f.data[2]<=1) brake=f.data[2];
    if(type==5 && f.length==4 && f.data[1]==31) timeout_ms=(unsigned(f.data[2])<<8)|f.data[3];
    if(type==5 && f.length==4 && f.data[1]==22) torque_constant=((unsigned(f.data[2])<<8)|f.data[3])/100.0;
    if(type==6 && f.length==2 && f.data[1]<=1) brake=f.data[1];
    if(type==5 && f.length==6 && f.data[1]==2) { rpm=decode_float(&f.data[2]);speed_seen=std::isfinite(rpm); }
    if(type==3 && f.length==8) {
      rpm=decode_float(&f.data[1]);speed_seen=std::isfinite(rpm);++control_feedback;
      int16_t current=static_cast<int16_t>((unsigned(f.data[5])<<8)|f.data[6]);
      std::printf("FEEDBACK rpm=%.4f phase_current=%.3fA fault=%d\n",rpm,current/100.0,fault);
    }
  }
};
#endif
}
int main(int argc,char**argv) {
  setvbuf(stdout,nullptr,_IOLBF,0);
  bool hardware=false, status_only=false, release=false, zero_only=false;
  std::string model;
  double seconds=0.2;
  double torque_nm=0.5;
  try {
    for(int i=1;i<argc;++i) {
      std::string a=argv[i];
      if(a=="--hardware") hardware=true;
      else if(a=="--status") status_only=true;
      else if(a=="--zero-only") zero_only=true;
      else if(a=="--release-brake") release=true;
      else if(a=="--torque" && i+1<argc) {
        std::string value=argv[++i];std::size_t used=0;torque_nm=std::stod(value,&used);
        if(used!=value.size() || !std::isfinite(torque_nm) || torque_nm < -327.68 || torque_nm > 327.67)
          throw std::invalid_argument("--torque must fit ENCOS int16 at 0.01 Nm resolution (-327.68..327.67)");
      }
      else if(a=="--seconds" && i+1<argc) {
        std::string value=argv[++i];std::size_t used=0;seconds=std::stod(value,&used);
        if(used!=value.size() || !std::isfinite(seconds) || seconds<=0)
          throw std::invalid_argument("--seconds must be finite and >0");
      }
      else if(a=="--model" && i+1<argc) {
        model=argv[++i];std::replace(model.begin(),model.end(),'_','-');
        if(model!="EC-A6408-P2-25") throw std::invalid_argument("This jog model option supports EC-A6408-P2-25 only");
      }
      else if(a=="--help") {
        std::puts("Usage: encos_jog [--status | --zero-only] [--seconds S] [--torque Nm] [--hardware] [--model EC-A6408-P2-25 | --release-brake]\nDefault offline preview; fixed CAN5, ID1; torque default +0.5Nm, --torque follows protocol int16 range; duration default 0.2s, --seconds any positive finite value; then zero torque.\n--status only queries brake, speed, torque coefficient and timeout; no motion.\n--release-brake explicitly permits release before jog; it remains released afterward.");return 0;
      } else throw std::invalid_argument("Unknown argument: "+a);
    }
    if(status_only && zero_only) throw std::invalid_argument("Choose status or zero-only");
    if(zero_only && release) throw std::invalid_argument("Zero-only does not release brake");
    if(!model.empty() && release) throw std::invalid_argument("Model mode omits brake operations; do not combine with --release-brake");
    if(!model.empty()) std::printf("MODEL %s; no brake operations per operator instruction\n",model.c_str());
    if(!status_only && !zero_only) std::printf("REQUEST torque=%+.3fNm duration_seconds=%.3f\n",torque_nm,seconds);
    if(!hardware) {
      std::puts("OFFLINE PREVIEW: no CANHAL initialization or physical frames");
      if(model.empty()) print("BRAKE QUERY",query(37));
      print("SPEED QUERY",query(2));
      print("TORQUE COEFFICIENT QUERY",query(22));print("TIMEOUT QUERY",query(31));
      if(!status_only) { if(release) print("RELEASE",{1,3,{0x75,0,1}});if(!zero_only) print("TORQUE",torque(torque_nm));print("ZERO TORQUE",torque(0)); }
      return 0;
    }
#ifdef S100_ID_CANHAL
    std::signal(SIGINT,on_signal);std::signal(SIGTERM,on_signal);
    Bus bus;Status state;
    for(unsigned code:{37U,2U,22U,31U}) {
      if(code==37 && !model.empty()) continue;
      bus.send(query(code));bus.receive_for(std::chrono::milliseconds(150),[&](const auto&f){state.update(f);});
      if(interrupted) throw std::runtime_error("Interrupted before torque");
    }
    std::printf("STATUS brake=%d speed_seen=%d rpm=%.4f fault=%d\n",state.brake,state.speed_seen,state.rpm,state.fault);
    std::printf("PARAMETERS torque_constant=%.3fNm/A timeout_ms=%d\n",state.torque_constant,state.timeout_ms);
    if(status_only) return 0;
    if(!zero_only && (state.fault || !state.speed_seen)) throw std::runtime_error("Need fault-free valid speed feedback before torque");
    if(!zero_only && model.empty() && state.brake!=1) {
      if(!release) throw std::runtime_error("Brake not confirmed released; no torque sent (explicit --release-brake required)");
      bus.send({1,3,{0x75,0,1}});
      bus.receive_for(std::chrono::milliseconds(300),[&](const auto&f){state.update(f);});
      bus.send(query(37));bus.receive_for(std::chrono::milliseconds(150),[&](const auto&f){state.update(f);});
      if(state.brake!=1 || state.fault || interrupted) throw std::runtime_error("Brake release not confirmed; no torque sent");
    }
    bool started=false;
    unsigned positive_packets=0;
    const auto torque_start=Clock::now();
    auto withdraw=[&]() {
      bool submitted=false;
      for(int i=0;i<3;++i) {
        try { bus.send(torque(0));submitted=true; }
        catch(const std::exception& z) { std::fprintf(stderr,"ZERO failed: %s\n",z.what()); }
        // Drain each response promptly: CANHAL rejects queued frames older than 28ms.
        try { bus.receive_for(std::chrono::milliseconds(20),[&](const auto&f){state.update(f);}); }
        catch(const std::exception& z) { std::fprintf(stderr,"ZERO feedback failed: %s\n",z.what()); }
      }
      return submitted;
    };
    try {
      // Compare elapsed seconds without converting an arbitrarily long request to integer clock ticks.
      while(!zero_only && std::chrono::duration<double>(Clock::now()-torque_start).count()<seconds && !interrupted) {
        const auto feedback_before=state.control_feedback;
        started=true;bus.send(torque(torque_nm));++positive_packets;
        bus.receive_for(std::chrono::milliseconds(20),[&](const auto&f){state.update(f);});
        if(state.fault) throw std::runtime_error("Motor reported fault; withdraw torque");
        if(state.control_feedback==feedback_before || !state.speed_seen) throw std::runtime_error("No fresh valid control feedback; withdraw torque");
      }
    } catch(const std::exception& e) {
      std::fprintf(stderr,"Jog ended early: %s\n",e.what());
      // Always attempt to withdraw commanded torque, including after a send/RX error.
      withdraw();
      throw;
    }
    const auto torque_window_ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-torque_start).count();
    if(!withdraw()) throw std::runtime_error("All zero torque submissions failed; rely on verified motor timeout");
    std::printf("TORQUE_WINDOW positive_packets=%u elapsed_ms=%lld\n",positive_packets,static_cast<long long>(torque_window_ms));
    bus.receive_for(std::chrono::milliseconds(500),[&](const auto&f){state.update(f);});
    state.speed_seen=false;
    bus.send(query(2));bus.receive_for(std::chrono::milliseconds(150),[&](const auto&f){state.update(f);});
    std::printf("DONE started=%d control_feedback=%u final_rpm=%.4f fault=%d; zero torque submitted; no brake closing command sent\n",started,state.control_feedback,state.rpm,state.fault);
    return state.fault || interrupted || !state.speed_seen || (!zero_only && !state.control_feedback) ? 1 : 0;
#else
    throw std::runtime_error("Offline build has no CANHAL backend");
#endif
  } catch(const std::exception& e) { std::fprintf(stderr,"ERROR: %s\n",e.what());return 1; }
}

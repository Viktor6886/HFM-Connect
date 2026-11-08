// DiagEngine.h - FreeRTOS task that exclusively owns the K-Line.
// HTTP handlers never touch the link: they submit jobs and read state
// snapshots under the engine mutex. Arduino only.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>

#include "HfmLive.h"
#include "HfmServices.h"
#include "ImpulseDecoder.h"
#include "ServiceGuard.h"
#include "Settings.h"

class KLineUart;
class SimEcu;

enum class Job : uint8_t {
  None, Connect, Disconnect, Identify, ReadDtc, ReadAdapt, ReadCoding, ReadVin,
  LiveStart, LiveStop, SvcPrepare, SvcExecute, PurgeOn, PurgeOff, Stop, ImpulseRead, AckFault, ApplySource
};
const char* jobCode(Job j);

enum class JobState : uint8_t { Idle, Queued, Running, Done, Failed };
const char* jobStateCode(JobState s);

struct JobInfo {
  uint32_t id = 0;
  Job job = Job::None;
  JobState state = JobState::Idle;
  char message[96] = {0};
  uint32_t startedMs = 0, finishedMs = 0;
};

struct DtcView {
  bool valid = false;
  svc::FaultMemory fm;
  dtc::Decoded items[48];
  uint16_t count = 0;
  uint32_t tMs = 0;
};

struct ImpulseView {
  bool valid = false;
  ImpulseResult result;
  ImpulseEdge edges[96];
  uint16_t edgeCount = 0;
  uint8_t codes[16];
  uint8_t codeCount = 0;
  uint32_t tMs = 0;
};

struct EngineState {
  // link/session
  SessionState session = SessionState::Disconnected;
  LinkStatus lastError = LinkStatus::Ok;
  uint32_t connectedAtMs = 0;
  uint32_t txCount = 0, errCount = 0;
  bool simulator = false;
  // identification/profile
  EcuIdent ident;
  prof::EcuProfile profile;
  // results
  DtcView dtc;
  svc::Adaptations adapt;
  svc::Coding coding;
  svc::Vin vin;
  bool liveActive = false;
  live::Snapshot snap;
  live::Decoded live;
  uint32_t liveCycles = 0;
  // services
  guard::Plan plan;
  bool purgeOn = false;
  uint32_t purgeOnSinceMs = 0;
  uint32_t purgeActiveSinceMs = 0;
  bool purgeSonde = false;
  char lastServiceResult[160] = {0};
  // impulse
  ImpulseView impulse;
  // hardware
  bool interlock = false;
  float batteryV = -1;
  uint32_t parityErrors = 0, frameErrors = 0;
  // recording
  bool recording = false;
  char recordFile[40] = {0};
  uint32_t recordRows = 0;
  JobInfo job;
};

class DiagEngine {
 public:
  void begin(Settings* settings);

  // Submit a job; returns job id (0 if another job is queued/running).
  uint32_t submit(Job j, uint32_t arg = 0, const char* text = nullptr);

  void lock();
  void unlock();
  EngineState& state() { return st_; }  // access only between lock()/unlock()
  TraceLog& trace() { return trace_; }

  bool startRecording();
  void stopRecording();
  void reresolveProfile();  // after settings change (experimental / engineer mode)
  const char* sessionId() const { return sessionId_; }
  const char* transportName();

  static void taskEntry(void* self);

 private:
  void run();
  void execute(Job j, uint32_t arg, const char* text);
  bool ensureConnected(char* msg, size_t n);
  void doConnect(char* msg, size_t n, bool& ok);
  void doIdentify(char* msg, size_t n, bool& ok);
  void livePoll();
  void keepAlive();
  void purgeWatchdog();
  void fillFacts(guard::Facts& f);
  void finishJob(bool ok, const char* msg);
  void readHardware();
  void recordRow();
  void selectLink();
  bool can(uint8_t feature, char* msg, size_t n);

  Settings* settings_ = nullptr;
  SemaphoreHandle_t mtx_ = nullptr;
  TraceLog trace_;
  ByteLink* link_ = nullptr;
  KLineUart* kline_ = nullptr;
  SimEcu* sim_ = nullptr;
  HfmSession* session_ = nullptr;
  guard::ServiceGuard guard_;
  EngineState st_;
  volatile Job pending_ = Job::None;
  uint32_t pendingArg_ = 0;
  char pendingText_[64] = {0};
  uint32_t nextJobId_ = 1;
  uint32_t lastKeepAliveMs_ = 0;
  uint32_t lastLiveMs_ = 0;
  uint32_t lastFlushMs_ = 0;
  char sessionId_[20] = {0};
  std::string recBuf_;
  live::Decoded prevLive_;
};

extern DiagEngine Engine;
#endif

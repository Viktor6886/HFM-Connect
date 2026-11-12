// DiagEngine.h - FreeRTOS task that exclusively owns the K-Line.
// HTTP handlers never touch the link: they submit jobs and read state
// snapshots under the engine mutex. Arduino only.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>

#include "DtcRefresh.h"
#include "HfmLive.h"
#include "HfmServices.h"
#include "ImpulseDecoder.h"
#include "KLineWake.h"
#include "ServiceGuard.h"
#include "Settings.h"

class KLineUart;
class FtdiLink;

enum class Job : uint8_t {
  None, Connect, Disconnect, Identify, ReadDtc, ReadAdapt, ReadCoding, ReadVin,
  LiveStart, LiveStop, SvcPrepare, SvcExecute, PurgeOn, PurgeOff, Stop, ImpulseRead, AckFault
};
const char* jobCode(Job j);

enum class JobState : uint8_t { Idle, Queued, Running, Done, Failed };
const char* jobStateCode(JobState s);

struct JobInfo {
  uint32_t id = 0;
  Job job = Job::None;
  JobState state = JobState::Idle;
  char message[256] = {0};
  uint32_t startedMs = 0, finishedMs = 0;
};

// Single state of the diagnostic link shown by the UI (D-027).
enum class LinkPhase : uint8_t { NoAdapter, Idle, Paused, Connecting, Recovering, Ready, Error };
const char* linkPhaseCode(LinkPhase p);

// Runtime result of a feature (capability matrix: what really worked).
struct FeatRun {
  uint8_t state = 0;  // 0 not run, 1 worked, 2 failed, 3 not supported by this ECU (D-034)
  uint32_t tMs = 0;
};

struct DtcView {
  bool valid = false;
  svc::FaultMemory fm;
  dtc::Decoded items[48];
  uint16_t count = 0;
  dtc::BankMapping inferred = dtc::BankMapping::Unknown;  // OEM intermittent rule
  uint32_t tMs = 0;
  bool automatic = false;  // read by the engine itself (connection / link recovery), not by a job
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

struct AdapterInfo {
  bool hostReady = false;
  bool present = false;
  bool configured = false;
  uint16_t vid = 0, pid = 0;
  const char* chip = "нет";
  uint32_t attachCount = 0, usbErrors = 0, breaks = 0, overruns = 0;
  // USB diagnostics while no adapter is attached.
  int usbDevices = -1;          // enumerated USB devices on the OTG port (-1 = unknown)
  uint32_t deviceEvents = 0;    // NEW_DEV events since boot
  uint16_t seenVid = 0, seenPid = 0;
  char note[160] = {0};         // why no adapter is attached ("" when attached)
  bool phyOtg = false, portPowered = false, portConnected = false;
  uint8_t lineState = 0;        // D+ (bit0), D- (bit1)
  // K-Line wake-up: last attempt and the profile used next.
  bool haveWake = false;
  kwake::Diag wake;
  kwake::Profile wakeNext;
  bool wakeLocked = false;
  uint32_t wakeAttempts = 0;
};

struct EngineState {
  // link/session
  LinkPhase link = LinkPhase::Idle;
  char linkMsg[200] = {0};
  uint32_t recoverAttempts = 0;  // wake-ups since the link was lost
  uint32_t recoveries = 0;       // automatic reconnections after a loss
  uint32_t resyncs = 0;          // sessions resumed without a new BREAK
  bool faultStop = false;        // ServiceGuard FAULT_STOP (indication)
  bool serviceActive = false;    // a state-changing service operation is running
  SessionState session = SessionState::Disconnected;
  LinkStatus lastError = LinkStatus::Ok;
  uint32_t connectedAtMs = 0;
  uint32_t txCount = 0, errCount = 0;
  // identification/profile
  EcuIdent ident;
  prof::EcuProfile profile;
  // results
  DtcView dtc;
  svc::Adaptations adapt;
  svc::Coding coding;
  svc::Vin vin;
  bool liveActive = false;      // polling wanted by the user (kept while the link recovers)
  live::Snapshot snap;          // last poll cycle (may be partial)
  live::Decoded live;           // last COMPLETE cycle (kept when a cycle fails)
  uint32_t liveCycles = 0;
  bool livePlanVdo = false;     // address plan in use
  FeatRun feat[prof::F_COUNT];
  // services
  guard::Plan plan;
  bool purgeOn = false;
  uint32_t purgeOnSinceMs = 0;
  uint32_t purgeActiveSinceMs = 0;
  bool purgeSonde = false;
  char lastServiceResult[320] = {0};  // Russian text: 2 bytes per letter
  uint32_t pwLockS = 0;          // seconds left of the password lock (D-035)
  // impulse
  ImpulseView impulse;
  // hardware
  bool interlock = false;
  float batteryV = -1;
  uint32_t parityErrors = 0, frameErrors = 0;
  AdapterInfo adapter;
  char autoMsg[256] = {0};
  uint32_t autoAttempts = 0;
  // recording
  bool recording = false;
  char recordFile[40] = {0};
  uint32_t recordRows = 0;
  JobInfo job;
};

class DiagEngine {
 public:
  void begin(Settings* settings);

  // Queue a job; returns its id (0 = queue full). Connect and repeated
  // identical jobs are merged with the one already waiting; STOP and
  // LIVE_STOP go first.
  uint32_t submit(Job j, uint32_t arg = 0, const char* text = nullptr);
  // Copy of a recent job by id (false if unknown/too old). Call under lock().
  bool jobInfo(uint32_t id, JobInfo& out);
  uint8_t queued() const { return qCount_; }

  void lock();
  void unlock();
  EngineState& state() { return st_; }  // access only between lock()/unlock()
  TraceLog& trace() { return trace_; }

  bool startRecording();
  void stopRecording();
  void reresolveProfile();  // after settings change (experimental / engineer mode)
  const char* sessionId() const { return sessionId_; }
  const char* transportName();
  FtdiLink* usbLink() { return ftdi_; }  // null for the UART transport

  static void taskEntry(void* self);

 private:
  void run();
  void execute(Job j, uint32_t arg, const char* text);
  bool ensureConnected(char* msg, size_t n);
  void doConnect(char* msg, size_t n, bool& ok, uint8_t attempts = 0);
  void doIdentify(char* msg, size_t n, bool& ok);
  bool readDtc(char* msg, size_t n, bool automatic = false);
  bool autoReadDtc(bool byUser = false);
  bool recoverRead();
  void afterResolve();
  void livePoll();
  void keepAlive();
  void maintainLink();
  void updateLink();
  void persistLearned();
  void featMark(uint8_t f, bool ok);
  void featUnsupported(uint8_t f);
  bool impulseCapture(char* msg, size_t n);
  bool impulseVehicleOk(char* msg, size_t n);
  JobInfo* ringFind(uint32_t id);
  uint8_t vehicleCylinders();
  bool adapterPresent();
  void adapterMissing(char* msg, size_t n);
  void purgeWatchdog();
  void fillFacts(guard::Facts& f);
  void finishJob(bool ok, const char* msg);
  void readHardware();
  void recordRow();
  void applySettings();
  bool can(uint8_t feature, char* msg, size_t n);

  Settings* settings_ = nullptr;
  SemaphoreHandle_t mtx_ = nullptr;
  TraceLog trace_;
  ByteLink* link_ = nullptr;
  KLineUart* kline_ = nullptr;
  FtdiLink* ftdi_ = nullptr;
  bool userDisconnected_ = false;
  bool everConnected_ = false;   // a session existed since boot / adapter attach
  bool connecting_ = false;      // wake-up in progress
  uint32_t lastAutoMs_ = 0;
  uint32_t autoBackoffMs_ = 2000;
  HfmSession* session_ = nullptr;
  guard::ServiceGuard guard_;
  EngineState st_;
  struct QJob {
    Job job;
    uint32_t arg, id;
    char text[64];
  };
  static const uint8_t kQueue = 6, kRing = 10;
  QJob q_[kQueue];
  uint8_t qCount_ = 0;
  JobInfo ring_[kRing];
  uint8_t ringNext_ = 0;
  uint32_t nextJobId_ = 1;
  uint32_t lastPersistMs_ = 0;
  uint32_t lastKeepAliveMs_ = 0;
  uint32_t lastLiveMs_ = 0;
  uint32_t lastFlushMs_ = 0;
  char sessionId_[20] = {0};
  std::string recBuf_;
  live::Decoded prevLive_;
  uint8_t extFails_ = 0;
  bool extDisabled_ = false;
  uint8_t planTried_ = 0;        // live plans answered with addresses: 1 Bosch, 2 VDO
  bool svcConfirmStopped_ = false;  // user confirmed "engine stopped" in the service dialog
  DtcRefresh dtcRefresh_;  // engine task only
};

extern DiagEngine Engine;
#endif

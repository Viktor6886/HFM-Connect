// HfmTypes.h - shared enums and small types for the HFM-SFI diagnostic core.
// Platform independent: no Arduino headers (compiled by host tests too).
#pragma once
#include <stddef.h>
#include <stdint.h>

// Evidence / confirmation level of a fact (REV 1.4 section 2).
//  ZAV   - stated by a Mercedes-Benz factory document
//  REK   - reconstructed protocol data (REV 1.1 specification), not factory data
//  GIP   - plausible hypothesis, ambiguous
//  VAL   - full trace + repeatable test on the given HW+SW+diag index
//  BLOCK - no verification, state-changing risk -> rejected by firmware
enum class Confidence : uint8_t { ZAV, REK, GIP, VAL, BLOCK };

inline const char* confidenceCode(Confidence c) {
  switch (c) {
    case Confidence::ZAV: return "ZAV";
    case Confidence::REK: return "REK";
    case Confidence::GIP: return "GIP";
    case Confidence::VAL: return "VAL";
    default: return "BLOCK";
  }
}

// Implementation status of a diagnostic function (CLAUDE.md status ladder).
enum class FnStatus : uint8_t {
  Planned,         // запланирована
  InProgress,      // разрабатывается
  Implemented,     // реализована
  SoftwareTested,  // протестирована программно (host tests / simulator)
  EcuVerified      // проверена на реальном ЭБУ
};

inline const char* fnStatusCode(FnStatus s) {
  switch (s) {
    case FnStatus::Planned: return "planned";
    case FnStatus::InProgress: return "in_progress";
    case FnStatus::Implemented: return "implemented";
    case FnStatus::SoftwareTested: return "sw_tested";
    default: return "ecu_verified";
  }
}

// UART framing of a single byte on the HFM K-Line.
//  None  - 8N1: prefix/opcode bytes (stop bit is read by the ECU as 9th bit = 1)
//  Space - 8S1: selector/argument bytes (9th bit = 0)
enum class Parity : uint8_t { None, Space };

// Result of one link-level operation.
enum class LinkStatus : uint8_t {
  Ok,
  Timeout,        // fewer bytes than expected within the deadline
  EchoMismatch,   // first received byte != transmitted byte
  NoSync,         // AA 55 synchronisation not observed after BREAK
  LinkDown,       // transport not open / hardware fault
  BadAck,         // prefix answered with something other than prefix ^ 0xFF
  BadArgReply,    // write argument answered with an unexpected value
  NotAllowed,     // command policy rejected the byte before transmission
  NotConnected    // session not in Ready state
};

inline const char* linkStatusCode(LinkStatus s) {
  switch (s) {
    case LinkStatus::Ok: return "OK";
    case LinkStatus::Timeout: return "TIMEOUT";
    case LinkStatus::EchoMismatch: return "ECHO_MISMATCH";
    case LinkStatus::NoSync: return "NO_SYNC";
    case LinkStatus::LinkDown: return "LINK_DOWN";
    case LinkStatus::BadAck: return "BAD_ACK";
    case LinkStatus::BadArgReply: return "BAD_ARG_REPLY";
    case LinkStatus::NotAllowed: return "NOT_ALLOWED";
    default: return "NOT_CONNECTED";
  }
}

// Phase of a traced byte (REV 1.4 section 20, JSONL field "phase").
enum class TracePhase : uint8_t { Break, Sync, Prefix, Selector, Argument, Info };

inline const char* tracePhaseCode(TracePhase p) {
  switch (p) {
    case TracePhase::Break: return "BREAK";
    case TracePhase::Sync: return "SYNC";
    case TracePhase::Prefix: return "PREFIX";
    case TracePhase::Selector: return "SELECTOR";
    case TracePhase::Argument: return "ARG";
    default: return "INFO";
  }
}

// Risk class of a command (REV 1.4 section 20 "command_risk").
enum class CommandRisk : uint8_t { Read, Service, Write };

inline const char* commandRiskCode(CommandRisk r) {
  switch (r) {
    case CommandRisk::Read: return "read";
    case CommandRisk::Service: return "service";
    default: return "write";
  }
}

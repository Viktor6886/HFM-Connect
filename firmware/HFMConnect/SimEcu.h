// SimEcu.h - software model of an HFM-SFI ECU implementing ByteLink.
//
// SYNTHETIC DATA ONLY. Behaviour follows the HFMreader reference simulator
// (complement ACKs, echo, page-scoped selectors). Identification bytes are
// HFMreader ecu_identification_simulator.GOLDEN_RAW - not a vehicle capture.
// Used by host tests and by the on-device "Simulator" source (WebUI banner).
#pragma once
#include "ByteLink.h"

class SimEcu : public ByteLink {
 public:
  enum class Scenario : uint8_t { WarmIdle, EngineOff, ColdStart, Faults };

  // Fault injection for tests (one-shot, counted in exchanged bytes).
  struct Inject {
    int32_t echoMismatchAt = -1;
    int32_t timeoutAt = -1;
    int32_t badAckAt = -1;
    bool noSync = false;
  };

  SimEcu();

  void setScenario(Scenario s);
  Scenario scenario() const { return scenario_; }
  void setInject(const Inject& i) { inject_ = i; }
  void setIdent(const uint8_t* raw28);
  void setFaultByte(uint8_t index, uint8_t value);
  uint8_t faultByte(uint8_t index) const { return dtc_[index & 0x3F]; }
  void setPageByte(uint8_t page, uint8_t sel, uint8_t value);  // override
  uint32_t exchanges() const { return count_; }
  uint8_t lastWritePrefix() const { return lastWritePrefix_; }
  bool purgeOn() const { return purgeOn_; }
  bool adaptationsReset() const { return adaptReset_; }

  // ByteLink
  bool open() override { open_ = true; return true; }
  void close() override { open_ = false; initialized_ = false; }
  bool isOpen() const override { return open_; }
  LinkStatus wakeup(const LinkTiming& t, uint8_t& echo, uint8_t& sync) override;
  LinkStatus exchange(uint8_t tx, Parity p, uint16_t deadlineMs, uint8_t& echo, uint8_t& resp) override;
  void sleepMs(uint32_t ms) override { clockUs_ += (uint64_t)ms * 1000; }
  uint32_t nowMs() override { return (uint32_t)(clockUs_ / 1000); }
  uint32_t nowUs() override { return (uint32_t)clockUs_; }
  const char* name() const override { return "sim"; }

 protected:
  // Engine model sample for the current simulated time.
  uint8_t pageValue(uint8_t page, uint8_t sel);
  virtual uint32_t modelMs() { return nowMs(); }

  uint64_t clockUs_ = 0;

 private:
  Scenario scenario_ = Scenario::WarmIdle;
  Inject inject_;
  bool open_ = false;
  bool initialized_ = false;
  uint8_t page_ = 0;
  bool pageSet_ = false;
  uint8_t ident_[28];
  uint8_t dtc_[64];
  uint8_t ovPage_[8] = {0}, ovSel_[8] = {0}, ovVal_[8] = {0};
  uint8_t ovN_ = 0;
  uint32_t count_ = 0;
  uint32_t startMs_ = 0;
  uint8_t lastWritePrefix_ = 0;
  uint8_t controlStage_ = 0;
  bool purgeOn_ = false;
  bool adaptReset_ = false;
  uint8_t adapt_[3] = {131, 133, 126};
};

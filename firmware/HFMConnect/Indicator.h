// Indicator.h - sound (passive piezo) and status light (external RGB LED,
// on-board RGB LED mirrored). Arduino only.
//
// A low-priority task follows the diagnostic engine (snapshot under
// Engine.lock() every 20 ms): link-state transitions and new fault-memory
// readouts become short melodies, the current state becomes a light scene.
// Nothing here talks to the ECU. Wiring and scenes: docs/HARDWARE.md.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>

enum class Sound : uint8_t {
  None,
  Boot,        // device started
  Connect,     // ECU on line (first time / after "Connect")
  Recovered,   // link restored automatically
  Disconnect,  // "Disconnect" pressed
  LinkLost,    // ECU stopped answering
  AdapterIn,   // USB K-Line adapter attached
  AdapterOut,  // adapter removed
  DtcNone,     // fault memory read: no faults
  DtcPresent,  // fault memory read: faults stored
  Fault,       // FAULT_STOP of a service operation
  Test,        // settings: sound check
};

void indicatorBegin();
// Queue a melody (any task, never blocks).
void indicatorPlay(Sound s);
// Settings: 0 = off, 1 low, 2 medium, 3 high.
void indicatorSetLevels(uint8_t sound, uint8_t light);
// Settings: colour sweep of the RGB LED.
void indicatorTestLight();

#endif

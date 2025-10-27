// File: src/modes/ManualMode.h
#pragma once
#include <Arduino.h>
#include "../hw/RelayBank.h"
#include "../state/RelayState.h"

class ManualMode {
public:
  struct Pins { int pinNext; int pinPrev; };

  // Debounce ISR en microsegundos para entradas de caudal
  static constexpr unsigned long DEBOUNCE_US_  = 1500;
  static constexpr int           NUM_MAINS_    = 12;
  static constexpr int           NUM_SECS_     = 2;
  static constexpr unsigned long SAFE_BOOT_MS_ = 50;

  // Mapeo eléctrico (orden EXACTO al README)
  static const uint8_t MAIN_PINS_[NUM_MAINS_];
  static const bool    MAIN_AL_[NUM_MAINS_];
  static const uint8_t SEC_PINS_[NUM_SECS_];
  static const bool    SEC_AL_[NUM_SECS_];

  // “Always on” (bomba/12V) en tu HW
  static constexpr int PIN_ALWAYS_ON_    = 33;
  static constexpr int PIN_ALWAYS_ON_12_ = 12;

  ManualMode(RelayBank& bank,
             const Pins& pins,
             const int* customStates,
             int numStates,
             unsigned long debounceMs,
             unsigned long longPressMs,
             unsigned long stepDelayMs,
             int pinFlow1 = 34,
             int pinFlow2 = 35);

  // ciclo de vida
  void begin();
  void reset();
  void run();

  // latch manual desde Web
  void webStartState(const RelayState& rs);
  void webStopState();
  inline bool webIsActive() const { return webActive_; }

  // telemetría cruda para WebUI (/mode.json)
  bool telemetryRaw(uint32_t& d1, uint32_t& d2,
                    uint32_t& elapsedMs, int& stateIdx, bool& active) const;

private:
  // helpers relés
  void allMainsOff();
  void allSecsOff();
  void applyMainsPattern(int m, bool direct);
  void smoothTransitionTo(int idx);
  void applyRelayState_(const RelayState& rs);

  // caudal (ISR)
  static void IRAM_ATTR isrFlow1_();
  static void IRAM_ATTR isrFlow2_();
  void attachFlowIsr_();
  void detachFlowIsr_();
  void resetFlowCounters_();

  // estado
  RelayBank&     bank_;
  Pins           pins_;
  const int*     customStates_;
  int            numCustomStates_;
  unsigned long  debounceMs_;
  unsigned long  longPressMs_;
  unsigned long  stepDelayMs_;
  int            pinFlow1_;
  int            pinFlow2_;

  bool           initialized_ = false;

  // navegación por estados con botones
  int            customStateIndex_ = 0;
  bool           toggleNextState_  = false;
  bool           togglePrevState_  = false;

  unsigned long  lastNextChange_   = 0;
  unsigned long  lastPrevChange_   = 0;
  unsigned long  nextPressStart_   = 0;
  unsigned long  prevPressStart_   = 0;
  bool           lastNextRaw_      = false;
  bool           lastPrevRaw_      = false;
  bool           nextLongHandled_  = false;
  bool           prevLongHandled_  = false;

  // latch web
  RelayState     webState_;
  bool           webActive_        = false;

  // medición actual
  unsigned long  webStartMs_       = 0;

  // contadores ISR compartidos
  static volatile unsigned long pulses1_;
  static volatile unsigned long pulses2_;
  static volatile unsigned long lastUs1_;
  static volatile unsigned long lastUs2_;
};

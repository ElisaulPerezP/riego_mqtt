#pragma once
#include <Arduino.h>
#include "../hw/RelayBank.h"
#include "../state/RelayState.h"

class ManualMode {
public:
  struct Pins { int pinNext; int pinPrev; };

  ManualMode(RelayBank& bank,
             const Pins& pins,
             const int* customStates,
             int numStates,
             unsigned long debounceMs,
             unsigned long longPressMs,
             unsigned long stepDelayMs,
             int pinFlow1 = -1,
             int pinFlow2 = -1);

  // ciclo de vida
  void begin();
  void reset();
  void run();

  // Latch manual por Web
  void webStartState(const RelayState& rs);
  void webStopState();
  bool webIsActive() const { return webActive_; }

  // Telemetría cruda (pulsos y tiempo desde el último cambio de zona)
  bool telemetryRaw(uint32_t& d1, uint32_t& d2, uint32_t& elapsedMs, int& stateIdx, bool& active) const;

  // --- Arrays de mapeo HW (definidos en .cpp) ---
  static const uint8_t MAIN_PINS_[12];
  static const bool    MAIN_AL_[12];
  static const uint8_t SEC_PINS_[2];
  static const bool    SEC_AL_[2];

private:
  // helpers HW
  void allMainsOff();
  void allSecsOff();
  void applyMainsPattern(int m, bool direct);
  void smoothTransitionTo(int idx);

  // latch Web/hard
  void applyRelayState_(const RelayState& rs);
  void attachFlowIsr_();
  void detachFlowIsr_();

  // ISRs caudal
  static void IRAM_ATTR isrFlow1_();
  static void IRAM_ATTR isrFlow2_();

private:
  // Config constante
  static constexpr int NUM_MAINS_ = 12;
  static constexpr int NUM_SECS_  = 2;
  static constexpr int PIN_ALWAYS_ON_    = 33;
  static constexpr int PIN_ALWAYS_ON_12_ = 12;
  static constexpr unsigned long SAFE_BOOT_MS_ = 100;     // ventana de arranque
  static constexpr unsigned long DEBOUNCE_US_  = 300;     // anti-rebote caudalímetros

  RelayBank& bank_;
  Pins       pins_;
  const int* customStates_ = nullptr;
  int        numCustomStates_ = 0;
  unsigned long debounceMs_   = 100;
  unsigned long longPressMs_  = 5000;
  unsigned long stepDelayMs_  = 500;

  // Pines caudal
  int pinFlow1_ = -1;
  int pinFlow2_ = -1;

  // Estado
  bool initialized_ = false;

  // botones HW (next/prev)
  bool     lastNextRaw_ = false, lastPrevRaw_ = false;
  uint32_t lastNextChange_ = 0, lastPrevChange_ = 0;
  uint32_t nextPressStart_ = 0, prevPressStart_ = 0;
  bool     nextLongHandled_ = false, prevLongHandled_ = false;

  // toggles hacia RelayBank (fert/alarma)
  bool toggleNextState_ = false, togglePrevState_ = false;

  // índice de estado activo por HW
  int customStateIndex_ = 0;

  // latch Web
  RelayState webState_;
  bool       webActive_ = false;

  // ISR flujo (compartidos)
  static volatile unsigned long pulses1_;
  static volatile unsigned long pulses2_;
  static volatile unsigned long lastUs1_;
  static volatile unsigned long lastUs2_;

  // control de attach para no duplicar
  bool flowIsrAttached_ = false;

  // base de medición (reinicia al cambiar de zona o al iniciar por Web)
  uint32_t p1Start_ = 0, p2Start_ = 0;
  uint32_t zoneStartMs_ = 0;
};

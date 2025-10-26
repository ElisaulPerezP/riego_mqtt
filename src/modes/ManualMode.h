#pragma once
#include <Arduino.h>
#include "modes/IMode.h"
#include "hw/RelayBank.h"
#include "../state/RelayState.h"   // <-- para aplicar estados /states

class ManualMode : public IMode {
public:
  struct Pins { uint8_t pinNext; uint8_t pinPrev; };

  ManualMode(RelayBank& bank,
             const Pins& pins,
             const int* customStates,
             int numStates,
             unsigned long debounceMs,
             unsigned long longPressMs,
             unsigned long stepDelayMs,
             int pinFlow1 = -1,
             int pinFlow2 = -1);

  void begin() override;
  void run()   override;
  void reset() override;

  // ===== Control manual “desde Web” basado en RelayState =====
  void webStartState(const RelayState& rs); // aplica máscaras y activa “latch”
  void webStopState();                      // apaga latch y libera
  bool webIsActive() const { return webActive_; }

private:
  // --- helpers legacy (igual que los que ya tenías) ---
  void allMainsOff();
  void allSecsOff();
  void applyMainsPattern(int m, bool direct);
  void smoothTransitionTo(int idx);

  // --- helpers Web ---
  void applyRelayState_(const RelayState& rs);

  // ISR caudal (por si quieres mostrar volumen luego)
  void attachFlowIsr_();
  void detachFlowIsr_();
  static void IRAM_ATTR isrFlow1_();
  static void IRAM_ATTR isrFlow2_();

  // Dependencias/config
  RelayBank&       bank_;
  Pins             pins_;
  const int*       customStates_;
  const int        numCustomStates_;
  const unsigned long debounceMs_;
  const unsigned long longPressMs_;
  const unsigned long stepDelayMs_;
  const int        pinFlow1_;
  const int        pinFlow2_;

  // Estado legacy (botones)
  bool initialized_             = false;
  int  customStateIndex_        = 0;
  bool lastNextRaw_             = false;
  bool lastPrevRaw_             = false;
  bool nextLongHandled_         = false;
  bool prevLongHandled_         = false;
  unsigned long lastNextChange_ = 0;
  unsigned long lastPrevChange_ = 0;
  unsigned long nextPressStart_ = 0;
  unsigned long prevPressStart_ = 0;
  bool toggleNextState_         = false;
  bool togglePrevState_         = false;

  // Mapeo de pines (idéntico a tu implementación actual)
  static const uint8_t MAIN_PINS_[12];
  static const bool    MAIN_AL_[12];
  static const uint8_t SEC_PINS_[2];
  static const bool    SEC_AL_[2];
  static const uint8_t PIN_ALWAYS_ON_    = 33;
  static const uint8_t PIN_ALWAYS_ON_12_ = 12;
  static const int NUM_MAINS_ = 12;
  static const int NUM_SECS_  = 2;
  static const unsigned long SAFE_BOOT_MS_ = 400;

  // ===== Latch manual Web =====
  bool     webActive_  = false;
  RelayState webState_;
  uint32_t webStartMs_ = 0;

  // ISR caudal (opcional)
  static volatile unsigned long pulses1_;
  static volatile unsigned long pulses2_;
  static volatile unsigned long lastUs1_;
  static volatile unsigned long lastUs2_;
  static constexpr unsigned long DEBOUNCE_US_ = 50000UL;
};

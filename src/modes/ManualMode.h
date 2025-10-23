#pragma once
#include <Arduino.h>
#include "modes/IMode.h"
#include "hw/RelayBank.h"

// Modo "manual" (equivale a tu fullMode).
// - Avanza / retrocede una secuencia de estados con NEXT/PREV.
// - Pulsación larga de NEXT/PREV conmuta salidas auxiliares via RelayBank.
// - Mantiene el mismo mapeo de pines que tu implementación original.

class ManualMode : public IMode {
public:
  struct Pins {
    uint8_t pinNext;  // activo LOW
    uint8_t pinPrev;  // activo HIGH
  };

  // customStates: arreglo con índices de estado (0..OFF), numStates su tamaño.
  ManualMode(RelayBank& bank,
             const Pins& pins,
             const int* customStates,
             int numStates,
             unsigned long debounceMs,
             unsigned long longPressMs,
             unsigned long stepDelayMs);

  void begin() override;  // <- necesario para no ser abstracta
  void run()   override;
  void reset() override;

private:
  // --- helpers equivalentes a fullMode.cpp ---
  void allMainsOff();
  void allSecsOff();
  void applyMainsPattern(int m, bool direct);
  void smoothTransitionTo(int idx);

  // Dependencias y configuración
  RelayBank&       bank_;
  Pins             pins_;
  const int*       customStates_;
  const int        numCustomStates_;
  const unsigned long debounceMs_;
  const unsigned long longPressMs_;
  const unsigned long stepDelayMs_;

  // Estado interno
  bool initialized_            = false;
  int  customStateIndex_       = 0;
  bool lastNextRaw_            = false;
  bool lastPrevRaw_            = false;
  bool nextLongHandled_        = false;
  bool prevLongHandled_        = false;
  unsigned long lastNextChange_ = 0;
  unsigned long lastPrevChange_ = 0;
  unsigned long nextPressStart_ = 0;
  unsigned long prevPressStart_ = 0;
  bool toggleNextState_        = false;  // estado software de toggles
  bool togglePrevState_        = false;

  // ---- Mapeo de pines (igual que tu fullMode original) ----
  // IMPORTANTE: los arrays se definen en el .cpp
  static const uint8_t MAIN_PINS_[12];   // {5,18,0,21,17,3,2,22,16,1,15,23}
  static const bool    MAIN_AL_[12];     // todos activo-bajo
  static const uint8_t SEC_PINS_[2];     // {27,26}
  static const bool    SEC_AL_[2];       // ambos activo-alto

  // Pines “always on”
  static const uint8_t PIN_ALWAYS_ON_    = 33;
  static const uint8_t PIN_ALWAYS_ON_12_ = 12;

  // Constantes auxiliares
  static const int NUM_MAINS_ = 12;
  static const int NUM_SECS_  = 2;
  static const unsigned long SAFE_BOOT_MS_ = 400;
};

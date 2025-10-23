#pragma once
#include "IMode.h"
#include "../hw/RelayBank.h"
#include <vector>

// Orden de riego (para el futuro via MQTT)
struct IrrigationOrder {
  // Índice de main (0..N-1), si aplica a main directo/complementario
  int  mainIndex = -1;
  bool direct    = true;         // true=directo; false=complementario
  unsigned long durationMs = 0;  // o 0 = ignora duración
  unsigned long volumeMl   = 0;  // futuro: cerrar por volumen
  // fertilizantes, etc... futuro
};

class AutoMode : public IMode {
public:
  AutoMode(RelayBank& bank,
           const int* customStates, int numStates,
           unsigned long stateDurationMs,
           unsigned long offDurationMs,
           unsigned long stepDelayMs,
           int pinFlow1, int pinFlow2);

  void begin() override;
  void reset() override;
  void run()   override;

  // Futuro: encolar órdenes desde MQTT
  void enqueueOrder(const IrrigationOrder& ord);

  // Telemetría mínima
  struct Tele {
    bool activePhase;
    int  stateIndex;
    unsigned long pulses;  // acumulado reciente
  };
  Tele telemetry() const;

private:
  static void IRAM_ATTR isrFlow1Thunk();
  static void IRAM_ATTR isrFlow2Thunk();

  void smoothTransition(int idx);
  void applyOrderIfAny();
  void handlePhaseLogic();

private:
  RelayBank& bank_;
  const int* customStates_;
  const int  numStates_;
  const unsigned long stateDurMs_;
  const unsigned long offDurMs_;
  const unsigned long stepDelayMs_;

  const int pinFlow1_;
  const int pinFlow2_;

  // estado
  volatile static AutoMode* self_; // para ISR
  volatile static unsigned long pulse1_;
  volatile static unsigned long pulse2_;
  volatile static unsigned long lastMicros1_;
  volatile static unsigned long lastMicros2_;

  static const unsigned long DEBOUNCE_US = 50000UL;

  bool initialized_   = false;
  bool activePhase_   = true;
  int  stateIndex_    = 0;
  unsigned long phaseStart_ = 0;
  unsigned long stateStart_ = 0;
  unsigned long pulseCount_ = 0;

  std::vector<IrrigationOrder> queue_;
};

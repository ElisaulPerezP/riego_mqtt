#include "AutoMode.h"
#include <Arduino.h>

volatile AutoMode* AutoMode::self_ = nullptr;
volatile unsigned long AutoMode::pulse1_ = 0;
volatile unsigned long AutoMode::pulse2_ = 0;
volatile unsigned long AutoMode::lastMicros1_ = 0;
volatile unsigned long AutoMode::lastMicros2_ = 0;

AutoMode::AutoMode(RelayBank& bank,
                   const int* customStates, int numStates,
                   unsigned long stateDurationMs,
                   unsigned long offDurationMs,
                   unsigned long stepDelayMs,
                   int pinFlow1, int pinFlow2)
: bank_(bank),
  customStates_(customStates), numStates_(numStates),
  stateDurMs_(stateDurationMs), offDurMs_(offDurationMs),
  stepDelayMs_(stepDelayMs),
  pinFlow1_(pinFlow1), pinFlow2_(pinFlow2)
{}

void IRAM_ATTR AutoMode::isrFlow1Thunk() {
  unsigned long now = micros();
  if (now - lastMicros1_ >= DEBOUNCE_US) { pulse1_++; lastMicros1_ = now; }
}
void IRAM_ATTR AutoMode::isrFlow2Thunk() {
  unsigned long now = micros();
  if (now - lastMicros2_ >= DEBOUNCE_US) { pulse2_++; lastMicros2_ = now; }
}

void AutoMode::begin() {
  bank_.begin();
  if (pinFlow1_ >= 0) { pinMode(pinFlow1_, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(pinFlow1_), isrFlow1Thunk, RISING); }
  if (pinFlow2_ >= 0) { pinMode(pinFlow2_, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(pinFlow2_), isrFlow2Thunk, RISING); }
  self_ = this;

  phaseStart_ = stateStart_ = millis();
  smoothTransition(customStates_[stateIndex_]);
  initialized_ = true;
}

void AutoMode::reset() {
  initialized_ = false;
  activePhase_ = true;
  stateIndex_  = 0;
  phaseStart_ = stateStart_ = 0;
  pulseCount_ = 0;
  pulse1_ = pulse2_ = 0;
  lastMicros1_ = lastMicros2_ = 0;
}

void AutoMode::smoothTransition(int idx) {
  // similar a tu blinkMode
  bank_.allMainsOff(); bank_.allSecsOff();
  bank_.setAlways(false);
  delay(stepDelayMs_);

  int m = idx / 2;
  bool direct = ((idx & 1) == 0);

  bank_.setAlways(true);
  bank_.setAlways12(true);
  delay(stepDelayMs_);

  if (direct) {
    bank_.setMainDirect(m);
    delay(stepDelayMs_);
    bank_.setSec(0, true);
    delay(stepDelayMs_);
  } else {
    bank_.setSec(0, false); // apaga sec0 (si era activa baja)
    delay(stepDelayMs_);
    bank_.allMainsOff(); delay(stepDelayMs_);
    bank_.setAlways(false); delay(stepDelayMs_);
    bank_.setAlways(true);  delay(stepDelayMs_);
    bank_.applyMainsPattern(m, false); delay(stepDelayMs_);
    bank_.setSec(1, true);
    delay(stepDelayMs_);
  }
  bank_.setToggleNext(true);
  bank_.setTogglePrev(true);
}

void AutoMode::enqueueOrder(const IrrigationOrder& ord) {
  queue_.push_back(ord);
}

AutoMode::Tele AutoMode::telemetry() const {
  Tele t;
  t.activePhase = activePhase_;
  t.stateIndex  = stateIndex_;
  noInterrupts();
  unsigned long p = pulse1_ + pulse2_;
  interrupts();
  t.pulses = p;
  return t;
}

void AutoMode::applyOrderIfAny() {
  if (queue_.empty()) return;
  // Ejemplo simple: toma la primera orden y ejecuta una transición directa
  IrrigationOrder o = queue_.front();
  queue_.erase(queue_.begin());
  if (o.mainIndex >= 0) {
    // Traduce a índice “idx = main*2 + (direct?0:1)”
    int idx = o.mainIndex * 2 + (o.direct ? 0 : 1);
    smoothTransition(idx);
    if (o.durationMs > 0) {
      // Ejecuta durante X ms y luego OFF transicional (simple)
      unsigned long t0 = millis();
      while (millis() - t0 < o.durationMs) {
        // permite contar pulsos
        yield();
      }
      bank_.allMainsOff(); bank_.allSecsOff();
      bank_.setAlways(false); bank_.setAlways12(false);
    }
  }
}

void AutoMode::handlePhaseLogic() {
  unsigned long now = millis();

  if (activePhase_) {
    // acumula pulsos recientes
    noInterrupts();
    unsigned long p_tot = pulse1_ + pulse2_;
    pulse1_ = pulse2_ = 0;
    interrupts();
    pulseCount_ += p_tot;

    if ((now - stateStart_ >= stateDurMs_) || (pulseCount_ >= 1000UL)) {
      stateIndex_++;
      stateStart_ = now;
      pulseCount_ = 0;
      if (stateIndex_ >= numStates_) {
        activePhase_ = false;
        phaseStart_  = now;
        bank_.allMainsOff(); bank_.allSecsOff();
        bank_.setAlways(false); bank_.setAlways12(false);
        bank_.setToggleNext(false); bank_.setTogglePrev(false);
      } else {
        smoothTransition(customStates_[stateIndex_]);
      }
    }
  } else {
    if (now - phaseStart_ >= offDurMs_) {
      activePhase_ = true;
      stateIndex_  = 0;
      phaseStart_ = stateStart_ = now;
      smoothTransition(customStates_[stateIndex_]);
    }
  }
}

void AutoMode::run() {
  if (!initialized_) { begin(); return; }
  applyOrderIfAny();  // si hay órdenes MQTT, aplícalas
  handlePhaseLogic(); // comportamiento “blink” base
}

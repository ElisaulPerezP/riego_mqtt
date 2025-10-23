#include "AutoMode.h"
#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

// ====== estáticos ISR ======
volatile unsigned long AutoMode::pulse1_ = 0;
volatile unsigned long AutoMode::pulse2_ = 0;
volatile unsigned long AutoMode::lastMicros1_ = 0;
volatile unsigned long AutoMode::lastMicros2_ = 0;

// ------------------- ctor -------------------
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

// ------------------- ISR caudal -------------------
void IRAM_ATTR AutoMode::isrFlow1Thunk() {
  unsigned long now = micros();
  if (now - lastMicros1_ >= DEBOUNCE_US) { pulse1_++; lastMicros1_ = now; }
}
void IRAM_ATTR AutoMode::isrFlow2Thunk() {
  unsigned long now = micros();
  if (now - lastMicros2_ >= DEBOUNCE_US) { pulse2_++; lastMicros2_ = now; }
}

// ------------------- ciclo de vida -------------------
void AutoMode::begin() {
  bank_.begin();
  if (pinFlow1_ >= 0) { pinMode(pinFlow1_, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(pinFlow1_), isrFlow1Thunk, RISING); }
  if (pinFlow2_ >= 0) { pinMode(pinFlow2_, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(pinFlow2_), isrFlow2Thunk, RISING); }

  phaseStart_ = stateStart_ = millis();
  // Si no hay programa configurado, arranca con la transición inicial legacy.
  if (!prog_ && customStates_ && numStates_ > 0) {
    smoothTransition(customStates_[0]);
  }
  initialized_ = true;
}

void AutoMode::reset() {
  initialized_ = false;

  // Legacy
  activePhase_ = true;
  stateIndex_  = 0;
  phaseStart_  = 0;
  stateStart_  = 0;
  pulseCount_  = 0;

  // Programado
  phase_        = Phase::IDLE;
  stepIdx_      = 0;
  stepStartMs_  = 0;
  pauseStartMs_ = 0;
  stepStartP1_  = 0;
  stepStartP2_  = 0;
  runVolumeMl_  = 0;
  curStartIdx_  = -1;
  curSetIdx_    = -1;
  timeScale_    = 1.0f;
  volScale_     = 1.0f;

  allOff_();
}

void AutoMode::run() {
  if (!initialized_) { begin(); return; }

  // Si hay programa asignado, usarlo. Si no, comportamiento legacy.
  if (prog_ && prog_->enabled && (!prog_->sets.empty() || !prog_->starts.empty())) {
    runScheduled();
  } else {
    // Legacy "blink"
    handlePhaseLogic();
  }
}

// ------------------- compat órdenes -------------------
void AutoMode::enqueueOrder(const IrrigationOrder& ord) {
  queue_.push_back(ord);
}

// ------------------- Programación: seteo -------------------
void AutoMode::setSchedule(const ProgramSpec* prog, const FlowCalibration* cal) {
  prog_ = prog;
  if (cal) cal_ = *cal;
  allOff_();
  phase_ = Phase::IDLE;
  stepIdx_ = 0;
  runVolumeMl_ = 0;
  curStartIdx_ = -1;
  curSetIdx_   = -1;
}

// ------------------- Telemetría -------------------
AutoMode::Tele AutoMode::telemetry() const {
  Tele t;

  time_t nowEpoch = time(nullptr);
  t.timeSynced = (nowEpoch > 100000);
  t.pausing    = (phase_ == Phase::PAUSE);
  t.running    = (phase_ == Phase::RUN_STEP || t.pausing);

  // Pulsos globales
  noInterrupts();
  t.pulses1 = pulse1_;
  t.pulses2 = pulse2_;
  interrupts();

  t.programEnabled = (prog_ && prog_->enabled);
  t.nextStartEpoch = computeNextStartEpoch_();

  if (prog_ && t.running) {
    const StepSpec* sp = nullptr;
    if (curSetIdx_ >= 0 && (size_t)curSetIdx_ < prog_->sets.size()) {
      const StepSet& set = prog_->sets[curSetIdx_];
      if (stepIdx_ < set.steps.size()) sp = &set.steps[stepIdx_];
    }
    // fallback si no hay sets -> incompatible, pero no debería pasar si t.running
    if (!sp) {
      t.stepIndex = -1;
      return t;
    }

    const int idx = sp->idx;
    t.stepIndex  = (int)stepIdx_;
    t.stateIndex = (int)stepIdx_;
    t.mainIndex  = idx / 2;
    t.direct     = ((idx & 1) == 0);

    // límites (escalados)
    const uint32_t durScaled = sp->maxDurationMs ? (uint32_t)lroundf((float)sp->maxDurationMs * timeScale_) : 0;
    const uint32_t volScaled = sp->targetMl     ? (uint32_t)lroundf((float)sp->targetMl     * volScale_ ) : 0;

    t.stateDurationMs = durScaled;
    t.stateTargetMl   = volScaled;
    t.stateElapsedMs  = millis() - stepStartMs_;

    // volumen del paso (delta)
    noInterrupts();
    unsigned long p1 = pulse1_, p2 = pulse2_;
    interrupts();
    uint32_t d1 = (uint32_t)(p1 - stepStartP1_);
    uint32_t d2 = (uint32_t)(p2 - stepStartP2_);
    t.stateVolumeMl = volumeMlFromPulses_(d1, d2);

    t.runVolumeMl = runVolumeMl_ + t.stateVolumeMl;
  } else if (!prog_ && customStates_ && numStates_ > 0 && activePhase_) {
    // Legacy básico
    const int idx = customStates_[stateIndex_];
    t.stepIndex  = stateIndex_;
    t.stateIndex = stateIndex_;
    t.mainIndex  = idx / 2;
    t.direct     = ((idx & 1) == 0);
    t.stateElapsedMs  = millis() - stateStart_;
    t.stateDurationMs = stateDurMs_;
    t.stateTargetMl   = 0;
    t.stateVolumeMl   = 0;
    t.runVolumeMl     = 0;
  } else {
    t.stepIndex = -1;
  }

  return t;
}

// ------------------- helpers comunes -------------------
void AutoMode::smoothTransition(int idx) {
  // Apaga todo
  bank_.allMainsOff(); bank_.allSecsOff();
  bank_.setAlways(false); bank_.setAlways12(false);
  delay(stepDelayMs_);

  // Enciende bancos
  bank_.setAlways(true);
  bank_.setAlways12(true);
  delay(stepDelayMs_);

  const int  m      = idx / 2;
  const bool direct = ((idx & 1) == 0);

  if (direct) {
    bank_.setMainDirect(m);
    delay(stepDelayMs_);
    bank_.setSec(0, false);
    bank_.setSec(1, true);
    delay(stepDelayMs_);
  } else {
    bank_.applyMainsPattern(m, false);
    delay(stepDelayMs_);
    bank_.setSec(0, true);
    bank_.setSec(1, false);
    delay(stepDelayMs_);
  }

  bank_.setToggleNext(true);
  bank_.setTogglePrev(true);
}

void AutoMode::allOff_() {
  bank_.allMainsOff();
  bank_.allSecsOff();
  bank_.setAlways(false);
  bank_.setAlways12(false);
}

bool AutoMode::waitForValidIP(uint32_t timeoutMs) const {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if ((uint32_t)WiFi.localIP() != 0) return true;
    delay(50); yield();
  }
  return false;
}

uint32_t AutoMode::msSince(uint32_t t0) const {
  return (uint32_t)(millis() - t0);
}

// ------------------- Legacy blink (sin programa) -------------------
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
        allOff_();
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

// ------------------- Programado: tiempo/volumen -------------------
bool AutoMode::timeNow(struct tm& out) const {
  time_t now = time(nullptr);
  if (now < 1600000000) return false; // inválido si epoch < ~2020
  if (!localtime_r(&now, &out)) return false;
  return true;
}

bool AutoMode::todayMatches(uint8_t dowMask, int wday) const {
  // tm_wday: 0=Dom..6=Sab ; máscara: bit0=Lun..bit6=Dom
  int dowBit = -1;
  switch (wday) {
    case 1: dowBit = 0; break; // Lun
    case 2: dowBit = 1; break; // Mar
    case 3: dowBit = 2; break; // Mie
    case 4: dowBit = 3; break; // Jue
    case 5: dowBit = 4; break; // Vie
    case 6: dowBit = 5; break; // Sab
    case 0: dowBit = 6; break; // Dom
  }
  if (dowBit < 0) return false;
  return (dowMask & (1 << dowBit)) != 0;
}

int AutoMode::shouldStartNow(const struct tm& nowTm) {
  if (!prog_ || !prog_->enabled || prog_->starts.empty()) return -1;

  int yday = nowTm.tm_yday;
  int minuteOfDay = nowTm.tm_hour * 60 + nowTm.tm_min;

  // Evitar doble disparo en el mismo minuto
  if (lastStartYDay_ == yday && lastStartMin_ == minuteOfDay) return -1;

  for (size_t i=0;i<prog_->starts.size();++i) {
    const StartSpec& st = prog_->starts[i];
    if (!st.enabled) continue;
    if (!todayMatches(st.dowMask, nowTm.tm_wday)) continue;
    if (st.hour == (uint8_t)nowTm.tm_hour && st.minute == (uint8_t)nowTm.tm_min) {
      lastStartYDay_ = yday;
      lastStartMin_  = minuteOfDay;
      return (int)i;
    }
  }
  return -1;
}

void AutoMode::startProgramForStart(size_t startIdx) {
  if (!prog_ || startIdx >= prog_->starts.size()) return;

  const StartSpec& st = prog_->starts[startIdx];
  // Resolve set
  size_t setIdx = (size_t)st.stepSetIndex;
  if (prog_->sets.empty() || setIdx >= prog_->sets.size()) {
    // Sin sets válidos -> no arranca
    return;
  }

  const StepSet& set = prog_->sets[setIdx];
  if (set.steps.empty()) return;

  // Contexto de run
  curStartIdx_ = (int)startIdx;
  curSetIdx_   = (int)setIdx;
  timeScale_   = st.timeScale > 0.f ? st.timeScale : 1.0f;
  volScale_    = st.volumeScale > 0.f ? st.volumeScale : 1.0f;

  phase_    = Phase::RUN_STEP;
  stepIdx_  = 0;
  runVolumeMl_ = 0;

  noInterrupts();
  stepStartP1_ = pulse1_;
  stepStartP2_ = pulse2_;
  interrupts();

  beginStep_(stepIdx_);
}

void AutoMode::stopProgram() {
  allOff_();
  bank_.setToggleNext(false); bank_.setTogglePrev(false);
  phase_ = Phase::IDLE;
  stepIdx_ = 0;
  curStartIdx_ = -1;
  curSetIdx_   = -1;
}

void AutoMode::beginStep_(size_t idx) {
  if (!prog_ || curSetIdx_ < 0 || (size_t)curSetIdx_ >= prog_->sets.size()) { stopProgram(); return; }
  const StepSet& set = prog_->sets[curSetIdx_];
  if (idx >= set.steps.size()) { stopProgram(); return; }

  smoothTransition(set.steps[idx].idx);
  stepStartMs_ = millis();

  noInterrupts();
  stepStartP1_ = pulse1_;
  stepStartP2_ = pulse2_;
  interrupts();
}

void AutoMode::finishStep_() {
  // suma volumen del paso al acumulado
  noInterrupts();
  unsigned long p1 = pulse1_, p2 = pulse2_;
  interrupts();
  uint32_t d1 = (uint32_t)(p1 - stepStartP1_);
  uint32_t d2 = (uint32_t)(p2 - stepStartP2_);
  runVolumeMl_ += volumeMlFromPulses_(d1, d2);

  if (!prog_ || curSetIdx_ < 0 || (size_t)curSetIdx_ >= prog_->sets.size()) { stopProgram(); return; }
  const StepSet& set = prog_->sets[curSetIdx_];

  // ¿hay pausa?
  uint32_t pauseMs = set.pauseMsBetweenSteps ? (uint32_t)lroundf((float)set.pauseMsBetweenSteps * timeScale_) : 0;

  if (pauseMs > 0) {
    allOff_();
    bank_.setToggleNext(false); bank_.setTogglePrev(false);
    pauseStartMs_ = millis();
    phase_ = Phase::PAUSE;
  } else {
    // sin pausa
    stepIdx_++;
    if (stepIdx_ < set.steps.size()) {
      beginStep_(stepIdx_);
      phase_ = Phase::RUN_STEP;
    } else {
      stopProgram();
    }
  }
}

void AutoMode::runScheduled() {
  struct tm nowTm;
  bool haveTime = timeNow(nowTm);

  // Arranque por horario
  if (phase_ == Phase::IDLE && haveTime) {
    int sIdx = shouldStartNow(nowTm);
    if (sIdx >= 0) {
      startProgramForStart((size_t)sIdx);
    }
  }

  if (!prog_ || curSetIdx_ < 0 || (size_t)curSetIdx_ >= prog_->sets.size()) return;
  const StepSet& set = prog_->sets[curSetIdx_];

  // Paso en ejecución
  if (phase_ == Phase::RUN_STEP) {
    const StepSpec& sp = set.steps[stepIdx_];

    // Volumen
    uint32_t volTarget = sp.targetMl ? (uint32_t)lroundf((float)sp.targetMl * volScale_) : 0;
    if (volTarget > 0 && (cal_.pulsesPerMl1 > 0.f || cal_.pulsesPerMl2 > 0.f)) {
      noInterrupts();
      unsigned long p1 = pulse1_, p2 = pulse2_;
      interrupts();
      uint32_t d1 = (uint32_t)(p1 - stepStartP1_);
      uint32_t d2 = (uint32_t)(p2 - stepStartP2_);
      uint32_t ml  = volumeMlFromPulses_(d1, d2);
      if (ml >= volTarget) {
        finishStep_();
        return;
      }
    }

    // Tiempo
    uint32_t durMs = sp.maxDurationMs ? (uint32_t)lroundf((float)sp.maxDurationMs * timeScale_) : 0;
    if (durMs > 0 && msSince(stepStartMs_) >= durMs) {
      finishStep_();
      return;
    }
  }

  // Pausa
  if (phase_ == Phase::PAUSE) {
    uint32_t pauseMs = set.pauseMsBetweenSteps ? (uint32_t)lroundf((float)set.pauseMsBetweenSteps * timeScale_) : 0;
    if (msSince(pauseStartMs_) >= pauseMs) {
      stepIdx_++;
      if (stepIdx_ < set.steps.size()) {
        beginStep_(stepIdx_);
        phase_ = Phase::RUN_STEP;
      } else {
        stopProgram();
      }
    }
  }
}

uint32_t AutoMode::volumeMlFromPulses_(uint32_t p1, uint32_t p2) const {
  if (cal_.pulsesPerMl1 <= 0.f && cal_.pulsesPerMl2 <= 0.f) {
    return p1 + p2;
  }
  float ml1 = (cal_.pulsesPerMl1 > 0.f) ? (float)p1 / cal_.pulsesPerMl1 : 0.f;
  float ml2 = (cal_.pulsesPerMl2 > 0.f) ? (float)p2 / cal_.pulsesPerMl2 : 0.f;
  float ml  = ml1 + ml2;
  if (ml < 0) ml = 0;
  return (uint32_t)lroundf(ml);
}

uint32_t AutoMode::computeNextStartEpoch_() const {
  if (!prog_ || !prog_->enabled || prog_->starts.empty()) return 0;

  time_t nowEpoch = time(nullptr);
  if (nowEpoch <= 100000) return 0; // sin hora válida

  time_t best = 0;

  for (int d = 0; d <= 7; ++d) {
    time_t dayBase = nowEpoch + d * 86400;
    struct tm dayTm;
    if (!localtime_r(&dayBase, &dayTm)) continue;

    for (const auto& st : prog_->starts) {
      if (!st.enabled) continue;
      if (!todayMatches(st.dowMask, dayTm.tm_wday)) continue;

      struct tm ts = dayTm;
      ts.tm_hour = st.hour;
      ts.tm_min  = st.minute;
      ts.tm_sec  = 0;

      time_t cand = mktime(&ts); // epoch local
      if (cand <= nowEpoch) continue; // futuro únicamente
      if (best == 0 || cand < best) best = cand;
    }
  }
  return (best > 0) ? (uint32_t)best : 0;
}

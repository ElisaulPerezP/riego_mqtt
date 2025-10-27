#include "AutoMode.h"
#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <Preferences.h>   // NVS ventanas / zonas

// ====== estáticos ISR ======
volatile unsigned long AutoMode::pulse1_ = 0;
volatile unsigned long AutoMode::pulse2_ = 0;
volatile unsigned long AutoMode::lastMicros1_ = 0;
volatile unsigned long AutoMode::lastMicros2_ = 0;

// --- Registro del último inicio por ventana (evitar doble disparo) ---
namespace {
  int gLastWinYDay = -1;        // yday del último inicio por ventana
  int gLastWinStartMin = -1;    // minuto-del-día (0..1439) del inicio de esa ventana
}

// ------------------- ctor -------------------
AutoMode::AutoMode(RelayBank& bank,
                   const int* customStates, int numStates,
                   unsigned long stateDurationMs,
                   unsigned long offDurationMs,
                   unsigned long stepDelayMs,
                   int pinFlow1, int pinFlow2)
: bank_(bank),
  customStates_(customStates), numStates_(numStates),
  stateDurMs_(stateDurationMs), offDurationMs_(offDurationMs),
  stepDelayMs_(stepDelayMs),
  pinFlow1_(pinFlow1), pinFlow2_(pinFlow2)
{}

// ------------------- setters nuevos -------------------
void AutoMode::setEventPublisher(EventPublisher pub, const String& topic) {
  publisher_ = pub;
  if (topic.length()) pubTopic_ = topic;
}
void AutoMode::setStateNameResolver(StateNameResolver res) {
  nameRes_ = res;
}

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
  // Pull-up externo según README -> INPUT (sin PULLUP interno)
  if (pinFlow1_ >= 0) { pinMode(pinFlow1_, INPUT); attachInterrupt(digitalPinToInterrupt(pinFlow1_), isrFlow1Thunk, RISING); }
  if (pinFlow2_ >= 0) { pinMode(pinFlow2_, INPUT); attachInterrupt(digitalPinToInterrupt(pinFlow2_), isrFlow2Thunk, RISING); }

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

  haveCurWindow_       = false;
  curWindowStartEpoch_ = 0;
  curWindowEndEpoch_   = 0;
  curWindowName_       = "";

  effDurMs_ = 0;
  effVolMl_ = 0;

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
  effDurMs_ = 0;
  effVolMl_ = 0;
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

    // límites efectivos
    // Preferimos los objetivos efectivos (NVS) si existen; si no, escalados de StepSpec.
    const uint32_t durScaled = sp->maxDurationMs ? (uint32_t)lroundf((float)sp->maxDurationMs * timeScale_) : 0;
    const uint32_t volScaled = sp->targetMl     ? (uint32_t)lroundf((float)sp->targetMl     * volScale_ ) : 0;

    t.stateDurationMs = effDurMs_ ? effDurMs_ : durScaled;
    t.stateTargetMl   = effVolMl_ ? effVolMl_ : volScaled;
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
    if (now - phaseStart_ >= offDurationMs_) {
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

  effDurMs_ = 0;
  effVolMl_ = 0;

  beginStep_(stepIdx_);
}

void AutoMode::stopProgram() {
  allOff_();
  bank_.setToggleNext(false); bank_.setTogglePrev(false);
  phase_ = Phase::IDLE;
  stepIdx_ = 0;
  curStartIdx_ = -1;
  curSetIdx_   = -1;

  haveCurWindow_       = false;
  curWindowStartEpoch_ = 0;
  curWindowEndEpoch_   = 0;
  curWindowName_       = "";

  effDurMs_ = 0;
  effVolMl_ = 0;
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

  // ====== Objetivos efectivos (NVS zonas) ======
  // 1) escalados a partir del StepSpec
  const StepSpec& sp = set.steps[idx];
  const uint32_t durScaled = sp.maxDurationMs ? (uint32_t)lroundf((float)sp.maxDurationMs * timeScale_) : 0;
  const uint32_t volScaled = sp.targetMl     ? (uint32_t)lroundf((float)sp.targetMl     * volScale_ ) : 0;

  // 2) leer NVS: "zones" -> z{idx}_time / z{idx}_vol
  uint32_t zVol = 0, zTime = 0;
  (void)readZoneTargetsNVS_((int)idx, zVol, zTime);

  // 3) elegir objetivos efectivos (preferir NVS si >0)
  effDurMs_ = (zTime > 0) ? zTime : durScaled;
  effVolMl_ = (zVol  > 0) ? zVol  : volScaled;

  // ====== PUBLICACIÓN: inicio de estado con objetivos efectivos ======
  publishStateStart_(idx, effDurMs_, effVolMl_);
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

  // ====== PUBLICACIÓN: fin de estado (volumen/tiempo reales) ======
  uint32_t durReal = msSince(stepStartMs_);
  uint32_t volReal = volumeMlFromPulses_(d1, d2);
  publishStateEnd_(stepIdx_, durReal, volReal);

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

  // Arranque por horario (sólo si hay franja activa)
  if (phase_ == Phase::IDLE && haveTime) {
    if (allowedNowByWindows_()) {   // dentro de alguna franja
      // 1) Prioridad: StartSpec explícitos
      int sIdx = shouldStartNow(nowTm);
      if (sIdx >= 0) {
        startProgramForStart((size_t)sIdx);
        return;
      }

      // 2) Fallback: Arrancar al entrar (o si aún no se arrancó) en la ventana -> Set 0
      //    Lee ventanas desde NVS y detecta en cuál estamos (s,e) con s<e
      int md = nowTm.tm_hour * 60 + nowTm.tm_min;
      int curStart = -1;
      {
        Preferences p;
        if (p.begin("windows", true)) {
          uint8_t cnt = p.getUChar("count", 0);
          for (uint8_t i = 0; i < cnt && i < 60; ++i) {
            uint8_t sh = p.getUChar((String("w")+i+"_sh").c_str(), 0);
            uint8_t sm = p.getUChar((String("w")+i+"_sm").c_str(), 0);
            uint8_t eh = p.getUChar((String("w")+i+"_eh").c_str(), 0);
            uint8_t em = p.getUChar((String("w")+i+"_em").c_str(), 0);
            int s = (int)sh*60 + (int)sm;
            int e = (int)eh*60 + (int)em;
            if (s < e && md >= s && md < e) { curStart = s; break; }
          }
          p.end();
        }
      }

      if (curStart >= 0 && prog_ && prog_->enabled && !prog_->sets.empty()) {
        // ¿ya arrancamos esta ventana hoy?
        if (gLastWinYDay != nowTm.tm_yday || gLastWinStartMin != curStart) {
          // Selecciona el Set 0 por defecto
          curStartIdx_ = -1;          // “no viene de StartSpec”
          curSetIdx_   = 0;
          timeScale_   = 1.0f;
          volScale_    = 1.0f;
          phase_       = Phase::RUN_STEP;
          stepIdx_     = 0;
          runVolumeMl_ = 0;

          noInterrupts();
          stepStartP1_ = pulse1_;
          stepStartP2_ = pulse2_;
          interrupts();

          effDurMs_ = 0;
          effVolMl_ = 0;

          beginStep_(stepIdx_);

          gLastWinYDay     = nowTm.tm_yday;
          gLastWinStartMin = curStart;
          return;
        }
      }
    }
  }

  // Si estamos corriendo o en pausa y salimos de franja: detener **publicando** fin de estado
  if ((phase_ == Phase::RUN_STEP || phase_ == Phase::PAUSE) && !allowedNowByWindows_()) {

    if (phase_ == Phase::RUN_STEP) {
      // Medidas reales hasta este instante
      noInterrupts();
      unsigned long p1 = pulse1_, p2 = pulse2_;
      interrupts();

      uint32_t d1 = (uint32_t)(p1 - stepStartP1_);
      uint32_t d2 = (uint32_t)(p2 - stepStartP2_);
      uint32_t volReal = volumeMlFromPulses_(d1, d2);
      uint32_t durReal = msSince(stepStartMs_);

      // Publicar el cierre
      publishStateEnd_(stepIdx_, durReal, volReal);
    }

    stopProgram();   // apaga y resetea fase
    return;
  }

  if (!prog_ || curSetIdx_ < 0 || (size_t)curSetIdx_ >= prog_->sets.size()) return;
  const StepSet& set = prog_->sets[curSetIdx_];

  // Paso en ejecución
  if (phase_ == Phase::RUN_STEP) {
    // Objetivos efectivos ya están en effDurMs_/effVolMl_ (inicializados en beginStep_)
    const StepSpec& sp = set.steps[stepIdx_];

    // Volumen (si hay objetivo)
    if (effVolMl_ > 0 && (cal_.pulsesPerMl1 > 0.f || cal_.pulsesPerMl2 > 0.f)) {
      noInterrupts();
      unsigned long p1 = pulse1_, p2 = pulse2_;
      interrupts();
      uint32_t d1 = (uint32_t)(p1 - stepStartP1_);
      uint32_t d2 = (uint32_t)(p2 - stepStartP2_);
      uint32_t ml  = volumeMlFromPulses_(d1, d2);
      if (ml >= effVolMl_) {
        finishStep_();
        return;
      }
    } else if (effVolMl_ == 0) {
      // Si no hay objetivo por NVS, revisa si StepSpec trae volumen escalado (>0)
      uint32_t volScaled = sp.targetMl ? (uint32_t)lroundf((float)sp.targetMl * volScale_) : 0;
      if (volScaled > 0 && (cal_.pulsesPerMl1 > 0.f || cal_.pulsesPerMl2 > 0.f)) {
        noInterrupts();
        unsigned long p1 = pulse1_, p2 = pulse2_;
        interrupts();
        uint32_t d1 = (uint32_t)(p1 - stepStartP1_);
        uint32_t d2 = (uint32_t)(p2 - stepStartP2_);
        uint32_t ml  = volumeMlFromPulses_(d1, d2);
        if (ml >= volScaled) {
          finishStep_();
          return;
        }
      }
    }

    // Tiempo (si hay objetivo)
    if (effDurMs_ > 0 && msSince(stepStartMs_) >= effDurMs_) {
      finishStep_();
      return;
    } else if (effDurMs_ == 0) {
      // Si no hay objetivo por NVS, usa el del StepSpec escalado (si existe)
      uint32_t durScaled = sp.maxDurationMs ? (uint32_t)lroundf((float)sp.maxDurationMs * timeScale_) : 0;
      if (durScaled > 0 && msSince(stepStartMs_) >= durScaled) {
        finishStep_();
        return;
      }
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

// =================== Ventanas (permiso por hora) ===================
bool AutoMode::allowedNowByWindows_() const {
  // Cache simple para evitar I/O constante en NVS (refresca cada 5s)
  struct Win { uint8_t sh, sm, eh, em; };
  static Win cache[60];
  static uint8_t cacheCnt = 0;
  static uint32_t lastReadMs = 0;

  const uint32_t nowMs = millis();
  if (lastReadMs == 0 || (nowMs - lastReadMs) > 5000) {
    cacheCnt = 0;
    Preferences p;
    if (p.begin("windows", true)) {
      uint8_t cnt = p.getUChar("count", 0);
      for (uint8_t i = 0; i < cnt && i < 60; ++i) {
        Win w;
        w.sh = p.getUChar((String("w")+i+"_sh").c_str(), 0);
        w.sm = p.getUChar((String("w")+i+"_sm").c_str(), 0);
        w.eh = p.getUChar((String("w")+i+"_eh").c_str(), 0);
        w.em = p.getUChar((String("w")+i+"_em").c_str(), 0);
        // Validar rango: semi-abierto [start, end), sin cruzar medianoche
        int s = (int)w.sh*60 + (int)w.sm;
        int e = (int)w.eh*60 + (int)w.em;
        if (s < e) cache[cacheCnt++] = w;
      }
      p.end();
    }
    lastReadMs = nowMs ? nowMs : 1;
  }

  // Si no hay franjas: permitir (compatibilidad hacia atrás)
  if (cacheCnt == 0) return true;

  // Si no hay hora válida (sin NTP), permitir (no bloquear)
  struct tm tmnow;
  if (!timeNow(tmnow)) return true;

  const int md = tmnow.tm_hour * 60 + tmnow.tm_min;
  for (uint8_t i = 0; i < cacheCnt; ++i) {
    const int s = (int)cache[i].sh*60 + (int)cache[i].sm;
    const int e = (int)cache[i].eh*60 + (int)cache[i].em;
    if (md >= s && md < e) return true;
  }
  return false;
}

// =================== Helpers nuevos (ventana + JSON) ===================
String AutoMode::two_(int v){ if (v<0) v=0; if (v>99) v%=100; char b[3]; snprintf(b,sizeof(b),"%02d",v); return String(b); }
String AutoMode::hhmm_(int m){ if (m<0) m=0; if (m>=1440) m%=1440; return two_(m/60)+":"+two_(m%60); }
String AutoMode::isoLocal_(time_t epoch){
  if (epoch <= 0) return String("-");
  struct tm tm; if (!localtime_r(&epoch, &tm)) return String("-");
  char b[32]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
  return String(b);
}
String AutoMode::jsonEscape_(const String& s){
  String o; o.reserve(s.length()+4);
  for (size_t i=0;i<s.length();++i){
    char c=s[i];
    if (c=='\\' || c=='"'){ o += '\\'; o += c; }
    else if (c=='\n'){ o += "\\n"; }
    else if (c=='\r'){ o += "\\r"; }
    else if (c=='\t'){ o += "\\t"; }
    else { o += c; }
  }
  return o;
}

bool AutoMode::computeCurrentWindow_(int& sminOut, int& eminOut, time_t& startEpochOut, time_t& endEpochOut, String& nameOut) const {
  sminOut = -1; eminOut = -1; startEpochOut = 0; endEpochOut = 0; nameOut = "";
  struct tm nowTm;
  if (!timeNow(nowTm)) return false;

  int md = nowTm.tm_hour*60 + nowTm.tm_min;

  struct Win { uint8_t sh, sm, eh, em; };
  bool found=false; int smin=0, emin=0;

  Preferences p;
  if (p.begin("windows", true)) {
    uint8_t cnt = p.getUChar("count", 0);
    for (uint8_t i=0;i<cnt && i<60;i++){
      uint8_t sh = p.getUChar((String("w")+i+"_sh").c_str(), 0);
      uint8_t sm = p.getUChar((String("w")+i+"_sm").c_str(), 0);
      uint8_t eh = p.getUChar((String("w")+i+"_eh").c_str(), 0);
      uint8_t em = p.getUChar((String("w")+i+"_em").c_str(), 0);
      int s = (int)sh*60 + (int)sm;
      int e = (int)eh*60 + (int)em;
      if (s<e && md>=s && md<e){ found=true; smin=s; emin=e; break; }
    }
    p.end();
  }

  if (!found) {
    // Sin ventanas configuradas -> etiqueta genérica
    nameOut = "Sin ventana";
    sminOut = 0; eminOut = 1440;
    // Epochs del día actual
    struct tm st=nowTm, en=nowTm;
    st.tm_hour=0; st.tm_min=0; st.tm_sec=0;
    en.tm_hour=23; en.tm_min=59; en.tm_sec=59;
    startEpochOut = mktime(&st);
    endEpochOut   = mktime(&en);
    return true;
  }

  sminOut = smin; eminOut = emin;
  nameOut = String("Ventana ")+hhmm_(smin)+"–"+hhmm_(emin);

  struct tm st=nowTm, en=nowTm;
  st.tm_hour = smin/60; st.tm_min = smin%60; st.tm_sec = 0;
  en.tm_hour = emin/60; en.tm_min = emin%60; en.tm_sec = 0;
  startEpochOut = mktime(&st);
  endEpochOut   = mktime(&en);
  return true;
}

void AutoMode::publishStateStart_(size_t stepIdx, uint32_t durMsTarget, uint32_t volMlTarget) {
  if (!publisher_) return;

  // Refrescar/obtener ventana actual
  int smin=0, emin=0; time_t se=0, ee=0; String wname;
  haveCurWindow_ = computeCurrentWindow_(smin, emin, se, ee, wname);
  if (haveCurWindow_) {
    curWindowStartEpoch_ = se;
    curWindowEndEpoch_   = ee;
    curWindowName_       = wname;
  } else {
    curWindowStartEpoch_ = 0;
    curWindowEndEpoch_   = 0;
    curWindowName_       = "—";
  }

  String stateName = nameRes_ ? nameRes_((int)stepIdx) : (String("Paso ")+String((int)stepIdx));

  time_t nowE = time(nullptr);
  String payload = "{"
    "\"event\":\"state_start\","
    "\"window\":{"
      "\"name\":\""+jsonEscape_(curWindowName_)+"\","
      "\"start\":\""+isoLocal_(curWindowStartEpoch_)+"\","
      "\"end\":\""+isoLocal_(curWindowEndEpoch_)+"\"},"
    "\"state\":{"
      "\"name\":\""+jsonEscape_(stateName)+"\","
      "\"volume_ml\":" + String(volMlTarget) + ","
      "\"duration_ms\":" + String(durMsTarget) + "},"
    "\"at\":\""+isoLocal_(nowE)+"\""
  "}";

  publisher_(pubTopic_, payload);
}

void AutoMode::publishStateEnd_(size_t stepIdx, uint32_t durMsReal, uint32_t volMlReal) {
  if (!publisher_) return;

  String stateName = nameRes_ ? nameRes_((int)stepIdx) : (String("Paso ")+String((int)stepIdx));
  time_t nowE = time(nullptr);

  // Usamos las MISMAS claves que en state_start para reducir tamaño
  String payload = "{"
    "\"event\":\"state_end\","
    "\"window\":{"
      "\"name\":\""+jsonEscape_(curWindowName_)+"\","
      "\"start\":\""+isoLocal_(curWindowStartEpoch_)+"\","
      "\"end\":\""+isoLocal_(curWindowEndEpoch_)+"\"},"
    "\"state\":{"
      "\"name\":\""+jsonEscape_(stateName)+"\","
      "\"volume_ml\":" + String(volMlReal) + ","
      "\"duration_ms\":" + String(durMsReal) + "},"
    "\"at\":\""+isoLocal_(nowE)+"\""
  "}";

  publisher_(pubTopic_, payload);
}


// =================== NVS zonas ===================
bool AutoMode::readZoneTargetsNVS_(int zoneIdx, uint32_t& volMlOut, uint32_t& timeMsOut) {
  volMlOut = 0; timeMsOut = 0;
  if (zoneIdx < 0) return false;

  Preferences p;
  if (!p.begin("zones", /*ro*/ true)) return false;

  String base = String("z") + String(zoneIdx) + "_";
  volMlOut = p.getUInt((base + "vol").c_str(),  0);
  timeMsOut= p.getUInt((base + "time").c_str(), 0);
  p.end();
  return (volMlOut > 0 || timeMsOut > 0);
}

#include "modes/ManualMode.h"

// Arrays estáticos (igual que tenías)
const uint8_t ManualMode::MAIN_PINS_[12] = { 5,18,0,21,17,3,2,22,16,1,15,23 };
const bool    ManualMode::MAIN_AL_[12]   = { 1,1,1,1,1,1,1,1,1,1,1,1 };
const uint8_t ManualMode::SEC_PINS_[2]   = { 27,26 };
const bool    ManualMode::SEC_AL_[2]     = { 0, 0 };

// ISR caudal
volatile unsigned long ManualMode::pulses1_ = 0;
volatile unsigned long ManualMode::pulses2_ = 0;
volatile unsigned long ManualMode::lastUs1_ = 0;
volatile unsigned long ManualMode::lastUs2_ = 0;

void IRAM_ATTR ManualMode::isrFlow1_() {
  unsigned long now = micros();
  if (now - lastUs1_ >= DEBOUNCE_US_) { pulses1_++; lastUs1_ = now; }
}
void IRAM_ATTR ManualMode::isrFlow2_() {
  unsigned long now = micros();
  if (now - lastUs2_ >= DEBOUNCE_US_) { pulses2_++; lastUs2_ = now; }
}

// ---------------- ctor ----------------
ManualMode::ManualMode(RelayBank& bank,
                       const Pins& pins,
                       const int* customStates,
                       int numStates,
                       unsigned long debounceMs,
                       unsigned long longPressMs,
                       unsigned long stepDelayMs,
                       int pinFlow1,
                       int pinFlow2)
: bank_(bank), pins_(pins),
  customStates_(customStates), numCustomStates_(numStates),
  debounceMs_(debounceMs), longPressMs_(longPressMs), stepDelayMs_(stepDelayMs),
  pinFlow1_(pinFlow1), pinFlow2_(pinFlow2) {}

// ---------------- helpers legacy ----------------
void ManualMode::allMainsOff() {
  for (int i=0;i<NUM_MAINS_;++i)
    digitalWrite(MAIN_PINS_[i], MAIN_AL_[i]? HIGH:LOW);
}
void ManualMode::allSecsOff() {
  for (int j=0;j<NUM_SECS_;++j)
    digitalWrite(SEC_PINS_[j], SEC_AL_[j]? HIGH:LOW);
}
void ManualMode::applyMainsPattern(int m, bool direct) {
  for (int i=0;i<NUM_MAINS_;++i) {
    bool on = direct ? (i==m) : (i!=m);
    digitalWrite(MAIN_PINS_[i], MAIN_AL_[i] ? (on?LOW:HIGH) : (on?HIGH:LOW));
  }
}
void ManualMode::smoothTransitionTo(int idx) {
  const int OFF_IDX = NUM_MAINS_*2;

  allMainsOff();
  allSecsOff();
  digitalWrite(PIN_ALWAYS_ON_,    LOW);
  digitalWrite(PIN_ALWAYS_ON_12_, LOW);
  delay(stepDelayMs_);

  if (idx == OFF_IDX) return;

  digitalWrite(PIN_ALWAYS_ON_,    HIGH);
  digitalWrite(PIN_ALWAYS_ON_12_, HIGH);
  delay(stepDelayMs_);

  int  m      = idx / 2;
  bool direct = ((idx & 1) == 0);

  if (direct) {
    digitalWrite(MAIN_PINS_[m], MAIN_AL_[m]? LOW:HIGH);
  } else {
    applyMainsPattern(m, false);
  }
  delay(stepDelayMs_);

  if (direct) {
    digitalWrite(SEC_PINS_[1], SEC_AL_[1]? LOW:HIGH);
  } else {
    digitalWrite(SEC_PINS_[0], SEC_AL_[0]? LOW:HIGH);
  }
  delay(stepDelayMs_);
}

// ---------------- ciclo de vida ----------------
void ManualMode::begin() {
  for (int i=0;i<NUM_MAINS_;++i) pinMode(MAIN_PINS_[i], INPUT_PULLUP);
  for (int j=0;j<NUM_SECS_; ++j) pinMode(SEC_PINS_[j],  INPUT_PULLUP);
  pinMode(PIN_ALWAYS_ON_,    INPUT_PULLUP);
  pinMode(PIN_ALWAYS_ON_12_, INPUT_PULLUP);
  delay(SAFE_BOOT_MS_);

  for (int i=0;i<NUM_MAINS_;++i) pinMode(MAIN_PINS_[i], OUTPUT);
  for (int j=0;j<NUM_SECS_; ++j) pinMode(SEC_PINS_[j],  OUTPUT);
  pinMode(PIN_ALWAYS_ON_,    OUTPUT);
  pinMode(PIN_ALWAYS_ON_12_, OUTPUT);

  pinMode(pins_.pinNext, INPUT_PULLUP);
  pinMode(pins_.pinPrev, INPUT_PULLDOWN);

  allMainsOff(); allSecsOff();
  digitalWrite(PIN_ALWAYS_ON_,    LOW);
  digitalWrite(PIN_ALWAYS_ON_12_, LOW);

  customStateIndex_ = 0;
  if (customStates_ && numCustomStates_>0) {
    smoothTransitionTo(customStates_[customStateIndex_]);
  }

  toggleNextState_ = togglePrevState_ = false;
  bank_.setToggleNext(false);
  bank_.setTogglePrev(false);

  initialized_ = true;
}

void ManualMode::reset() {
  initialized_ = false;
  customStateIndex_ = 0;
  nextLongHandled_ = prevLongHandled_ = false;
  lastNextChange_ = lastPrevChange_ = 0;
  nextPressStart_ = prevPressStart_ = 0;
  lastNextRaw_ = lastPrevRaw_ = false;
  toggleNextState_ = togglePrevState_ = false;
  bank_.setToggleNext(false);
  bank_.setTogglePrev(false);
  webStopState(); // por si estaba activo el latch
}

// ----------- Web latch ----------
void ManualMode::attachFlowIsr_() {
  if (pinFlow1_>=0) { pinMode(pinFlow1_, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(pinFlow1_), isrFlow1_, RISING); }
  if (pinFlow2_>=0) { pinMode(pinFlow2_, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(pinFlow2_), isrFlow2_, RISING); }
}
void ManualMode::detachFlowIsr_() {
  if (pinFlow1_>=0) detachInterrupt(digitalPinToInterrupt(pinFlow1_));
  if (pinFlow2_>=0) detachInterrupt(digitalPinToInterrupt(pinFlow2_));
}

void ManualMode::applyRelayState_(const RelayState& rs) {
  // always/always12 (activo-alto en tu HW)
  digitalWrite(PIN_ALWAYS_ON_,    rs.alwaysOn  ? HIGH : LOW);
  digitalWrite(PIN_ALWAYS_ON_12_, rs.alwaysOn12? HIGH : LOW);

  // mains
  for (int i=0;i<NUM_MAINS_;++i) {
    bool on = (rs.mainsMask & (1u<<i)) != 0;
    digitalWrite(MAIN_PINS_[i], MAIN_AL_[i] ? (on?LOW:HIGH) : (on?HIGH:LOW));
  }
  // secs
  for (int j=0;j<NUM_SECS_;++j) {
    bool on = (rs.secsMask & (1u<<j)) != 0;
    digitalWrite(SEC_PINS_[j], SEC_AL_[j] ? (on?LOW:HIGH) : (on?HIGH:LOW));
  }
}

void ManualMode::webStartState(const RelayState& rs) {
  if (!initialized_) begin();

  webState_ = rs;
  applyRelayState_(webState_);

  noInterrupts(); pulses1_=pulses2_=0; lastUs1_=lastUs2_=micros(); interrupts();
  webStartMs_ = millis();

  attachFlowIsr_();
  webActive_ = true;
}

void ManualMode::webStopState() {
  if (!webActive_) return;
  detachFlowIsr_();
  // No forzamos apagar mains/secs aquí (queremos un STOP “limpio”).
  webActive_ = false;
}

// ---------------- run ----------------
void ManualMode::run() {
  if (!initialized_) { begin(); return; }

  if (webActive_) {
    // Latch: no hacemos nada salvo mantener el estado aplicado
    // (El caudal se acumula por ISR si lo necesitas para UI)
    return;
  }

  // ===== comportamiento con botones (igual al tuyo) =====
  const unsigned long now = millis();
  const bool nextRaw = (digitalRead(pins_.pinNext) == LOW);
  const bool prevRaw = (digitalRead(pins_.pinPrev) == HIGH);

  if (nextRaw) {
    if (nextPressStart_==0) nextPressStart_=now;
    if (!nextLongHandled_ && (now - nextPressStart_) >= longPressMs_) {
      toggleNextState_ = !toggleNextState_; bank_.setToggleNext(toggleNextState_); nextLongHandled_=true;
    }
  } else { nextPressStart_=0; nextLongHandled_=false; }

  if (prevRaw) {
    if (prevPressStart_==0) prevPressStart_=now;
    if (!prevLongHandled_ && (now - prevPressStart_) >= longPressMs_) {
      togglePrevState_ = !togglePrevState_; bank_.setTogglePrev(togglePrevState_); prevLongHandled_=true;
    }
  } else { prevPressStart_=0; prevLongHandled_=false; }

  if (nextRaw != lastNextRaw_ && (now - lastNextChange_) > debounceMs_) {
    lastNextRaw_ = nextRaw; lastNextChange_=now;
    if (nextRaw && customStates_ && numCustomStates_>0) {
      customStateIndex_ = (customStateIndex_ + 1) % numCustomStates_;
      smoothTransitionTo(customStates_[customStateIndex_]);
    }
  }
  if (prevRaw != lastPrevRaw_ && (now - lastPrevChange_) > debounceMs_) {
    lastPrevRaw_ = prevRaw; lastPrevChange_=now;
    if (prevRaw && customStates_ && numCustomStates_>0) {
      customStateIndex_ = (customStateIndex_ - 1 + numCustomStates_) % numCustomStates_;
      smoothTransitionTo(customStates_[customStateIndex_]);
    }
  }
}

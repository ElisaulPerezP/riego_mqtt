#include "modes/ManualMode.h"

// ====== Definiciones de los arrays estáticos (resuelven el link error) ======
const uint8_t ManualMode::MAIN_PINS_[12] = { 5, 18, 0, 21, 17, 3, 2, 22, 16, 1, 15, 23 };
const bool    ManualMode::MAIN_AL_[12]   = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }; // activo-bajo
const uint8_t ManualMode::SEC_PINS_[2]   = { 27, 26 };
const bool    ManualMode::SEC_AL_[2]     = { 0,  0  }; // activo-alto

// ------------------- ctor -------------------
ManualMode::ManualMode(RelayBank& bank,
                       const Pins& pins,
                       const int* customStates,
                       int numStates,
                       unsigned long debounceMs,
                       unsigned long longPressMs,
                       unsigned long stepDelayMs)
: bank_(bank),
  pins_(pins),
  customStates_(customStates),
  numCustomStates_(numStates),
  debounceMs_(debounceMs),
  longPressMs_(longPressMs),
  stepDelayMs_(stepDelayMs)
{}

// ------------------- helpers -------------------
void ManualMode::allMainsOff() {
  for (int i = 0; i < NUM_MAINS_; ++i) {
    digitalWrite(MAIN_PINS_[i], MAIN_AL_[i] ? HIGH : LOW);
  }
}
void ManualMode::allSecsOff() {
  for (int j = 0; j < NUM_SECS_; ++j) {
    digitalWrite(SEC_PINS_[j], SEC_AL_[j] ? HIGH : LOW);
  }
}
void ManualMode::applyMainsPattern(int m, bool direct) {
  for (int i = 0; i < NUM_MAINS_; ++i) {
    bool on = direct ? (i == m) : (i != m);
    digitalWrite(MAIN_PINS_[i],
      MAIN_AL_[i] ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
  }
}
void ManualMode::smoothTransitionTo(int idx) {
  const int OFF_IDX = NUM_MAINS_ * 2;

  // 1) apaga todo
  allMainsOff();
  allSecsOff();
  digitalWrite(PIN_ALWAYS_ON_,    LOW);
  digitalWrite(PIN_ALWAYS_ON_12_, LOW);
  delay(stepDelayMs_);

  if (idx == OFF_IDX) return;

  // 2) habilita banco + principal
  digitalWrite(PIN_ALWAYS_ON_,    HIGH);
  digitalWrite(PIN_ALWAYS_ON_12_, HIGH);
  delay(stepDelayMs_);

  // 3) MAIN
  int  m      = idx / 2;
  bool direct = ((idx & 1) == 0);
  if (direct) {
    digitalWrite(MAIN_PINS_[m], MAIN_AL_[m] ? LOW : HIGH);
  } else {
    applyMainsPattern(m, false);
  }
  delay(stepDelayMs_);

  // 4) SEC
  if (direct) {
    digitalWrite(SEC_PINS_[1], SEC_AL_[1] ? LOW : HIGH);
  } else {
    digitalWrite(SEC_PINS_[0], SEC_AL_[0] ? LOW : HIGH);
  }
  delay(stepDelayMs_);
}

// ------------------- ciclo de vida -------------------
void ManualMode::begin() {
  // Ventana segura al arranque
  for (int i = 0; i < NUM_MAINS_; ++i) pinMode(MAIN_PINS_[i], INPUT_PULLUP);
  for (int j = 0; j < NUM_SECS_;  ++j) pinMode(SEC_PINS_[j],  INPUT_PULLUP);
  pinMode(PIN_ALWAYS_ON_,    INPUT_PULLUP);
  pinMode(PIN_ALWAYS_ON_12_, INPUT_PULLUP);
  delay(SAFE_BOOT_MS_);

  // Configura salidas
  for (int i = 0; i < NUM_MAINS_; ++i) pinMode(MAIN_PINS_[i], OUTPUT);
  for (int j = 0; j < NUM_SECS_;  ++j) pinMode(SEC_PINS_[j],  OUTPUT);
  pinMode(PIN_ALWAYS_ON_,    OUTPUT);
  pinMode(PIN_ALWAYS_ON_12_, OUTPUT);

  // Botones
  pinMode(pins_.pinNext, INPUT_PULLUP);   // activo LOW
  pinMode(pins_.pinPrev, INPUT_PULLDOWN); // activo HIGH

  // Estado seguro
  allMainsOff();
  allSecsOff();
  digitalWrite(PIN_ALWAYS_ON_,    LOW);
  digitalWrite(PIN_ALWAYS_ON_12_, LOW);

  // Primer estado
  customStateIndex_ = 0;
  if (customStates_ && numCustomStates_ > 0) {
    smoothTransitionTo(customStates_[customStateIndex_]);
  }

  // toggles a falso
  toggleNextState_ = togglePrevState_ = false;
  bank_.setToggleNext(false);
  bank_.setTogglePrev(false);

  initialized_         = true;
  lastNextRaw_         = false;
  lastPrevRaw_         = false;
  nextLongHandled_     = false;
  prevLongHandled_     = false;
  lastNextChange_      = 0;
  lastPrevChange_      = 0;
  nextPressStart_      = 0;
  prevPressStart_      = 0;
}

void ManualMode::reset() {
  initialized_         = false;
  customStateIndex_    = 0;
  nextLongHandled_     = false;
  prevLongHandled_     = false;
  lastNextChange_      = 0;
  lastPrevChange_      = 0;
  nextPressStart_      = 0;
  prevPressStart_      = 0;
  lastNextRaw_         = false;
  lastPrevRaw_         = false;
  toggleNextState_     = false;
  togglePrevState_     = false;

  bank_.setToggleNext(false);
  bank_.setTogglePrev(false);
}

void ManualMode::run() {
  if (!initialized_) { begin(); return; }

  const unsigned long now = millis();

  const bool nextRaw = (digitalRead(pins_.pinNext) == LOW);
  const bool prevRaw = (digitalRead(pins_.pinPrev) == HIGH);

  // Pulsación larga NEXT -> toggleNext
  if (nextRaw) {
    if (nextPressStart_ == 0) nextPressStart_ = now;
    if (!nextLongHandled_ && (now - nextPressStart_) >= longPressMs_) {
      toggleNextState_ = !toggleNextState_;
      bank_.setToggleNext(toggleNextState_);
      nextLongHandled_ = true;
    }
  } else {
    nextPressStart_  = 0;
    nextLongHandled_ = false;
  }

  // Pulsación larga PREV -> togglePrev
  if (prevRaw) {
    if (prevPressStart_ == 0) prevPressStart_ = now;
    if (!prevLongHandled_ && (now - prevPressStart_) >= longPressMs_) {
      togglePrevState_ = !togglePrevState_;
      bank_.setTogglePrev(togglePrevState_);
      prevLongHandled_ = true;
    }
  } else {
    prevPressStart_  = 0;
    prevLongHandled_ = false;
  }

  // NEXT corto (debounce) -> siguiente estado
  if (nextRaw != lastNextRaw_ && (now - lastNextChange_) > debounceMs_) {
    lastNextRaw_    = nextRaw;
    lastNextChange_ = now;
    if (nextRaw && customStates_ && numCustomStates_ > 0) {
      customStateIndex_ = (customStateIndex_ + 1) % numCustomStates_;
      smoothTransitionTo(customStates_[customStateIndex_]);
    }
  }

  // PREV corto (debounce) -> estado previo
  if (prevRaw != lastPrevRaw_ && (now - lastPrevChange_) > debounceMs_) {
    lastPrevRaw_    = prevRaw;
    lastPrevChange_ = now;
    if (prevRaw && customStates_ && numCustomStates_ > 0) {
      customStateIndex_ = (customStateIndex_ - 1 + numCustomStates_) % numCustomStates_;
      smoothTransitionTo(customStates_[customStateIndex_]);
    }
  }
}

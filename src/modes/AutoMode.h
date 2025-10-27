#pragma once
#include <Arduino.h>
#include <vector>
#include <time.h>
#include <functional>

#include "../hw/RelayBank.h"
#include "IMode.h"
#include "../schedule/IrrigationSchedule.h"

// ======================= AutoMode con StepSets + escalados por horario =======================
// - Si program.sets.size()>0: cada StartSpec elige el StepSet (stepSetIndex) y sus escalas.
// - Si no hay sets, se sigue comportando como el "blink" legacy con customStates.

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

  struct IrrigationOrder {
    int  mainIndex = -1;
    bool direct    = true;
    unsigned long durationMs = 0;
    unsigned long volumeMl   = 0;
  };
  void enqueueOrder(const IrrigationOrder& ord);

  void setSchedule(const ProgramSpec* prog, const FlowCalibration* cal);

  struct Tele {
    bool     running = false;        // RUN_STEP o PAUSE
    bool     pausing = false;        // en pausa entre pasos
    bool     timeSynced = false;     // NTP ok

    int      stepIndex = -1;         // índice de paso actual
    int      stateIndex = -1;        // alias (compat)
    int      mainIndex  = -1;        // idx/2
    bool     direct     = true;      // (idx&1)==0

    uint32_t stateElapsedMs  = 0;
    uint32_t stateDurationMs = 0;    // límite tiempo (ya escalado por horario)
    uint32_t stateVolumeMl   = 0;    // mL del paso (delta)
    uint32_t stateTargetMl   = 0;    // objetivo mL (ya escalado por horario)
    uint32_t runVolumeMl     = 0;    // acumulado run

    uint32_t pulses1 = 0;
    uint32_t pulses2 = 0;

    bool     programEnabled = false;
    uint32_t nextStartEpoch = 0;
  };
  Tele telemetry() const;

  // ====== Callbacks para publicar eventos y resolver nombres ======
  using EventPublisher     = std::function<void(const String& topic, const String& payload)>;
  using StateNameResolver  = std::function<String(int stepIdx)>; // stepIdx dentro del set activo

  void setEventPublisher(EventPublisher pub, const String& topic);
  void setStateNameResolver(StateNameResolver res);

private:
  // ISR caudal
  static void IRAM_ATTR isrFlow1Thunk();
  static void IRAM_ATTR isrFlow2Thunk();

  // Actuación
  void smoothTransition(int idx);
  void allOff_();

  // Legacy blink
  void handlePhaseLogic();

  // Programado
  void runScheduled();
  bool timeNow(struct tm& out) const;
  bool todayMatches(uint8_t dowMask, int wday) const;
  int  shouldStartNow(const struct tm& nowTm); // devuelve índice del StartSpec matcheado o -1
  void startProgramForStart(size_t startIdx);  // inicia usando sets + escalas
  void stopProgram();
  void beginStep_(size_t idx);
  void finishStep_();
  uint32_t msSince(uint32_t t0) const;
  bool waitForValidIP(uint32_t timeoutMs = 7000) const;

  // util volumen/epoch
  uint32_t volumeMlFromPulses_(uint32_t d1, uint32_t d2) const;
  uint32_t computeNextStartEpoch_() const;

  // ====== Franjas (/windows en NVS) ======
  bool allowedNowByWindows_() const;   // true si hora actual cae en alguna franja válida

  // ====== helpers para ventana + timestamps + JSON ======
  bool   computeCurrentWindow_(int& sminOut, int& eminOut, time_t& startEpochOut, time_t& endEpochOut, String& nameOut) const;
  static String two_(int v);
  static String hhmm_(int minuteOfDay);
  static String isoLocal_(time_t epoch);
  static String jsonEscape_(const String& s);

  void   publishStateStart_(size_t stepIdx, uint32_t durMsTarget, uint32_t volMlTarget);
  void   publishStateEnd_  (size_t stepIdx, uint32_t durMsReal,    uint32_t volMlReal);

  // ====== NVS zonas (targets efectivos por zona) ======
  static bool readZoneTargetsNVS_(int zoneIdx, uint32_t& volMlOut, uint32_t& timeMsOut);

private:
  // Dependencias
  RelayBank&      bank_;
  const int*      customStates_;
  const int       numStates_;
  const unsigned long stateDurMs_;
  const unsigned long offDurationMs_;
  const unsigned long stepDelayMs_;
  const int       pinFlow1_;
  const int       pinFlow2_;

  // ISR state
  volatile static unsigned long pulse1_;
  volatile static unsigned long pulse2_;
  volatile static unsigned long lastMicros1_;
  volatile static unsigned long lastMicros2_;
  static const unsigned long DEBOUNCE_US = 5000UL;

  // Legacy blink
  bool      initialized_   = false;
  bool      activePhase_   = true;
  int       stateIndex_    = 0;
  uint32_t  phaseStart_    = 0;
  uint32_t  stateStart_    = 0;
  unsigned long pulseCount_ = 0;

  std::vector<IrrigationOrder> queue_;

  // Programación
  const ProgramSpec*   prog_   = nullptr;
  FlowCalibration      cal_    {};
  enum class Phase { IDLE, RUN_STEP, PAUSE } phase_ = Phase::IDLE;

  size_t     stepIdx_       = 0;    // índice paso dentro del set activo
  uint32_t   stepStartMs_   = 0;
  uint32_t   pauseStartMs_  = 0;
  unsigned long stepStartP1_ = 0;
  unsigned long stepStartP2_ = 0;
  uint32_t   runVolumeMl_   = 0;

  // “contexto” del arranque actual
  int        curStartIdx_   = -1;   // StartSpec elegido
  int        curSetIdx_     = -1;   // StepSet elegido por StartSpec
  float      timeScale_     = 1.0f; // escalas de ese Start
  float      volScale_      = 1.0f;

  // Anti-redoble
  int lastStartYDay_ = -1;
  int lastStartMin_  = -1;

  // Publicación y nombres
  EventPublisher    publisher_  = nullptr;
  String            pubTopic_   = "public/riegoArandanosDeMiPueblo";
  StateNameResolver nameRes_    = nullptr;

  // Ventana actual (caché para mensajes)
  bool   haveCurWindow_         = false;
  time_t curWindowStartEpoch_   = 0;
  time_t curWindowEndEpoch_     = 0;
  String curWindowName_;

  // ====== Targets efectivos (lo que realmente se usa) ======
  uint32_t effDurMs_ = 0;   // tiempo objetivo del paso (zona) en ms; 0 = sin límite
  uint32_t effVolMl_ = 0;   // volumen objetivo del paso (zona) en mL; 0 = sin límite
};

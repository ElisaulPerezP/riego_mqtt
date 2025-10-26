#include "modes.h"
#include "ManualMode.h"
#include "AutoMode.h"
#include "../hw/PinMap.h"
#include "../hw/RelayBank.h"
#include "../schedule/IrrigationSchedule.h"
#include "../state/RelayState.h"
#include <vector>

// -------------------- Pines/const originales --------------------
namespace FULL {
  enum {
    // Pulsadores con pull-down -> activo HIGH
    PIN_NEXT              = 13,  // activo HIGH
    PIN_PREV              = 4,   // activo HIGH

    PIN_ALWAYS_ON         = 33,
    PIN_ALWAYS_ON_12      = 12,

    // Auxiliares según README: 25=Fert1 (AH), 14=Alarma (AH)
    PIN_TOGGLE_NEXT       = 25,
    PIN_TOGGLE_PREV       = 14
  };

  // === Orden/AL EXACTO del README ===
  static const int  MAIN_PINS[]       = { 0, 2, 5, 15, 16, 17, 1, 3, 18, 21, 22, 23 };
  static const bool MAIN_ACTIVE_LOW[] = { 1, 1, 1,  1,  1,  1, 1, 1,  1,  1,  1,  1 };
  static const int  NUM_MAINS         = sizeof(MAIN_PINS)/sizeof(MAIN_PINS[0]);

  static const int  SEC_PINS[]        = { 26, 27 };     // (AH)
  static const bool SEC_ACTIVE_LOW[]  = { 0, 0 };
  static const int  NUM_SECS          = sizeof(SEC_PINS)/sizeof(SEC_PINS[0]);

  // Mantengo tu tabla de estados personalizada (índices [0..NUM_MAINS*2])
  static const int CUSTOM_STATES[] = {
     15,17,3,1,11,9,7,13,19,5,23,21,
      8,12,16,
      NUM_MAINS*2
  };
  static const int NUM_CUSTOM_STATES = sizeof(CUSTOM_STATES)/sizeof(CUSTOM_STATES[0]);

  static const unsigned long DEBOUNCE_MS   = 100;
  static const unsigned long LONGPRESS_MS  = 5000;
  static const unsigned long STEP_MS       = 500;
}

namespace BLINK {
  enum {
    PIN_ALWAYS_ON        = 33,
    PIN_ALWAYS_ON_12     = 12,
    PIN_TOGGLE_NEXT      = 25,   // Fert1
    PIN_TOGGLE_PREV      = 14,   // Alarma
    PIN_OVR_NEXT         = 13,   // activo HIGH (pull-down)
    PIN_OVR_PREV         = 4     // activo HIGH (pull-down)
  };

  // Uso mismo orden que README
  static const int  MAIN_PINS[]       = { 0, 2, 5, 15, 16, 17, 1, 3, 18, 21, 22, 23 };
  static const bool MAIN_ACTIVE_LOW[] = { 1, 1, 1,  1,  1,  1, 1, 1,  1,  1,  1,  1 };
  static const int  SEC_PINS[]        = { 26, 27 };
  static const bool SEC_ACTIVE_LOW[]  = { 0, 0 };

  static const int CUSTOM_STATES[] = { 15,17,3,1,11,9,7,13,19,5,23,21,8,12,16 };
  static const int NUM_CUSTOM_STATES = sizeof(CUSTOM_STATES)/sizeof(CUSTOM_STATES[0]);

  static const unsigned long STATE_MS = 5UL * 60UL * 1000UL;
  static const unsigned long OFF_MS   = 4UL * 60UL * 60UL * 1000UL;
  static const unsigned long STEP_MS  = 500UL;

  static const int PIN_FLOW_1 = 34;  // pull-up externo
  static const int PIN_FLOW_2 = 35;  // pull-up externo
}

// -------------------- Objetos --------------------
static const PinMap pinmapFull = {
  FULL::MAIN_PINS, FULL::MAIN_ACTIVE_LOW, (int)(sizeof(FULL::MAIN_PINS)/sizeof(int)),
  FULL::SEC_PINS,  FULL::SEC_ACTIVE_LOW,  (int)(sizeof(FULL::SEC_PINS)/sizeof(int)),
  FULL::PIN_ALWAYS_ON, FULL::PIN_ALWAYS_ON_12,
  FULL::PIN_TOGGLE_NEXT, FULL::PIN_TOGGLE_PREV
};
static RelayBank relayFull(pinmapFull);

static ManualMode::Pins fullPins = { FULL::PIN_NEXT, FULL::PIN_PREV };
static ManualMode manualMode(relayFull, fullPins,
                             FULL::CUSTOM_STATES, FULL::NUM_CUSTOM_STATES,
                             FULL::DEBOUNCE_MS, FULL::LONGPRESS_MS, FULL::STEP_MS);

static const PinMap pinmapBlink = {
  BLINK::MAIN_PINS, BLINK::MAIN_ACTIVE_LOW, (int)(sizeof(BLINK::MAIN_PINS)/sizeof(int)),
  BLINK::SEC_PINS,  BLINK::SEC_ACTIVE_LOW,  2,
  BLINK::PIN_ALWAYS_ON, BLINK::PIN_ALWAYS_ON_12,
  BLINK::PIN_TOGGLE_NEXT, BLINK::PIN_TOGGLE_PREV
};
static RelayBank relayBlink(pinmapBlink);

static AutoMode autoMode(relayBlink,
                         BLINK::CUSTOM_STATES, BLINK::NUM_CUSTOM_STATES,
                         BLINK::STATE_MS, BLINK::OFF_MS, BLINK::STEP_MS,
                         BLINK::PIN_FLOW_1, BLINK::PIN_FLOW_2);

// Programa/cali “vivos” dentro de este módulo (que AutoMode referenciará)
static ProgramSpec     gProg;
static FlowCalibration gCal;
static bool            gProgInit = false;

// Por si el loop arranca antes de que el main le pase algo sensato
static void ensureProgramInit() {
  if (gProgInit) return;
  gProg.enabled = true;

  StepSet set0;
  set0.name = "Default";
  set0.pauseMsBetweenSteps = 10000; // 10s
  for (int i=0;i<BLINK::NUM_CUSTOM_STATES;i++) {
    set0.steps.push_back( StepSpec{ BLINK::CUSTOM_STATES[i], BLINK::STATE_MS, 0 } );
  }
  gProg.sets.clear();
  gProg.sets.push_back(set0);

  uint8_t everyday = (uint8_t)(DOW_MON|DOW_TUE|DOW_WED|DOW_THU|DOW_FRI|DOW_SAT|DOW_SUN);
  gProg.starts.clear();
  gProg.starts.push_back( StartSpec(5, 0, everyday, 0, true, 0.80f, 0.70f) );
  gProg.starts.push_back( StartSpec(17,30, everyday, 0, true, 1.00f, 1.00f) );

  gCal.pulsesPerMl1 = 4.5f;
  gCal.pulsesPerMl2 = 4.5f;

  autoMode.setSchedule(&gProg, &gCal);
  gProgInit = true;
}

// -------------------- Fachada C-like --------------------
void resetFullMode()  { manualMode.reset(); }
void runFullMode()    { manualMode.run();   }

void resetBlinkMode() { autoMode.reset();   }
void runBlinkMode()   { ensureProgramInit(); autoMode.run(); }

AutoMode::Tele getAutoTelemetry() { return autoMode.telemetry(); }

// El main llama esto tras guardar/editar en WebUI
void modesSetProgram(const ProgramSpec& p, const FlowCalibration& c) {
  gProg = p;             // copiamos (vive dentro de este módulo)
  gCal  = c;
  gProgInit = true;      // ya tenemos un programa real
  autoMode.setSchedule(&gProg, &gCal);
}

// -------------------- Control manual desde Web (latch de RelayState) --------------------
void manualWeb_startState(const RelayState& rs) { manualMode.webStartState(rs); }
void manualWeb_stopState()                      { manualMode.webStopState();    }
bool manualWeb_isActive()                       { return manualMode.webIsActive(); }

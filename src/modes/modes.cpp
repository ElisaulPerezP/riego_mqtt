#include "modes.h"
#include "ManualMode.h"
#include "AutoMode.h"
#include "../hw/PinMap.h"
#include "../hw/RelayBank.h"

// -------------------- Pines/const de tu implementación original --------------------
// FULL
namespace FULL {
  enum {
    PIN_NEXT              = 13,  // activo LOW
    PIN_PREV              = 4,   // activo HIGH
    PIN_ALWAYS_ON         = 33,
    PIN_ALWAYS_ON_12      = 12,
    PIN_TOGGLE_NEXT       = 25,
    PIN_TOGGLE_PREV       = 14
  };
  static const int  MAIN_PINS[]       = {5,18,0,21,17,3,2,22,16,1,15,23};
  static const bool MAIN_ACTIVE_LOW[] = {true,true,true,true,true,true,true,true,true,true,true,true};
  static const int  NUM_MAINS         = sizeof(MAIN_PINS)/sizeof(MAIN_PINS[0]);

  static const int  SEC_PINS[]        = {27,26};
  static const bool SEC_ACTIVE_LOW[]  = {false,false};
  static const int  NUM_SECS          = sizeof(SEC_PINS)/sizeof(SEC_PINS[0]);

  // Secuencia personalizada FULL
  static const int CUSTOM_STATES[] = {
     15,17,3,1,11,9,7,13,19,5,23,21,  // "b" states
      8,12,16,                        // "a" states
      NUM_MAINS*2                     // OFF simbólico al final (no usado explícito)
  };
  static const int NUM_CUSTOM_STATES = sizeof(CUSTOM_STATES)/sizeof(CUSTOM_STATES[0]);

  static const unsigned long DEBOUNCE_MS   = 100;
  static const unsigned long LONGPRESS_MS  = 5000;
  static const unsigned long STEP_MS       = 500;
}

// BLINK
namespace BLINK {
  enum {
    PIN_ALWAYS_ON        = 33,
    PIN_ALWAYS_ON_12     = 12,
    PIN_TOGGLE_NEXT      = 25,
    PIN_TOGGLE_PREV      = 14,
    PIN_OVR_NEXT         = 13, // override manual (LOW)
    PIN_OVR_PREV         = 4   // override manual (HIGH)
  };
  static const int  MAIN_PINS[]       = {5,18,0,21,17,3,2,22,16,1,15,23};
  static const bool MAIN_ACTIVE_LOW[] = {true,true,true,true,true,true,true,true,true,true,true,true};
  static const int  SEC_PINS[]        = {26,27};
  static const bool SEC_ACTIVE_LOW[]  = {false,false};

  static const int CUSTOM_STATES[] = {15,17,3,1,11,9,7,13,19,5,23,21,8,12,16};
  static const int NUM_CUSTOM_STATES = sizeof(CUSTOM_STATES)/sizeof(CUSTOM_STATES[0]);

  static const unsigned long STATE_MS = 5UL * 60UL * 1000UL;
  static const unsigned long OFF_MS   = 4UL * 60UL * 60UL * 1000UL;
  static const unsigned long STEP_MS  = 500UL;

  static const int PIN_FLOW_1 = 34;
  static const int PIN_FLOW_2 = 35;
}

// -------------------- Objetos estáticos compartidos --------------------
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

// -------------------- Fachada C-like --------------------
void resetFullMode()  { manualMode.reset(); }
void runFullMode()    { manualMode.run();   }

void resetBlinkMode() { autoMode.reset();   }
void runBlinkMode()   { autoMode.run();     }

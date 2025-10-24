// File: src/hw/RelayPins.h
#pragma once
#include <Arduino.h>

namespace RP {

// ========================== Pines de control del banco ==========================
static constexpr int PIN_ALWAYS_ON    = 33;
static constexpr int PIN_ALWAYS_ON_12 = 12;
static constexpr int PIN_TOGGLE_NEXT  = 25;
static constexpr int PIN_TOGGLE_PREV  = 14;

// ========================== Pines de caudalímetros ==========================
static constexpr int PIN_FLOW_1 = 34;
static constexpr int PIN_FLOW_2 = 35;

// ========================== Selector físico Manual/Auto ==========================
// HIGH = MANUAL, LOW = AUTO (tal y como lo tenías)
static constexpr int PIN_SWITCH_MANUAL = 19;

// ========================== Banco de relés: MAIN y SEC ==========================
// NOTA: Se respeta tu hardware original, incluyendo GPIO1/3 (UART0) como relés.
static const int  MAIN_PINS[]       = { 5, 18, 0, 21, 17, 3, 2, 22, 16, 1, 15, 23 };
static const bool MAIN_ACTIVE_LOW[] = { true,true,true,true,true,true,true,true,true,true,true,true };
static const int  NUM_MAINS         = sizeof(MAIN_PINS)/sizeof(MAIN_PINS[0]);

// Orden SEC como usabas en ManualMode por defecto: {27,26}
static const int  SEC_PINS[]        = { 27, 26 };
static const bool SEC_ACTIVE_LOW[]  = { false, false };
static const int  NUM_SECS          = sizeof(SEC_PINS)/sizeof(SEC_PINS[0]);

// ========================== Botones frontales Manual ==========================
static constexpr int PIN_NEXT = 13;  // activo LOW
static constexpr int PIN_PREV = 4;   // activo HIGH

} // namespace RP

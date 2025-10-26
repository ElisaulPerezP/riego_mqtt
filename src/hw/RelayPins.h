#pragma once
#include <Arduino.h>

namespace RP {

// ========================== Pines de control del banco ==========================
static constexpr int PIN_ALWAYS_ON    = 33;  // Master banco (AH)
static constexpr int PIN_ALWAYS_ON_12 = 12;  // Válvula principal (AH)

// Salidas auxiliares (según README)
static constexpr int PIN_TOGGLE_NEXT  = 25;  // Fertilizante 1 (AH)
static constexpr int PIN_TOGGLE_PREV  = 14;  // Alarma (AH)
static constexpr int PIN_AUX_FERT2    = 32;  // Fertilizante 2 (AH)

// ========================== Pines de caudalímetros / sensores ==================
static constexpr int PIN_FLOW_1  = 34; // Pull-up externo
static constexpr int PIN_FLOW_2  = 35; // Pull-up externo
static constexpr int PIN_FLOW_VP = 36; // Pull-up externo
static constexpr int PIN_FLOW_VN = 39; // Pull-up externo

// ========================== Selector físico Manual/Auto ========================
// HIGH = MANUAL, LOW = AUTO
static constexpr int PIN_SWITCH_MANUAL = 19;

// ========================== Banco de relés: MAIN y SEC =========================
// Orden y niveles exactamente como el README
// MAIN (negativa: LOW = ON)
static const int  MAIN_PINS[]       = { 0, 2, 5, 15, 16, 17, 1, 3, 18, 21, 22, 23 };
static const bool MAIN_ACTIVE_LOW[] = { 1, 1, 1,  1,  1,  1, 1, 1,  1,  1,  1,  1 };
static const int  NUM_MAINS         = sizeof(MAIN_PINS)/sizeof(MAIN_PINS[0]);

// SEC (positiva: HIGH = ON) -> {26,27}
static const int  SEC_PINS[]        = { 26, 27 };
static const bool SEC_ACTIVE_LOW[]  = { 0, 0 };
static const int  NUM_SECS          = sizeof(SEC_PINS)/sizeof(SEC_PINS[0]);

// ========================== Botones frontales ==========================
// README: pulsadores usan pull-down -> activo HIGH al presionar
static constexpr int PIN_NEXT = 13;  // Stop (NC en README); cableado con pull-down -> leer HIGH si presionado
static constexpr int PIN_PREV = 4;   // Botón verde (NO); pull-down -> leer HIGH si presionado

} // namespace RP

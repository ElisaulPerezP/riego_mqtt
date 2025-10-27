#pragma once
#include "AutoMode.h"
#include "../schedule/IrrigationSchedule.h"
#include "../state/RelayState.h"
#include <functional>

// ===== Telemetría de MODO MANUAL (para WebUI /mode) =====
struct ManualTelemetry {
  bool     active     = false;   // hay zona manual activa (por latch SW o por HW)
  uint32_t volumeMl   = 0;       // volumen entregado desde que se inició/cambió de zona
  uint32_t elapsedMs  = 0;       // tiempo transcurrido desde inicio/cambio de zona
  int      stateIndex = -1;      // índice de la zona/estado actual si aplica
};

// Auto
void resetFullMode();
void runFullMode();
void resetBlinkMode();
void runBlinkMode();
AutoMode::Tele getAutoTelemetry();
void modesSetProgram(const ProgramSpec& p, const FlowCalibration& c);

// Manual latch desde Web usando RelayState
void manualWeb_startState(const RelayState& rs);
void manualWeb_stopState();
bool manualWeb_isActive();

// ====== Callbacks hacia AutoMode ======
void modesSetEventPublisher(AutoMode::EventPublisher pub, const String& topic);
void modesSetStateNameResolver(AutoMode::StateNameResolver res);

// ====== Telemetría Manual (para /mode) ======
ManualTelemetry modesGetManualTelemetry();

#pragma once
#include "AutoMode.h"
#include "../schedule/IrrigationSchedule.h"
#include "../state/RelayState.h"

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

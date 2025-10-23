#pragma once
#include "AutoMode.h"
#include "../schedule/IrrigationSchedule.h"

void resetFullMode();
void runFullMode();

void resetBlinkMode();
void runBlinkMode();

AutoMode::Tele getAutoTelemetry();

// NUEVO: aplicar programa/calibración al AutoMode
void modesSetProgram(const ProgramSpec& p, const FlowCalibration& c);

// File: src/schedule/IrrigationSchedule.h
#pragma once
#include <Arduino.h>
#include <vector>

// Bitmask de días de semana (bit0=Lun ... bit6=Dom)
enum : uint8_t {
  DOW_MON = 1<<0, DOW_TUE = 1<<1, DOW_WED = 1<<2,
  DOW_THU = 1<<3, DOW_FRI = 1<<4, DOW_SAT = 1<<5, DOW_SUN = 1<<6
};

// Un paso/estado de riego
struct StepSpec {
  int      idx;            // estado (main*2 + (direct?0:1))
  uint32_t maxDurationMs;  // límite de tiempo (0=sin límite)
  uint32_t targetMl;       // volumen objetivo (0=sin objetivo)
};

// Conjunto de pasos reutilizable
struct StepSet {
  String name;                       // p.ej. "Default", "Verano"
  std::vector<StepSpec> steps;       // lista ordenada de pasos
  uint32_t pauseMsBetweenSteps = 0;  // reposo opcional entre pasos
};

// Horario que lanza un StepSet + escalas por horario
struct StartSpec {
  // Campos
  uint8_t hour        = 0;     // 0..23
  uint8_t minute      = 0;     // 0..59
  uint8_t dowMask     = 0;     // DOW_*
  uint8_t stepSetIndex= 0;     // índice de StepSet
  bool    enabled     = true;  // activo/inactivo
  float   timeScale   = 1.0f;  // escala de tiempos
  float   volumeScale = 1.0f;  // escala de volúmenes

  // Constructores para compatibilidad
  StartSpec() = default;
  StartSpec(uint8_t h, uint8_t m, uint8_t mask)
  : hour(h), minute(m), dowMask(mask), stepSetIndex(0), enabled(true),
    timeScale(1.0f), volumeScale(1.0f) {}

  StartSpec(uint8_t h, uint8_t m, uint8_t mask,
            uint8_t setIdx, bool en, float tscale, float vscale)
  : hour(h), minute(m), dowMask(mask), stepSetIndex(setIdx), enabled(en),
    timeScale(tscale), volumeScale(vscale) {}
};

// Programa de riego
struct ProgramSpec {
  bool enabled = true;
  std::vector<StepSet>  sets;    // catálogos de pasos
  std::vector<StartSpec> starts; // horarios que apuntan a un set
};

// Calibración de caudalímetros
struct FlowCalibration {
  float pulsesPerMl1 = 1.0f;
  float pulsesPerMl2 = 1.0f;
};

// Config global (opcional)
struct IrrigationConfig {
  String tz = "America/Bogota";  // zona horaria NTP
  ProgramSpec program;
  FlowCalibration flowCal;
};

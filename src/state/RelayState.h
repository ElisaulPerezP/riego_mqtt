// File: src/state/RelayState.h
#pragma once
#include <Arduino.h>

// Describe un estado de riego en términos de relés encendidos
// - name: nombre amigable del estado
// - mainsMask: bits de los relés "principales" (12 bits típicamente). Bit i=1 => MAIN i encendido
// - secsMask:  bits de los relés "secundarios" (2 bits típicamente). Bit j=1 => SEC j encendido
// - alwaysOn / alwaysOn12: si se deben activar las líneas de habilitación
struct RelayState {
  String   name;
  uint16_t mainsMask = 0;
  uint16_t secsMask  = 0;
  bool     alwaysOn  = false;
  bool     alwaysOn12= false;
};

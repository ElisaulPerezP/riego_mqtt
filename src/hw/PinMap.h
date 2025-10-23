#pragma once
#include <Arduino.h>

// Mapa de pines y polaridad para un banco de relés principal/secundario
struct PinMap {
  // Principales
  const int*  mainPins;
  const bool* mainActiveLow;
  int         numMains;

  // Secundarios
  const int*  secPins;
  const bool* secActiveLow;
  int         numSecs;

  // Pines “siempre activos” y toggles auxiliares
  int pinAlwaysOn;       // habilita banco
  int pinAlwaysOn12;     // válvula principal
  int pinToggleNext;     // salida auxiliar (NEXT long press)
  int pinTogglePrev;     // salida auxiliar (PREV long press)
};

#pragma once
#include <stdint.h>

// Inicializa NTP con TZ Bogotá y deja configurada la resincronización automática (1 h).
// Úsalo si quieres forzar la configuración en setup(); si no lo llamas,
// se configura igual automáticamente cuando el WiFi obtiene IP.
void beginTimeSync();

// Llamada opcional desde loop(); SNTP ya re-sincroniza solo cada hora,
// pero si quieres, puedes invocarla para forzar comprobaciones adicionales.
void timeSyncLoop();

// Info útil de la última sincronización
struct TimeSyncInfo {
  uint32_t lastSyncEpoch;   // epoch (seg) del último sync; 0 si nunca
  uint32_t lastSyncMillis;  // millis() del último sync; 0 si nunca
  bool     timeValid;       // true si el reloj está puesto (epoch > 2020)
};

// Consulta estado de sincronización
TimeSyncInfo getTimeSyncInfo();

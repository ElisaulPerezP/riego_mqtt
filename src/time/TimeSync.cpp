#include "time/TimeSync.h"   // << NECESARIO para TimeSyncInfo y prototipos

#include <WiFi.h>
#include <time.h>
#include <sys/time.h>

// ====== Compat de cabeceras SNTP según core/IDF ======
#if __has_include(<esp_sntp.h>)
  #include <esp_sntp.h>
#elif __has_include(<lwip/apps/sntp.h>)
  #include <lwip/apps/sntp.h>
#else
  #warning "No se encontró esp_sntp.h ni lwip/apps/sntp.h; sntp_set_* podría no estar disponible."
#endif

// Zona horaria: Bogotá (UTC-05, sin DST). POSIX TZ:
static const char* TZ_BOGOTA = "COT5";

// Servidores NTP (puedes cambiarlos si deseas)
static const char* NTP1 = "pool.ntp.org";
static const char* NTP2 = "time.nist.gov";
static const char* NTP3 = "time.google.com";

// Estado básico de sincronización
static volatile uint32_t g_lastSyncEpoch  = 0;
static volatile uint32_t g_lastSyncMillis = 0;

// Intervalo de resincronización (1 hora)
static const uint32_t SYNC_INTERVAL_MS = 3600000UL;

// ---------- Utilidades ----------
static bool timeIsValid_() {
  // Consideramos válido si > 2020-01-01
  return time(nullptr) > 1577836800; // 2020-01-01 00:00:00 UTC
}

static void configTimeBogota_() {
  // Idempotente: llamar varias veces es seguro.
  configTzTime(TZ_BOGOTA, NTP1, NTP2, NTP3);

  // Ajusta el intervalo de sondeo SNTP si está disponible
  #if defined(sntp_set_sync_interval)
    sntp_set_sync_interval(SYNC_INTERVAL_MS);
  #endif

  // Modo de sincronización "smooth" (si está disponible) para corregir desvíos suavemente
  #if defined(SNTP_SYNC_MODE_SMOOTH) && defined(sntp_set_sync_mode)
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
  #endif
}

// Callback de notificación cuando SNTP actualiza la hora
static void timeSyncCb_(struct timeval* tv) {
  (void)tv;
  g_lastSyncEpoch  = (uint32_t)time(nullptr);
  g_lastSyncMillis = millis();
}

// Al obtener IP, (re)configuramos NTP
static void onGotIP_(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    configTimeBogota_();
  }
}

// Registro automático de eventos/NTF al cargar el módulo
struct AutoTimeSync_ {
  AutoTimeSync_() {
    WiFi.onEvent(onGotIP_, ARDUINO_EVENT_WIFI_STA_GOT_IP);

    // Si ya hay WiFi, configura NTP de inmediato
    if (WiFi.status() == WL_CONNECTED) {
      configTimeBogota_();
    }

    // Registra callback de sincronización si está disponible
    #if defined(sntp_set_time_sync_notification_cb)
      sntp_set_time_sync_notification_cb(timeSyncCb_);
    #endif

    // (Opcional) habilita servidor por DHCP si el router provee, sin quitar los tuyos
    #if defined(sntp_servermode_dhcp)
      sntp_servermode_dhcp(true);
    #endif
  }
} _autoTimeSyncInstance;

// ----------------- API pública -----------------
void beginTimeSync() {
  configTimeBogota_();
}

void timeSyncLoop() {
  // No es estrictamente necesario: SNTP ya re-sincroniza cada hora.
  // Aquí solo reforzamos en caso de relojes no válidos o si algo deshabilitó SNTP.
  static uint32_t lastCheck = 0;
  const uint32_t nowMs = millis();
  if (nowMs - lastCheck < 10000UL) return; // comprobar cada 10 s sin molestar
  lastCheck = nowMs;

  if (WiFi.status() == WL_CONNECTED) {
    if (!timeIsValid_()) {
      // Si el reloj aún no es válido, vuelve a configurar NTP (idempotente)
      configTimeBogota_();
    } else {
      // Si ha pasado bastante sin callback, fuerza restart del cliente SNTP (si existe)
      #if defined(sntp_restart) && defined(sntp_set_sync_interval)
        if (g_lastSyncMillis > 0 && (nowMs - g_lastSyncMillis) > (SYNC_INTERVAL_MS + 300000UL)) { // +5 min tolerancia
          sntp_restart();
        }
      #endif
    }
  }
}

TimeSyncInfo getTimeSyncInfo() {
  TimeSyncInfo t;
  t.lastSyncEpoch  = g_lastSyncEpoch;
  t.lastSyncMillis = g_lastSyncMillis;
  t.timeValid      = timeIsValid_();
  return t;
}

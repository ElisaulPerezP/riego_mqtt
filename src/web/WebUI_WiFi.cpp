// File: src/web/WebUI_WiFi.cpp
#include "web/WebUI.h"
#include <WiFi.h>
#include <Preferences.h>
#include <vector>
#include <time.h>              // hora local
#include "../time/TimeSync.h"  // getTimeSyncInfo()
#include "hw/RelayPins.h"      // mapa de pines del proyecto

/* ================= Helpers GPIO (solo para Home) ================= */

static bool vecContains_(const std::vector<int>& v, int x){ for(int p: v) if(p==x) return true; return false; }

static bool mainActiveLowOf_(int pin, bool& outAL, int& outIdx) {
  for (int i=0;i<RP::NUM_MAINS;i++){
    if (RP::MAIN_PINS[i]==pin){ outAL = RP::MAIN_ACTIVE_LOW[i]; outIdx = i; return true; }
  }
  return false;
}
static bool secActiveLowOf_(int pin, bool& outAL, int& outIdx) {
  for (int j=0;j<RP::NUM_SECS;j++){
    if (RP::SEC_PINS[j]==pin){ outAL = RP::SEC_ACTIVE_LOW[j]; outIdx = j; return true; }
  }
  return false;
}

static String roleOf_(int pin, bool& hasAL, bool& al) {
  hasAL=false; al=false;
  int idx=-1;
  if (mainActiveLowOf_(pin, al, idx)) { hasAL=true; return String(F("MAIN["))+String(idx)+F("]"); }
  if (secActiveLowOf_(pin,  al, idx)) { hasAL=true; return String(F("SEC[" ))+String(idx)+F("]"); }
  if (pin==RP::PIN_ALWAYS_ON)    { hasAL=false; return F("ALWAYS_ON"); }
  if (pin==RP::PIN_ALWAYS_ON_12) { hasAL=false; return F("ALWAYS_ON_12"); }
  if (pin==RP::PIN_TOGGLE_NEXT)  { hasAL=false; return F("TOGGLE_NEXT"); }
  if (pin==RP::PIN_TOGGLE_PREV)  { hasAL=false; return F("TOGGLE_PREV"); }
  if (pin==RP::PIN_FLOW_1)       { hasAL=false; return F("FLOW_1"); }
  if (pin==RP::PIN_FLOW_2)       { hasAL=false; return F("FLOW_2"); }
  if (pin==RP::PIN_SWITCH_MANUAL){ hasAL=false; return F("SWITCH_MANUAL"); }
  if (pin==RP::PIN_NEXT)         { hasAL=false; return F("BTN_NEXT"); }
  if (pin==RP::PIN_PREV)         { hasAL=false; return F("BTN_PREV"); }
  return F("-");
}

static bool isFlashPin_(int pin) { return (pin>=6 && pin<=11); } // evitar pines del flash

static String interpFromAL_(int level, bool hasAL, bool al) {
  if (!hasAL) return F("—");
  bool on = al ? (level==LOW) : (level==HIGH);
  return on ? F("ON") : F("OFF");
}

// Dirección inferida por rol (sin driver IDF)
static String dirOf_(int pin) {
  bool dummyAL; int dummyIdx;
  if (mainActiveLowOf_(pin, dummyAL, dummyIdx)) return F("OUT");
  if (secActiveLowOf_(pin,  dummyAL, dummyIdx)) return F("OUT");
  if (pin==RP::PIN_ALWAYS_ON || pin==RP::PIN_ALWAYS_ON_12 ||
      pin==RP::PIN_TOGGLE_NEXT || pin==RP::PIN_TOGGLE_PREV) return F("OUT");
  if (pin==RP::PIN_FLOW_1 || pin==RP::PIN_FLOW_2 ||
      pin==RP::PIN_SWITCH_MANUAL || pin==RP::PIN_NEXT || pin==RP::PIN_PREV) return F("IN");
  return F("—");
}

// Formatea “hace Xh Ym Zs” a partir de milisegundos
static String fmtSinceMs_(uint32_t ms) {
  uint32_t s = ms / 1000;
  uint32_t h = s / 3600;
  uint32_t m = (s % 3600) / 60;
  uint32_t ss = s % 60;
  String out;
  if (h) { out += String(h) + F("h "); }
  if (h || m) { out += String(m) + F("m "); }
  out += String(ss) + F("s");
  return out;
}

// Asegura que los pines ENTRADA tengan pulls correctos para LEER en Home
static void ensureInputReadConfig_() {
  if (RP::PIN_SWITCH_MANUAL >= 0) pinMode(RP::PIN_SWITCH_MANUAL, INPUT_PULLDOWN);
  if (RP::PIN_NEXT          >= 0) pinMode(RP::PIN_NEXT,          INPUT_PULLUP);
  if (RP::PIN_PREV          >= 0) pinMode(RP::PIN_PREV,          INPUT_PULLDOWN);
  // Caudalímetros (pull-up externo): INPUT “flotante” sin PULL interno
  if (RP::PIN_FLOW_1        >= 0) pinMode(RP::PIN_FLOW_1,        INPUT);
  if (RP::PIN_FLOW_2        >= 0) pinMode(RP::PIN_FLOW_2,        INPUT);
}

/* ====================== HOME ====================== */
void WebUI::handleRoot() {
  String s = htmlHeader(F("Home"));
  s += F("<p>Este servidor reemplaza el menú por Serial. Conéctate al AP <b>config</b> (pass <code>password</code>) y abre <a href='http://config.local/'>config.local</a>.</p>");
  s += F("<ul><li><a href='/states'>Estados (ver/editar/agregar)</a></li>");
  s += F("<li><a href='/mode'>Modo (Manual/Automático)</a></li>");
  s += F("<li><a href='/wifi/info'>Estado Wi-Fi</a></li>");
  s += F("<li><a href='/wifi/scan'>Escanear y conectar</a></li>");
  s += F("<li><a href='/wifi/saved'>Redes guardadas / autoconexión</a></li>");
  s += F("<li><a href='/mqtt'>MQTT (config, estado, chat)</a></li></ul>");

  // Configura entradas antes de leer el switch físico
  ensureInputReadConfig_();

  // ====== Bloque: Estado de modo + Hora local + Próxima ventana ======
  {
    // --- Modo desde NVS (override/software) ---
    bool ovr=false, manual=false;
    {
      Preferences p;
      if (p.begin(NS_MODE, /*ro*/ true)) {
        ovr    = p.getUChar("ovr", 0) != 0;
        manual = p.getUChar("manual", 0) != 0;
        p.end();
      }
    }
    // --- Modo desde hardware (switch físico): HIGH=MANUAL, LOW=AUTO ---
    bool hwManual = false;
    if (RP::PIN_SWITCH_MANUAL >= 0) hwManual = (digitalRead(RP::PIN_SWITCH_MANUAL) == HIGH);

    // --- Modo efectivo ---
    bool effManual = ovr ? manual : hwManual;

    // --- Hora local (Bogotá configurada por TimeSync) ---
    time_t nowEpoch = time(nullptr);
    struct tm lt;
    bool timeValid = (nowEpoch > 100000) && localtime_r(&nowEpoch, &lt);

    char nowBuf[32] = "-";
    if (timeValid) {
      strftime(nowBuf, sizeof(nowBuf), "%Y-%m-%d %H:%M:%S", &lt);
    }

    // --- Info de sincronización (hace cuánto) ---
    TimeSyncInfo tsi = getTimeSyncInfo();
    String lastSyncTxt = F("—");
    if (tsi.lastSyncMillis != 0) {
      uint32_t age = millis() - tsi.lastSyncMillis;
      lastSyncTxt = fmtSinceMs_(age) + F(" atrás");
    } else {
      lastSyncTxt = F("nunca");
    }

    // --- Ventanas desde NVS (NS_WINDOWS="windows") ---
    struct Win { uint8_t sh, sm, eh, em; };
    std::vector<Win> wins;
    {
      Preferences p;
      if (p.begin(NS_WINDOWS, /*ro*/ true)) {
        uint8_t cnt = p.getUChar("count", 0);
        for (uint8_t i = 0; i < cnt && i < 60; ++i) {
          Win w;
          w.sh = p.getUChar((String("w")+i+"_sh").c_str(), 0);
          w.sm = p.getUChar((String("w")+i+"_sm").c_str(), 0);
          w.eh = p.getUChar((String("w")+i+"_eh").c_str(), 0);
          w.em = p.getUChar((String("w")+i+"_em").c_str(), 0);
          int smin = (int)w.sh*60 + (int)w.sm;
          int emin = (int)w.eh*60 + (int)w.em;
          if (smin < emin) wins.push_back(w); // solo rangos válidos en el mismo día
        }
        p.end();
      }
    }

    String nextText = F("Sin franjas configuradas");
    if (timeValid && !wins.empty()) {
      int md = lt.tm_hour * 60 + lt.tm_min;  // minuto del día
      bool inside = false;
      int  curEnd = 1e9;
      int  nextStart = 1e9;
      int  firstStart = 1e9;

      for (const auto& w : wins) {
        int smin = (int)w.sh*60 + (int)w.sm;
        int emin = (int)w.eh*60 + (int)w.em;
        if (smin < firstStart) firstStart = smin;
        if (md >= smin && md < emin) { inside = true; if (emin < curEnd) curEnd = emin; }
        if (smin > md && smin < nextStart) nextStart = smin;
      }
      // Si no hay otra ventana hoy, la próxima es mañana al primer inicio
      if (nextStart == 1e9) nextStart = firstStart + 1440;

      int deltaMin = nextStart - md;
      if (deltaMin < 0) deltaMin += 1440; // seguridad

      int nsH = (nextStart % 1440) / 60;
      int nsM = (nextStart % 1440) % 60;

      char hhmm[6];
      snprintf(hhmm, sizeof(hhmm), "%02d:%02d", nsH, nsM);

      int dh = deltaMin / 60;
      int dm = deltaMin % 60;

      nextText = String(F("<b>Próxima ventana:</b> ")) + hhmm + F(" (en ");
      if (dh > 0) { nextText += String(dh) + F("h "); }
      nextText += String(dm) + F("m)");

      if (inside) {
        int rem = curEnd - md;
        if (rem < 0) rem = 0;
        int rh = rem / 60, rm = rem % 60;
        nextText += F(" &nbsp;|&nbsp; <b>Ahora en ventana</b> (termina en ");
        if (rh > 0) nextText += String(rh) + F("h ");
        nextText += String(rm) + F("m)");
      }
    } else if (!timeValid) {
      nextText = F("Hora no sincronizada");
    }

    // --- Render tarjeta de estado ---
    s += F("<div class='formcard'><h4>Estado del sistema</h4><p>");
    s += F("Modo efectivo: <b>");
    s += effManual ? F("MANUAL") : F("AUTOMÁTICO");
    s += F("</b><br/>");

    s += F("Fuente de modo: ");
    s += ovr ? F("<b>Software (override activo)</b>") : F("<b>Hardware (switch físico)</b>");
    s += F("<br/>");

    s += F("Selección SW: ");
    s += manual ? F("MANUAL") : F("AUTOMÁTICO");
    s += F(" &nbsp; | &nbsp; Switch HW: ");
    s += hwManual ? F("MANUAL") : F("AUTOMÁTICO");
    s += F("<br/>");

    s += F("Hora local: <b>");
    s += String(nowBuf);
    s += F("</b><br/>");

    s += F("Última sincronización NTP: ");
    s += lastSyncTxt;
    s += F("<br/>");

    s += nextText;
    s += F("</p></div>");
  }

  // ================= Tabla GPIO =================
  // Conjunto de pines relevantes del proyecto (evitamos 6..11 por flash)
  std::vector<int> pins;
  // MAIN/SEC
  for (int i=0;i<RP::NUM_MAINS;i++) if (!vecContains_(pins, RP::MAIN_PINS[i]) && !isFlashPin_(RP::MAIN_PINS[i])) pins.push_back(RP::MAIN_PINS[i]);
  for (int j=0;j<RP::NUM_SECS; j++) if (!vecContains_(pins, RP::SEC_PINS[j])   && !isFlashPin_(RP::SEC_PINS[j]))   pins.push_back(RP::SEC_PINS[j]);
  // Control/aux
  const int extras[] = {
    RP::PIN_ALWAYS_ON, RP::PIN_ALWAYS_ON_12, RP::PIN_TOGGLE_NEXT, RP::PIN_TOGGLE_PREV,
    RP::PIN_FLOW_1, RP::PIN_FLOW_2, RP::PIN_SWITCH_MANUAL, RP::PIN_NEXT, RP::PIN_PREV
  };
  for (size_t k=0;k<sizeof(extras)/sizeof(extras[0]);++k){
    int p = extras[k];
    if (p>=0 && !vecContains_(pins,p) && !isFlashPin_(p)) pins.push_back(p);
  }

  s += F("<h3>GPIO (dirección y nivel)</h3>");
  s += F("<p><small>La <b>dirección</b> se infiere por el <i>rol</i> configurado en el proyecto (MAIN/SEC y salidas auxiliares = OUT; botones, switch y caudalímetros = IN). "
         "El <b>nivel</b> es lectura directa de <code>digitalRead()</code>. "
         "“Interpretación” usa ActiveLow para MAIN/SEC.</small></p>");
  s += F("<table><tr><th>GPIO</th><th>Rol</th><th>Dir</th><th>Nivel</th><th>AL</th><th>Interpretación</th></tr>");

  for (int pin : pins) {
    String dir = dirOf_(pin);
    int level = digitalRead(pin); // seguro para IN y OUT

    bool hasAL=false, al=false;
    String role = roleOf_(pin, hasAL, al);

    s += F("<tr><td><code>");
    s += String(pin);
    s += F("</code></td><td>");
    s += role;
    s += F("</td><td>");
    s += dir;
    s += F("</td><td>");
    s += (level==HIGH ? F("HIGH") : F("LOW"));
    s += F("</td><td>");
    if (hasAL)   s += (al ? F("AL") : F("AH"));
    else         s += F("—");
    s += F("</td><td>");
    s += interpFromAL_(level, hasAL, al);
    s += F("</td></tr>");
  }
  s += F("</table>");

  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}

/* ====================== Preferencias Wi-Fi ====================== */
bool WebUI::loadSaved() {
  savedCount_ = 0; autoIdx_ = -1;
  Preferences prefs;
  if (!prefs.begin(NS_WIFI, /*ro*/ true)) return false;
  int cnt = prefs.getInt("count", 0);
  autoIdx_ = prefs.getInt("auto_idx", -1);
  for (int i=0;i<cnt && i<MAX_SAVED;i++){
    SavedNet n;
    n.ssid = prefs.getString((String("n")+i+"_ssid").c_str(), "");
    n.pass = prefs.getString((String("n")+i+"_pass").c_str(), "");
    n.open = prefs.getBool   ((String("n")+i+"_open").c_str(), false);
    if (n.ssid.length()) saved_[savedCount_++] = n;
  }
  prefs.end();
  if (autoIdx_ < -1 || autoIdx_ >= savedCount_) autoIdx_ = -1;
  return true;
}
bool WebUI::persistSaved() {
  Preferences prefs;
  if (!prefs.begin(NS_WIFI, /*ro*/ false)) return false;
  prefs.clear();
  prefs.putInt("count", savedCount_);
  prefs.putInt("auto_idx", autoIdx_);
  for (int i=0;i<savedCount_;i++){
    prefs.putString((String("n")+i+"_ssid").c_str(), saved_[i].ssid);
    prefs.putString((String("n")+i+"_pass").c_str(), saved_[i].pass);
    prefs.putBool  ((String("n")+i+"_open").c_str(), saved_[i].open);
  }
  prefs.end();
  return true;
}
int WebUI::findSavedBySsid(const String& ssid) const {
  for (int i=0;i<savedCount_;i++) if (saved_[i].ssid == ssid) return i;
  return -1;
}
bool WebUI::addOrUpdateSaved(const String& ssid, const String& pass, bool openNet, bool setAuto) {
  int idx = findSavedBySsid(ssid);
  if (idx >= 0){
    saved_[idx].pass = pass;
    saved_[idx].open = openNet;
  } else if (savedCount_ < MAX_SAVED){
    SavedNet tmp(ssid, pass, openNet);
    saved_[savedCount_] = tmp;
    idx = savedCount_;
    savedCount_++;
  } else {
    return false;
  }
  if (setAuto) autoIdx_ = idx;
  return persistSaved();
}
bool WebUI::deleteSaved(int idx) {
  if (idx < 0 || idx >= savedCount_) return false;
  for (int i=idx;i<savedCount_-1;i++) saved_[i] = saved_[i+1];
  savedCount_--;
  if (autoIdx_ == idx) autoIdx_ = -1;
  else if (autoIdx_ > idx) autoIdx_--;
  return persistSaved();
}
bool WebUI::setAutoIndex(int idx) {
  if (idx < 0 || idx >= savedCount_) return false;
  autoIdx_ = idx; return persistSaved();
}

/* ====================== HTTP: Wi-Fi ====================== */
void WebUI::handleWifiInfo() {
  String s = htmlHeader(F("Wi-Fi"));
  s += F("<h3>Estado Wi-Fi</h3>");
  s += F("<p>Status: ");
  s += (WiFi.status()==WL_CONNECTED) ? F("<b>WL_CONNECTED</b>") : F("<b>NO CONECTADO</b>");
  s += F("<br/>IP (STA): <code>"); s += WiFi.localIP().toString(); s += F("</code>");
  s += F("<br/>IP (AP) : <code>"); s += WiFi.softAPIP().toString(); s += F("</code>");
  s += F("<br/>RSSI: "); s += String(WiFi.RSSI()); s += F(" dBm");
  s += F("<br/>Canal: "); s += String(WiFi.channel());
  s += F("</p>");
  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}

void WebUI::handleWifiScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING || n == -2) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false); // async
    server_.sendHeader(F("Refresh"), "1; url=/wifi/scan");
    server_.send(200, F("text/html"), F("<meta http-equiv='refresh' content='1'><p>Escaneando… vuelve automáticamente.</p>"));
    return;
  }
  if (n < 0) { server_.send(500, F("text/plain"), F("Error de escaneo")); return; }
  String s = htmlHeader(F("Escaneo Wi-Fi"));
  s += F("<h3>Redes encontradas: "); s += String(n); s += F("</h3>");
  s += F("<table><tr><th>Idx</th><th>SSID</th><th>RSSI</th><th>Ch</th><th>Seguridad</th><th>Conectar</th></tr>");
  for (int i=0;i<n;i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    uint8_t enc = WiFi.encryptionType(i);
    bool open = (enc == WIFI_AUTH_OPEN);
    s += F("<tr><td>"); s += String(i); s += F("</td><td>"); s += ssid; s += F("</td><td>");
    s += String(rssi); s += F("</td><td>"); s += String(WiFi.channel(i)); s += F("</td><td>");
    s += open ? F("OPEN") : F("PSK"); s += F("</td><td>");
    s += F("<form method='post' action='/wifi/connect'><input type='hidden' name='ssid' value='");
    s += ssid; s += F("'><input type='password' name='pass' placeholder='pass' ");
    if (open) s += F("disabled");
    s += F(">");
    s += F(" <label><input type='checkbox' name='open' "); if (open) s += F("checked"); s += F("> abierta</label>");
    s += F(" <label><input type='checkbox' name='save'> guardar+auto</label>");
    s += F(" <button>Conectar</button></form>");
    s += F("</td></tr>");
  }
  s += F("</table>");
  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}

void WebUI::handleWifiConnect() {
  String ssid = server_.arg("ssid");
  String pass = server_.arg("pass");
  bool open   = server_.hasArg("open");
  bool save   = server_.hasArg("save");

  if (!ssid.length()) { server_.send(400, F("text/plain"), F("SSID vacío")); return; }

  if (save) { loadSaved(); addOrUpdateSaved(ssid, pass, open, /*setAuto*/ true); }

  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  if (open) WiFi.begin(ssid.c_str());
  else      WiFi.begin(ssid.c_str(), pass.c_str());

  server_.sendHeader(F("Refresh"), "2; url=/wifi/info");
  server_.send(200, F("text/html"), F("<meta http-equiv='refresh' content='2'><p>Conectando… regresar&aacute; a Wi-Fi/Estado.</p>"));
}

void WebUI::handleWifiSaved() {
  loadSaved();
  String s = htmlHeader(F("Guardadas"));
  s += F("<h3>Redes guardadas</h3>");
  if (savedCount_ == 0) {
    s += F("<p>No hay redes guardadas.</p>");
  } else {
    s += F("<table><tr><th>Idx</th><th>Auto</th><th>Seguridad</th><th>SSID</th><th>Acciones</th></tr>");
    for (int i=0;i<savedCount_;i++){
      s += F("<tr><td>"); s += String(i); s += F("</td><td>");
      s += (i==autoIdx_) ? F("⭐") : F("&nbsp;");
      s += F("</td><td>"); s += saved_[i].open ? F("OPEN") : F("PSK");
      s += F("</td><td>"); s += saved_[i].ssid; s += F("</td><td>");
      s += F("<form class='rowform' method='post' action='/wifi/saved/do'><input type='hidden' name='idx' value='"); s += String(i);
      s += F("'><input type='hidden' name='action' value='connect'><button>Conectar</button></form> ");
      s += F("<form class='rowform' method='post' action='/wifi/saved/do'><input type='hidden' name='idx' value='"); s += String(i);
      s += F("'><input type='hidden' name='action' value='setauto'><button>Auto</button></form> ");
      s += F("<form class='rowform' method='post' action='/wifi/saved/do'><input type='hidden' name='idx' value='"); s += String(i);
      s += F("'><input type='hidden' name='action' value='delete'><button>Eliminar</button></form>");
      s += F("</td></tr>");
    }
    s += F("</table>");
    s += F("<p><form method='post' action='/wifi/saved/do'><input type='hidden' name='action' value='disableauto'><button>Desactivar autoconexión</button></form></p>");
  }
  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}

void WebUI::handleWifiSavedAction() {
  loadSaved();
  String action = server_.arg("action");
  int idx = server_.hasArg("idx") ? server_.arg("idx").toInt() : -1;

  if (action == "connect") {
    if (idx>=0 && idx<savedCount_) {
      if (saved_[idx].open) WiFi.begin(saved_[idx].ssid.c_str());
      else                  WiFi.begin(saved_[idx].ssid.c_str(), saved_[idx].pass.c_str());
      server_.sendHeader(F("Refresh"), "2; url=/wifi/info");
      server_.send(200, F("text/html"), F("<meta http-equiv='refresh' content='2'><p>Conectando…</p>"));
      return;
    }
  } else if (action == "setauto") {
    if (setAutoIndex(idx)) { server_.sendHeader(F("Location"), "/wifi/saved"); server_.send(302, F("text/plain"), ""); return; }
  } else if (action == "delete") {
    if (deleteSaved(idx)) { server_.sendHeader(F("Location"), "/wifi/saved"); server_.send(302, F("text/plain"), ""); return; }
  } else if (action == "disableauto") {
    autoIdx_ = -1; persistSaved(); server_.sendHeader(F("Location"), "/wifi/saved"); server_.send(302, F("text/plain"), ""); return;
  }
  server_.send(400, F("text/plain"), F("Acción inválida/índice fuera de rango"));
}

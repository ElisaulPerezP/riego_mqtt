// File: src/web/WebUI_WiFi.cpp
#include "web/WebUI.h"
#include <WiFi.h>
#include <Preferences.h>

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

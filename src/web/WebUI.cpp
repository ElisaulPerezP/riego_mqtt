#include "web/WebUI.h"
#include <WiFi.h>

void WebUI::begin() {
  // Rutas
  server_.on("/",               HTTP_GET,  [this]{ handleRoot(); });
  server_.on("/wifi/info",      HTTP_GET,  [this]{ handleWifiInfo(); });
  server_.on("/wifi/scan",      HTTP_GET,  [this]{ handleWifiScan(); });
  server_.on("/wifi/connect",   HTTP_POST, [this]{ handleWifiConnect(); });
  server_.on("/wifi/saved",     HTTP_GET,  [this]{ handleWifiSaved(); });
  server_.on("/wifi/saved/do",  HTTP_POST, [this]{ handleWifiSavedAction(); });

  server_.on("/mqtt",           HTTP_GET,  [this]{ handleMqtt(); });
  server_.on("/mqtt/set",       HTTP_POST, [this]{ handleMqttSet(); });
  server_.on("/mqtt/publish",   HTTP_POST, [this]{ handleMqttPublish(); });
  server_.on("/mqtt/poll",      HTTP_GET,  [this]{ handleMqttPoll(); });

  server_.onNotFound([this]{ handleNotFound(); });

  server_.begin();
}

void WebUI::loop() {
  server_.handleClient();
}

void WebUI::attachMqttSink() {
  // Recibir mensajes y mostrarlos en /mqtt (polling)
  chat_.onMessage([this](const String& t, const String& p){ pushMsg_(t, p); });
  chat_.setSubTopic(cfg_.subTopic);
  chat_.subscribe(); // activa recepción
}

/* ====================== Preferencias Wi-Fi ====================== */
bool WebUI::loadSaved() {
  savedCount_ = 0; autoIdx_ = -1;
  if (!prefs_.begin(NS_WIFI, /*ro*/ true)) return false;
  int cnt = prefs_.getInt("count", 0);
  autoIdx_ = prefs_.getInt("auto_idx", -1);
  for (int i=0;i<cnt && i<MAX_SAVED;i++){
    SavedNet n;
    n.ssid = prefs_.getString((String("n")+i+"_ssid").c_str(), "");
    n.pass = prefs_.getString((String("n")+i+"_pass").c_str(), "");
    n.open = prefs_.getBool   ((String("n")+i+"_open").c_str(), false);
    if (n.ssid.length()) saved_[savedCount_++] = n;
  }
  prefs_.end();
  if (autoIdx_ < -1 || autoIdx_ >= savedCount_) autoIdx_ = -1;
  return true;
}
bool WebUI::persistSaved() {
  if (!prefs_.begin(NS_WIFI, /*ro*/ false)) return false;
  prefs_.clear();
  prefs_.putInt("count", savedCount_);
  prefs_.putInt("auto_idx", autoIdx_);
  for (int i=0;i<savedCount_;i++){
    prefs_.putString((String("n")+i+"_ssid").c_str(), saved_[i].ssid);
    prefs_.putString((String("n")+i+"_pass").c_str(), saved_[i].pass);
    prefs_.putBool  ((String("n")+i+"_open").c_str(), saved_[i].open);
  }
  prefs_.end();
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
    saved_[savedCount_++] = SavedNet{ssid, pass, openNet};
    idx = savedCount_-1;
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

/* ================================== HTML helpers ================================== */
String WebUI::htmlHeader(const String& title) const {
  String ipSTA = (uint32_t)WiFi.localIP()   ? WiFi.localIP().toString()   : F("(sin IP)");
  String ipAP  = (uint32_t)WiFi.softAPIP()  ? WiFi.softAPIP().toString()  : F("(sin AP)");
  String s;
  s += F("<!doctype html><html><head><meta charset='utf-8'>");
  s += F("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  s += F("<title>"); s += title; s += F("</title>");
  s += F("<style>body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;margin:16px}code,pre{background:#f4f4f4;padding:2px 4px;border-radius:4px}table{border-collapse:collapse}td,th{border:1px solid #ddd;padding:6px}</style>");
  s += F("</head><body>");
  s += F("<h2>Riego ESP32 - WebUI</h2>");
  s += F("<p>AP: <b>config</b> · mDNS: <a href='http://config.local/'>config.local</a></p>");
  s += F("<p>IP AP: <code>");  s += ipAP;  s += F("</code><br>");
  s += F("IP LAN (STA): <code>"); s += ipSTA; s += F("</code></p>");
  s += F("<nav><a href='/'>Home</a> · <a href='/wifi/info'>WiFi</a> · <a href='/wifi/scan'>Escanear</a> · <a href='/wifi/saved'>Guardadas</a> · <a href='/mqtt'>MQTT</a></nav><hr/>");
  return s;
}
String WebUI::htmlFooter() const {
  return F("<hr/><small>WebUI minimal · ESP32</small></body></html>");
}
String WebUI::jsonEscape(String s) {
  s.replace("\\","\\\\");
  s.replace("\"","\\\"");
  s.replace("\r","\\r");
  s.replace("\n","\\n");
  s.replace("\t","\\t");
  return s;
}

/* ================================== HTTP: HOME / Wi-Fi ================================== */
void WebUI::handleRoot() {
  String s = htmlHeader(F("Home"));
  s += F("<p>Este servidor reemplaza el menú por Serial. Conéctate al AP <b>config</b> (pass <code>password</code>) y abre <a href='http://config.local/'>config.local</a>.</p>");
  s += F("<ul><li><a href='/wifi/info'>Estado Wi-Fi</a></li>");
  s += F("<li><a href='/wifi/scan'>Escanear y conectar</a></li>");
  s += F("<li><a href='/wifi/saved'>Redes guardadas / autoconexión</a></li>");
  s += F("<li><a href='/mqtt'>MQTT (config, estado, chat)</a></li></ul>");
  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}
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

  if (save) {
    loadSaved();
    addOrUpdateSaved(ssid, pass, open, /*setAuto*/ true);
  }

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
      // acciones: conectar / auto / borrar
      s += F("<form style='display:inline' method='post' action='/wifi/saved/do'><input type='hidden' name='idx' value='"); s += String(i);
      s += F("'><input type='hidden' name='action' value='connect'><button>Conectar</button></form> ");
      s += F("<form style='display:inline' method='post' action='/wifi/saved/do'><input type='hidden' name='idx' value='"); s += String(i);
      s += F("'><input type='hidden' name='action' value='setauto'><button>Auto</button></form> ");
      s += F("<form style='display:inline' method='post' action='/wifi/saved/do'><input type='hidden' name='idx' value='"); s += String(i);
      s += F("'><input type='hidden' name='action' value='delete'><button>Eliminar</button></form>");
      s += F("</td></tr>");
    }
    s += F("</table>");
    // desactivar auto
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

/* ================================== HTTP: MQTT ================================== */
void WebUI::handleMqtt() {
  String s = htmlHeader(F("MQTT"));
  s += F("<h3>Estado</h3><p>");
  s += chat_.status();
  s += F("</p><hr/>");

  s += F("<h3>Configurar</h3>");
  s += F("<form method='post' action='/mqtt/set'>");
  s += F("Host: <input name='host' value='"); s += cfg_.host; s += F("'> ");
  s += F("Puerto: <input name='port' type='number' min='1' max='65535' value='"); s += String(cfg_.port); s += F("'><br>");
  s += F("Usuario: <input name='user' value='"); s += cfg_.user; s += F("'> ");
  s += F("Pass: <input name='pass' type='password' value='"); s += cfg_.pass; s += F("'><br>");
  s += F("Tópico (pub): <input name='topic' value='"); s += cfg_.topic; s += F("'> ");
  s += F("Tópico (sub): <input name='sub' value='"); s += cfg_.subTopic; s += F("'><br>");
  s += F("<button>Guardar</button></form><hr/>");

  s += F("<h3>Chat MQTT</h3>");
  s += F("<form method='post' action='/mqtt/publish'>Mensaje: <input name='msg' style='width:60%'> <button>Publicar</button></form>");
  s += F("<pre id='msgs' style='height:260px;overflow:auto'></pre>");
  s += F("<script>let last=0;async function tick(){try{let r=await fetch('/mqtt/poll?last='+last);let j=await r.json();last=j.last;let t='';for(let m of j.items){t+=`[${m.ms}] ${m.topic}: ${m.payload}\\n`;}let el=document.getElementById('msgs');el.textContent+=t;el.scrollTop=el.scrollHeight;}catch(e){} setTimeout(tick,1000);}tick();</script>");

  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}
void WebUI::handleMqttSet() {
  if (server_.hasArg("host")) cfg_.host = server_.arg("host");
  if (server_.hasArg("port")) { long p=server_.arg("port").toInt(); if (p>=1 && p<=65535) cfg_.port=(uint16_t)p; }
  if (server_.hasArg("user")) cfg_.user = server_.arg("user");
  if (server_.hasArg("pass")) cfg_.pass = server_.arg("pass");
  if (server_.hasArg("topic")) cfg_.topic = server_.arg("topic");
  if (server_.hasArg("sub"))   cfg_.subTopic = server_.arg("sub");

  cfgStore_.save(cfg_);

  chat_.setServer(cfg_.host, cfg_.port);
  chat_.setAuth(cfg_.user, cfg_.pass);
  chat_.setTopic(cfg_.topic);
  chat_.setSubTopic(cfg_.subTopic);
  chat_.subscribe(); // re-suscribirse tras cambio

  server_.sendHeader(F("Location"), "/mqtt");
  server_.send(302, F("text/plain"), "");
}
void WebUI::handleMqttPublish() {
  String msg = server_.arg("msg");
  bool ok = chat_.publish(msg);
  server_.sendHeader(F("Location"), "/mqtt");
  server_.send(302, F("text/plain"), ok ? "ok" : "fail");
}
void WebUI::handleMqttPoll() {
  // 'last' = último seq que el cliente ya vio
  unsigned long last = 0;
  if (server_.hasArg("last")) last = strtoul(server_.arg("last").c_str(), nullptr, 10);

  // Construir JSON con items de seq > last
  String out = F("{\"last\":");
  out += String(seqCounter_);
  out += F(",\"items\":[");
  bool first = true;

  size_t have = used_;
  for (size_t i=0;i<have;i++){
    size_t idx = ( (writePos_ + MSG_BUF - have + i) % MSG_BUF );
    const Msg& m = inbox_[idx];
    if (m.seq == 0 || m.seq <= last) continue;

    if (!first) out += ",";
    first = false;
    out += F("{\"topic\":\""); out += jsonEscape(m.topic);
    out += F("\",\"payload\":\""); out += jsonEscape(m.payload);
    out += F("\",\"ms\":"); out += String(m.ms);
    out += F("}");
  }
  out += F("]}");
  server_.send(200, F("application/json"), out);
}
void WebUI::handleNotFound() {
  server_.send(404, F("text/plain"), F("404"));
}

/* ========== MQTT sink buffer ========== */
void WebUI::pushMsg_(const String& t, const String& p) {
  Msg& m = inbox_[writePos_];
  m.topic = t;
  m.payload = p;
  m.ms = millis();
  m.seq = ++seqCounter_;             // secuencia monótona

  writePos_ = (writePos_ + 1) % MSG_BUF;
  if (used_ < MSG_BUF) used_++;     // si se llena, sobreescribe circular
}

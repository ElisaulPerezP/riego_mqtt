#include "WiFiManagerESP32.h"

WiFiManagerESP32* WiFiManagerESP32::self_ = nullptr;

WiFiManagerESP32::WiFiManagerESP32() {}

void WiFiManagerESP32::begin(Stream* io, uint32_t serialTimeoutMs) {
  io_ = io ? io : &Serial;
  serialTimeoutMs_ = serialTimeoutMs;

  self_ = this;
  WiFi.onEvent(WiFiEventThunk);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  loadSaved();
  if (autoIdx_ >= 0 && autoIdx_ < savedCount_) {
    io_->printf("Auto: intentando \"%s\"...\n", saved_[autoIdx_].ssid.c_str());
    (void)connectNonBlocking_(saved_[autoIdx_].ssid.c_str(),
                              saved_[autoIdx_].pass.c_str(),
                              saved_[autoIdx_].open);
  }
}

void WiFiManagerESP32::service() { serviceIPReporter(); }

void WiFiManagerESP32::showMenu() const {
  io_->println();
  io_->println("===== MENU WIFI =====");
  io_->println("[s] Escanear redes (7 s, no desconecta)");
  io_->println("[i] Info de conexión (IP/RSSI/Canal)");
  io_->println("[c] Conectar por índice (de la última lista)");
  io_->println("[m] Conectar manual (SSID y contraseña)");
  io_->println("[d] Desconectar de la Wi-Fi");
  io_->println("---- Guardadas / Autoconexión ----");
  io_->println("[l] Ver redes guardadas");
  io_->println("[k] Conectar a guardada por índice");
  io_->println("[u] Establecer autoconexión por índice");
  io_->println("[x] Eliminar guardada por índice");
  io_->println("[f] Desactivar autoconexión (mantiene la lista)");
  io_->println("[q] Reimprimir menú");
  io_->print("> ");
}

void WiFiManagerESP32::handleSerial() {
  if (!io_->available()) return;
  String cmd = readLine(true, 8);
  if (!cmd.length()) { showMenu(); return; }

  if (cmd == "s" || cmd == "S") {
    scanNetworksAndPrint();
  } else if (cmd == "i" || cmd == "I") {
    if (isConnected()) printConnInfo();
    else io_->println("No conectado.");
  } else if (cmd == "c" || cmd == "C") {
    int count = WiFi.scanComplete();
    if (count <= 0) { io_->println("Primero escanea redes con [s]."); }
    else {
      io_->print("Índice de red: ");
      String sidx = readLine(true, 8);
      if (sidx.length()) {
        int idx = sidx.toInt();
        count = WiFi.scanComplete();
        if (idx >= 0 && idx < count) {
          String ssid = WiFi.SSID(idx);
          uint8_t enc = WiFi.encryptionType(idx);
          bool openNet = (enc == WIFI_AUTH_OPEN);

          String pass;
          if (!openNet) { io_->print("Contraseña (oculta): "); pass = readPassword(); }
          else { io_->println("Red abierta: no requiere contraseña."); }

          io_->print("¿Activar autoconexión y guardar esta red? (s/N): ");
          String ans = readLine(true, 4);
          bool enableAuto = (ans.length() && (ans[0]=='s' || ans[0]=='S'));
          if (enableAuto) { addOrUpdateSaved(ssid, pass, openNet, true); }

          connectNonBlocking_(ssid.c_str(), pass.c_str(), openNet);
        } else {
          io_->println("Índice inválido.");
        }
      }
    }
  } else if (cmd == "m" || cmd == "M") {
    io_->print("SSID: ");
    String ssid = readLine(true, 64);
    if (!ssid.length()) { io_->println("SSID vacío."); }
    else {
      io_->print("¿Es red abierta? (s/N): ");
      String isOpen = readLine(true, 4);
      bool openNet = (isOpen.length() && (isOpen[0]=='s' || isOpen[0]=='S'));
      String pass;
      if (!openNet) { io_->print("Contraseña (oculta): "); pass = readPassword(); }

      io_->print("¿Activar autoconexión y guardar esta red? (s/N): ");
      String ans = readLine(true, 4);
      bool enableAuto = (ans.length() && (ans[0]=='s' || ans[0]=='S'));
      if (enableAuto) { addOrUpdateSaved(ssid, pass, openNet, true); }

      connectNonBlocking_(ssid.c_str(), pass.c_str(), openNet);
    }
  } else if (cmd == "d" || cmd == "D") {
    if (isConnected()) {
      io_->println("Desconectando…");
      WiFi.disconnect(false, true);
      ipAnnounced_ = false; lastIP_ = IPAddress((uint32_t)0);
    } else {
      io_->println("Ya estás desconectado.");
    }
  } else if (cmd == "l" || cmd == "L") {
    listSaved();
  } else if (cmd == "k" || cmd == "K") {
    listSaved();
    io_->print("Índice de guardada a conectar: ");
    String s = readLine(true, 8);
    if (s.length()){
      int idx = s.toInt();
      (void)connectSavedByIndex(idx);
    }
  } else if (cmd == "x" || cmd == "X") {
    listSaved();
    io_->print("Índice de guardada a eliminar: ");
    String s = readLine(true, 8);
    if (s.length()){
      int idx = s.toInt();
      if (!deleteSaved(idx)) io_->println("Índice inválido.");
    }
  } else if (cmd == "u" || cmd == "U") {
    listSaved();
    io_->print("Índice a marcar como AUTOCONEXIÓN: ");
    String s = readLine(true, 8);
    if (s.length()){
      int idx = s.toInt();
      if (!setAutoIndex(idx)) io_->println("Índice inválido.");
    }
  } else if (cmd == "f" || cmd == "F") {
    disableAuto();
  } else {
    showMenu();
  }
  io_->print("> ");
}

/* ================== Internos ================== */

void WiFiManagerESP32::WiFiEventThunk(WiFiEvent_t event) {
  if (self_) self_->onWiFiEvent(event);
}
void WiFiManagerESP32::onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      io_->println("\n[WiFi] Asociado al AP.");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      ipAnnounced_ = false; lastIP_ = IPAddress((uint32_t)0);
      io_->print("[WiFi] IP asignada: "); io_->println(WiFi.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      io_->println("[WiFi] Desconectado del AP.");
      ipAnnounced_ = false; lastIP_ = IPAddress((uint32_t)0);
      break;
    default: break;
  }
}

void WiFiManagerESP32::loadSaved() {
  savedCount_ = 0; autoIdx_ = -1;
  if (!prefs_.begin(NS_, true)) return;
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
}

void WiFiManagerESP32::persistSaved() {
  if (!prefs_.begin(NS_, false)) return;
  prefs_.clear();
  prefs_.putInt("count", savedCount_);
  prefs_.putInt("auto_idx", autoIdx_);
  for (int i=0;i<savedCount_;i++){
    prefs_.putString((String("n")+i+"_ssid").c_str(), saved_[i].ssid);
    prefs_.putString((String("n")+i+"_pass").c_str(), saved_[i].pass);
    prefs_.putBool  ((String("n")+i+"_open").c_str(), saved_[i].open);
  }
  prefs_.end();
}

int WiFiManagerESP32::findSavedBySsid(const String& ssid) const {
  for (int i=0;i<savedCount_;i++) if (saved_[i].ssid == ssid) return i;
  return -1;
}

void WiFiManagerESP32::addOrUpdateSaved(const String& ssid, const String& pass, bool openNet, bool setAsAuto) {
  int idx = findSavedBySsid(ssid);
  if (idx >= 0){
    saved_[idx].pass = pass;
    saved_[idx].open = openNet;
  } else if (savedCount_ < MAX_SAVED){
    saved_[savedCount_++] = SavedNet{ssid, pass, openNet};
    idx = savedCount_-1;
  } else {
    io_->println("⚠️ Lista de guardadas llena (MAX=10). Elimina alguna con [x].");
    return;
  }
  if (setAsAuto) autoIdx_ = idx;
  persistSaved();
  io_->printf("🧷 Guardada \"%s\"%s.\n", ssid.c_str(), setAsAuto?" como AUTOCONEXIÓN":"");
}

bool WiFiManagerESP32::waitForValidIP(uint32_t timeoutMs) const {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if ((uint32_t)WiFi.localIP() != 0) return true;
    delay(50); yield();
  }
  return false;
}

/* ======== ÚNICO CAMBIO DE COMPORTAMIENTO (silenciar IP en loop) ======== */
void WiFiManagerESP32::serviceIPReporter() {
  if (isConnected()) {
    IPAddress ip = WiFi.localIP();
    // actualiza estado pero NO imprime (la IP ya se imprime 1 vez en GOT_IP)
    if ((uint32_t)ip != 0 && (!ipAnnounced_ || ip != lastIP_)) {
      lastIP_ = ip;
      ipAnnounced_ = true;
    }
  } else {
    ipAnnounced_ = false;
    lastIP_ = IPAddress((uint32_t)0);
  }
}

void WiFiManagerESP32::scanNetworksAndPrint(uint32_t timeout_ms, bool show_hidden) {
  io_->println("\nEscaneando redes… (timeout ~ " + String(timeout_ms/1000.0,1) + " s)");
  WiFi.scanDelete();
  WiFi.scanNetworks(true, show_hidden);
  uint32_t t0 = millis();
  int res;
  while ((res = WiFi.scanComplete()) == -1 && (millis() - t0) < timeout_ms) {
    io_->print("."); delay(250); yield(); serviceIPReporter();
  }
  io_->println();

  if (res == -1) { io_->println("⏱️  Tiempo de escaneo agotado."); WiFi.scanDelete(); return; }
  if (res < 0)   { io_->printf("❌ Error de escaneo (%d)\n", res); return; }
  if (res == 0)  { io_->println("No se encontraron redes."); return; }

  io_->printf("Encontradas %d redes:\n\n", res);
  io_->println("Idx | Señal | Ch | Seguridad  | SSID");
  io_->println("----+-------+----+------------+------------------------------");
  for (int i = 0; i < res; i++) {
    io_->printf("%3d | %4ddB %-4s | %2u | %-10s | %s\n",
      i, WiFi.RSSI(i), rssiBars(WiFi.RSSI(i)).c_str(), WiFi.channel(i),
      authToStr((uint8_t)WiFi.encryptionType(i)).c_str(), WiFi.SSID(i).c_str());
  }
  io_->println();
}

bool WiFiManagerESP32::connectNonBlocking_(const char* ssid, const char* pass, bool openNet) {
  io_->printf("Conectando a \"%s\" …\n", ssid);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  if (openNet) WiFi.begin(ssid);
  else         WiFi.begin(ssid, pass);

  uint32_t t0 = millis();
  uint8_t st = WiFi.status();
  while (millis() - t0 < CONNECT_TIMEOUT_MS) {
    uint8_t now = WiFi.status();
    if (now != st) { io_->printf("[WiFi] Estado: %s\n", wlStatusStr(now)); st = now; }
    if (now == WL_CONNECTED) break;
    delay(200); yield(); serviceIPReporter();
  }

  if (isConnected()) {
    ipAnnounced_ = false; lastIP_ = IPAddress((uint32_t)0);
    waitForValidIP(7000);
    printConnInfo();
    return true;
  } else {
    io_->printf("❌ Falló la conexión. Último estado: %s\n", wlStatusStr(WiFi.status()));
    return false;
  }
}

/* ====== Programático ====== */

bool WiFiManagerESP32::connectManual(const String& ssid, const String& pass, bool openNet, bool askSaveAndAuto) {
  bool setAuto = false;
  if (askSaveAndAuto) {
    io_->print("¿Activar autoconexión y guardar esta red? (s/N): ");
    String ans = readLine(true, 4);
    setAuto = (ans.length() && (ans[0]=='s' || ans[0]=='S'));
  }
  if (setAuto) addOrUpdateSaved(ssid, pass, openNet, true);
  return connectNonBlocking_(ssid.c_str(), pass.c_str(), openNet);
}

bool WiFiManagerESP32::connectSavedByIndex(int idx) {
  loadSaved();
  if (idx < 0 || idx >= savedCount_) { io_->println("Índice inválido."); return false; }
  return connectNonBlocking_(saved_[idx].ssid.c_str(), saved_[idx].pass.c_str(), saved_[idx].open);
}

void WiFiManagerESP32::listSaved() const {
  const_cast<WiFiManagerESP32*>(this)->loadSaved();
  if (savedCount_ == 0){ io_->println("No hay redes guardadas."); return; }
  io_->println("\nGuardadas:");
  io_->println("Idx | Auto | Seguridad | SSID");
  io_->println("----+------+-----------+------------------------------");
  for (int i=0;i<savedCount_;i++){
    io_->printf("%3d |  %s   | %-9s | %s\n",
      i, (i==autoIdx_?"*":" "), saved_[i].open?"OPEN":"PSK", saved_[i].ssid.c_str());
  }
  io_->println();
}

bool WiFiManagerESP32::setAutoIndex(int idx) {
  loadSaved();
  if (idx < 0 || idx >= savedCount_) return false;
  autoIdx_ = idx; persistSaved();
  io_->printf("⭐ Autoconexión: \"%s\".\n", saved_[idx].ssid.c_str());
  return true;
}

bool WiFiManagerESP32::deleteSaved(int idx) {
  loadSaved();
  if (idx < 0 || idx >= savedCount_) return false;
  String name = saved_[idx].ssid;
  for (int i=idx;i<savedCount_-1;i++) saved_[i] = saved_[i+1];
  savedCount_--;
  if (autoIdx_ == idx) autoIdx_ = -1;
  else if (autoIdx_ > idx) autoIdx_--;
  persistSaved();
  io_->printf("🗑️ Eliminada \"%s\".\n", name.c_str());
  return true;
}

void WiFiManagerESP32::disableAuto() {
  loadSaved();
  autoIdx_ = -1; persistSaved();
  io_->println("🧷 Autoconexión desactivada (lista intacta).");
}

/* ===== Utils ===== */

void WiFiManagerESP32::printConnInfo() const {
  waitForValidIP(7000);
  io_->println("✅ Conectado");
  io_->print("IP: ");   io_->println(WiFi.localIP());
  io_->print("RSSI: "); io_->print(WiFi.RSSI()); io_->println(" dBm");
  io_->print("Canal: ");io_->println(WiFi.channel());
}

const char* WiFiManagerESP32::wlStatusStr(uint8_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:   return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:  return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:       return "WL_CONNECTED";
    case WL_CONNECT_FAILED:  return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:    return "WL_DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}
String WiFiManagerESP32::authToStr(uint8_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "UNKNOWN";
  }
}
String WiFiManagerESP32::rssiBars(int32_t r) {
  if (r >= -55) return "▮▮▮▮";
  if (r >= -65) return "▮▮▮─";
  if (r >= -75) return "▮▮──";
  if (r >= -85) return "▮───";
  return "────";
}

// Acepta LF/CR/CRLF
String WiFiManagerESP32::readLine(bool echo, uint16_t maxLen) const {
  String line; line.reserve(maxLen);
  uint32_t t0 = millis();
  while (true) {
    if (millis() - t0 > serialTimeoutMs_) return String();
    if (!io_->available()) { delay(5); yield(); continue; }
    char c = (char)io_->read();

    if (c == '\r' || c == '\n') {
      if (io_->available()) {
        int p = io_->peek();
        if ((c == '\r' && p == '\n') || (c == '\n' && p == '\r')) (void)io_->read();
      }
      break;
    }
    if ((c == 0x7F) || (c == 0x08)) {
      if (line.length()) { line.remove(line.length() - 1); if (echo) io_->print("\b \b"); }
    } else if (isPrintable(c) && line.length() < maxLen) {
      line += c; if (echo) io_->print(c);
    }
  }
  return line;
}
String WiFiManagerESP32::readPassword(uint16_t maxLen) const {
  String line; line.reserve(maxLen);
  uint32_t t0 = millis();
  while (true) {
    if (millis() - t0 > serialTimeoutMs_) return String();
    if (!io_->available()) { delay(5); yield(); continue; }
    char c = (char)io_->read();

    if (c == '\r' || c == '\n') {
      if (io_->available()) {
        int p = io_->peek();
        if ((c == '\r' && p == '\n') || (c == '\n' && p == '\r')) (void)io_->read();
      }
      break;
    }
    if ((c == 0x7F) || (c == 0x08)) {
      if (line.length()) { line.remove(line.length() - 1); io_->print("\b \b"); }
    } else if (isPrintable(c) && line.length() < maxLen) {
      line += c; io_->print('*');
    }
  }
  io_->println();
  return line;
}

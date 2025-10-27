#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>

#include "WiFiManagerESP32.h"

#include "config/MqttConfig.h"
#include "config/MqttConfigStore.h"
#include "mqtt/MqttChat.h"

#include "web/WebUI.h"

#include "modes/modes.h"                // resetFullMode/runFullMode/resetBlinkMode/runBlinkMode
#include "schedule/IrrigationSchedule.h"
#include "state/RelayState.h"           // catálogo de estados (RelayState)

// =================== CONFIG BÁSICA ===================
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif
#ifndef SERIAL_CLI_TIMEOUT_MS
#define SERIAL_CLI_TIMEOUT_MS 10UL
#endif

// Selector de modo MANUAL/AUTO por hardware
static constexpr int PIN_SWITCH_MANUAL  = 19;   // LOW=AUTO, HIGH=MANUAL
static constexpr uint32_t MODE_DEBOUNCE_MS = 120;

// =================== “Serial nulo” para WiFiManager ===================
class NullStream : public Stream {
public:
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t write(uint8_t) override { return 1; }
  using Print::write;
};

// =================== GLOBALES ===================
WebServer        server(80);
WiFiManagerESP32 wifi;
MqttConfig       cfg;
MqttConfigStore  cfgStore("mqtt");
MqttChat         chat(cfg.host.c_str(), cfg.port, cfg.user.c_str(), cfg.pass.c_str(), cfg.topic.c_str());
WebUI*           webui = nullptr;

static TaskHandle_t gIrrigationTask = nullptr;

// Config de riego (persistente)
static Preferences irrPrefs;
static IrrigationConfig gIrrCfg;   // tz, program (starts + sets[0].steps), flowCal

// ===== Catálogo de ESTADOS (persistente) =====
static Preferences statesPrefs;
static std::vector<RelayState> gStates;       // Estados visibles/editables en /states
static constexpr int HW_NUM_MAINS = 12;       // Ajusta si tu HW cambia
static constexpr int HW_NUM_SECS  = 2;

// ===== Override de modo (persistente, usado por /mode en WebUI) =====
static const char* NS_MODE = "mode";
static bool gOvrEnabled = false;   // si true, ignora el switch físico
static bool gOvrManual  = false;   // si gOvrEnabled, true=Manual, false=Auto

// ===== NUEVO: tópico de telemetría de riego =====
static const char* RIEGO_TOPIC = "public/riegoArandanosDeMiPueblo";

// =================== HELPERS: Tiempo ===================
static String nowString() {
  time_t now = time(nullptr);
  if (now < 1600000000) return F("(sin hora)");
  struct tm tm; localtime_r(&now, &tm);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
  return String(buf);
}

// =================== HELPERS: Wi-Fi ===================
static bool tryAutoConnectFromPrefs() {
  Preferences p;
  if (!p.begin("wifi_saved", /*ro*/ true)) return false;

  int count   = p.getInt("count", 0);
  int autoIdx = p.getInt("auto_idx", -1);
  if (autoIdx < 0 || autoIdx >= count) { p.end(); return false; }

  String base = String("n") + String(autoIdx) + "_";
  String ssid = p.getString((base + "ssid").c_str(), "");
  String pass = p.getString((base + "pass").c_str(), "");
  bool   open = p.getBool   ((base + "open").c_str(), false);
  p.end();

  if (!ssid.length()) return false;

  WiFi.persistent(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  if (open) WiFi.begin(ssid.c_str());
  else      WiFi.begin(ssid.c_str(), pass.c_str());

  int r = WiFi.waitForConnectResult(6000);
  Serial.printf("[WiFi] AutoConnect '%s' -> %d, IP=%s\n",
                ssid.c_str(), r, WiFi.localIP().toString().c_str());
  return (r == WL_CONNECTED);
}

// =================== HELPERS: Persistencia (Irrigation) ===================
static bool saveIrrConfig(const IrrigationConfig& c) {
  if (!irrPrefs.begin("irr", false)) return false;

  irrPrefs.putString("tz", c.tz);
  irrPrefs.putFloat("cal1", c.flowCal.pulsesPerMl1);
  irrPrefs.putFloat("cal2", c.flowCal.pulsesPerMl2);

  irrPrefs.putUChar("prog_en", c.program.enabled ? 1 : 0);

  // Guardar STARTS
  irrPrefs.putUChar("st_cnt", (uint8_t)min<size_t>(c.program.starts.size(), 30));
  for (size_t i=0; i<min<size_t>(c.program.starts.size(), 30); ++i) {
    const auto& st = c.program.starts[i];
    irrPrefs.putUChar((String("st")+i+"_h").c_str(), st.hour);
    irrPrefs.putUChar((String("st")+i+"_m").c_str(), st.minute);
    irrPrefs.putUChar((String("st")+i+"_dw").c_str(), st.dowMask);
    irrPrefs.putUChar((String("st")+i+"_set").c_str(), st.stepSetIndex);
    irrPrefs.putUChar((String("st")+i+"_en").c_str(), st.enabled ? 1 : 0);
    irrPrefs.putFloat((String("st")+i+"_ts").c_str(), st.timeScale);
    irrPrefs.putFloat((String("st")+i+"_vs").c_str(), st.volumeScale);
  }

  // Guardar sólo el StepSet 0
  uint8_t setCnt = (uint8_t)max<size_t>(1, c.program.sets.size());
  irrPrefs.putUChar("set_cnt", setCnt);

  if (c.program.sets.empty()) {
    StepSet s; s.name = "Default"; s.pauseMsBetweenSteps = 10000;
    irrPrefs.putString("set0_name", s.name);
    irrPrefs.putUInt  ("set0_pause", s.pauseMsBetweenSteps);
    irrPrefs.putUChar ("p_cnt", 0);
  } else {
    const StepSet& s0 = c.program.sets[0];
    irrPrefs.putString("set0_name", s0.name);
    irrPrefs.putUInt  ("set0_pause", s0.pauseMsBetweenSteps);

    irrPrefs.putUChar("p_cnt", (uint8_t)min<size_t>(s0.steps.size(), 40));
    for (size_t i=0; i<min<size_t>(s0.steps.size(), 40); ++i) {
      irrPrefs.putInt ((String("p")+i+"_idx").c_str(), s0.steps[i].idx);
      irrPrefs.putUInt((String("p")+i+"_dur").c_str(), s0.steps[i].maxDurationMs);
      irrPrefs.putUInt((String("p")+i+"_ml").c_str(),  s0.steps[i].targetMl);
    }
  }

  irrPrefs.end();
  return true;
}

static bool loadIrrConfig(IrrigationConfig& out) {
  if (!irrPrefs.begin("irr", true)) return false;

  out.tz = irrPrefs.getString("tz", "America/Bogota");
  out.flowCal.pulsesPerMl1 = irrPrefs.getFloat("cal1", 4.5f);
  out.flowCal.pulsesPerMl2 = irrPrefs.getFloat("cal2", 4.5f);

  out.program.enabled = irrPrefs.getUChar("prog_en", 1) != 0;

  // STARTS
  out.program.starts.clear();
  uint8_t sc = irrPrefs.getUChar("st_cnt", 0);
  for (uint8_t i=0; i<sc; ++i) {
    StartSpec st;
    st.hour         = irrPrefs.getUChar((String("st")+i+"_h").c_str(), 5);
    st.minute       = irrPrefs.getUChar((String("st")+i+"_m").c_str(), 0);
    st.dowMask      = irrPrefs.getUChar((String("st")+i+"_dw").c_str(), (DOW_MON|DOW_TUE|DOW_WED|DOW_THU|DOW_FRI|DOW_SAT|DOW_SUN));
    st.stepSetIndex = irrPrefs.getUChar((String("st")+i+"_set").c_str(), 0);
    st.enabled      = irrPrefs.getUChar((String("st")+i+"_en").c_str(), 1) != 0;
    st.timeScale    = irrPrefs.getFloat ((String("st")+i+"_ts").c_str(), 1.0f);
    st.volumeScale  = irrPrefs.getFloat ((String("st")+i+"_vs").c_str(), 1.0f);
    out.program.starts.push_back(st);
  }

  // SETS (sólo set 0)
  out.program.sets.clear();
  (void)irrPrefs.getUChar("set_cnt", 1);

  StepSet s0;
  s0.name = irrPrefs.getString("set0_name", "Default");
  s0.pauseMsBetweenSteps = irrPrefs.getUInt("set0_pause", 10000);

  uint8_t pc = irrPrefs.getUChar("p_cnt", 0);
  for (uint8_t i=0; i<pc; ++i) {
    StepSpec stp;
    stp.idx           = irrPrefs.getInt ((String("p")+i+"_idx").c_str(), 0);
    stp.maxDurationMs = irrPrefs.getUInt((String("p")+i+"_dur").c_str(), 5UL*60UL*1000UL);
    stp.targetMl      = irrPrefs.getUInt((String("p")+i+"_ml").c_str(),  0);
    s0.steps.push_back(stp);
  }
  out.program.sets.push_back(s0);

  irrPrefs.end();
  return true;
}

static void ensureIrrDefaults(IrrigationConfig& c) {
  c.tz = "America/Bogota";
  c.flowCal.pulsesPerMl1 = 4.5f;
  c.flowCal.pulsesPerMl2 = 4.5f;

  c.program.enabled = true;

  uint8_t everyday = (DOW_MON|DOW_TUE|DOW_WED|DOW_THU|DOW_FRI|DOW_SAT|DOW_SUN);
  c.program.starts.clear();
  c.program.starts.push_back(StartSpec(5,  0, everyday, 0, true, 1.00f, 1.00f));
  c.program.starts.push_back(StartSpec(17,30, everyday, 0, true, 1.00f, 1.00f));

  c.program.sets.clear();
  StepSet s0;
  s0.name = "Default";
  s0.pauseMsBetweenSteps = 10000; // 10 s
  s0.steps.push_back(StepSpec{0, 5UL*60UL*1000UL, 0});
  s0.steps.push_back(StepSpec{1, 5UL*60UL*1000UL, 0});
  s0.steps.push_back(StepSpec{2, 5UL*60UL*1000UL, 0});
  c.program.sets.push_back(s0);
}

// =================== Persistencia de ESTADOS ===================
static bool loadRelayStates(std::vector<RelayState>& out) {
  out.clear();
  if (!statesPrefs.begin("states", /*ro*/ true)) return false;
  uint8_t cnt = statesPrefs.getUChar("count", 0);
  for (uint8_t i=0; i<cnt; ++i) {
    RelayState rs;
    rs.name       = statesPrefs.getString((String("s")+i+"_name").c_str(), String("Estado ") + String(i));
    rs.alwaysOn   = statesPrefs.getUChar ((String("s")+i+"_always").c_str(), 0) != 0;
    rs.alwaysOn12 = statesPrefs.getUChar ((String("s")+i+"_a12").c_str(),    0) != 0;
    rs.mainsMask  = statesPrefs.getUShort((String("s")+i+"_mm").c_str(), 0);
    rs.secsMask   = statesPrefs.getUShort((String("s")+i+"_sm").c_str(), 0);
    out.push_back(rs);
  }
  statesPrefs.end();
  return true;
}

static bool saveRelayStates(const std::vector<RelayState>& v) {
  if (!statesPrefs.begin("states", /*ro*/ false)) return false;
  statesPrefs.clear();
  statesPrefs.putUChar("count", (uint8_t)min<size_t>(v.size(), 60));
  for (size_t i=0; i<v.size() && i<60; ++i) {
    const auto& rs = v[i];
    statesPrefs.putString((String("s")+i+"_name").c_str(), rs.name);
    statesPrefs.putUChar ((String("s")+i+"_always").c_str(), rs.alwaysOn ? 1 : 0);
    statesPrefs.putUChar ((String("s")+i+"_a12").c_str(),    rs.alwaysOn12 ? 1 : 0);
    statesPrefs.putUShort((String("s")+i+"_mm").c_str(), rs.mainsMask);
    statesPrefs.putUShort((String("s")+i+"_sm").c_str(), rs.secsMask);
  }
  statesPrefs.end();
  return true;
}

static void ensureStateDefaults(std::vector<RelayState>& out) {
  if (!out.empty()) return;
  out.clear();
  {
    RelayState a;
    a.name = "Zona 1";
    a.alwaysOn = true; a.alwaysOn12 = true;
    a.mainsMask = (1u << 0);
    a.secsMask  = (1u << 0);
    out.push_back(a);
  }
  {
    RelayState b;
    b.name = "Zona 2";
    b.alwaysOn = true; b.alwaysOn12 = true;
    b.mainsMask = (1u << 1);
    b.secsMask  = (1u << 0);
    out.push_back(b);
  }
  {
    RelayState c_;
    c_.name = "Zona 3";
    c_.alwaysOn = true; c_.alwaysOn12 = true;   // <-- FIX: antes decía a.alwaysOn12
    c_.mainsMask = (1u << 2);
    c_.secsMask  = (1u << 1);
    out.push_back(c_);
  }
  saveRelayStates(out);
}

// ====== (NUEVO) Mapear RelayState -> StepSpec.idx y sincronizar Set 0 ======
static int firstMainBit(uint16_t mm) {
  for (int i = 0; i < HW_NUM_MAINS; ++i) if (mm & (1u << i)) return i;
  return -1;
}
static bool secIsDirect(uint16_t sm) {
  // En tu hardware: direct => S1=ON, S0=OFF ; alterno => S0=ON, S1=OFF
  if (sm & (1u<<1)) return true;   // S1 activo -> direct
  if (sm & (1u<<0)) return false;  // S0 activo -> alterno
  return true; // por defecto
}
static int relayStateToStepIdx(const RelayState& rs) {
  int m = firstMainBit(rs.mainsMask);
  if (m < 0) m = 0; // fallback
  bool direct = secIsDirect(rs.secsMask);
  return m*2 + (direct ? 0 : 1);
}
static void ensureStepsCoverAllStates() {
  if (gIrrCfg.program.sets.empty()) {
    StepSet s; s.name = "Default"; s.pauseMsBetweenSteps = 10000;
    gIrrCfg.program.sets.push_back(s);
  }
  StepSet& s0 = gIrrCfg.program.sets[0];

  // Si ya coincide el tamaño, no tocamos para no pisar ajustes finos
  if (s0.steps.size() == gStates.size()) return;

  s0.steps.clear();
  s0.steps.reserve(gStates.size());
  for (size_t i = 0; i < gStates.size(); ++i) {
    StepSpec sp;
    sp.idx           = relayStateToStepIdx(gStates[i]);
    sp.maxDurationMs = 0; // objetivos reales vendrán por NVS "zones"
    sp.targetMl      = 0;
    s0.steps.push_back(sp);
  }

  // Persistir y reinyectar al motor sin reiniciar
  saveIrrConfig(gIrrCfg);
  modesSetProgram(gIrrCfg.program, gIrrCfg.flowCal);
}

static void applyAndSave() {
  saveIrrConfig(gIrrCfg);
  delay(50);
  ESP.restart();
}

// =================== Override de modo: helpers ===================
static void loadModeOverride(bool& outOvr, bool& outManual) {
  Preferences p;
  outOvr = false; outManual = false;
  if (p.begin(NS_MODE, /*ro*/ true)) {
    outOvr    = p.getUChar("ovr", 0) != 0;
    outManual = p.getUChar("manual", 0) != 0;
    p.end();
  }
}

// =================== TASK RIEGO ===================
static void irrigationTask(void* /*pv*/) {
  pinMode(PIN_SWITCH_MANUAL, INPUT_PULLDOWN);

  // Lee override inicial
  loadModeOverride(gOvrEnabled, gOvrManual);

  bool rawHW      = (digitalRead(PIN_SWITCH_MANUAL) == HIGH);
  bool debHW      = rawHW;               // estado debounced del switch
  uint32_t lastCh = millis();

  // Estado de operación actual
  bool manual = gOvrEnabled ? gOvrManual : debHW;
  if (manual) resetFullMode(); else resetBlinkMode();

  uint32_t lastModeCheck = millis();

  for (;;) {
    uint32_t now = millis();

    // Debounce del switch físico (sólo si no hay override)
    bool newRaw = (digitalRead(PIN_SWITCH_MANUAL) == HIGH);
    if (newRaw != rawHW) { rawHW = newRaw; lastCh = now; }
    if (!gOvrEnabled && (now - lastCh) > MODE_DEBOUNCE_MS) {
      if (debHW != rawHW) debHW = rawHW;
    }

    // Refrescar override desde NVS cada ~500 ms
    if (now - lastModeCheck > 500) {
      lastModeCheck = now;
      bool ovr, man;
      loadModeOverride(ovr, man);
      if (ovr != gOvrEnabled || man != gOvrManual) {
        gOvrEnabled = ovr;
        gOvrManual  = man;
      }
    }

    // Decide modo deseado (override tiene prioridad)
    bool desiredManual = gOvrEnabled ? gOvrManual : debHW;
    if (desiredManual != manual) {
      manual = desiredManual;
      if (manual) resetFullMode(); else resetBlinkMode();
    }

    // Ejecuta modo actual
    if (manual) runFullMode();
    else        runBlinkMode();

    vTaskDelay(1);
  }
}

// =================== AP + mDNS ===================
static void startApAndMdns() {
  WiFi.mode(WIFI_AP_STA);
  const char* AP_SSID = "config";
  const char* AP_PASS = "password";   // WPA2 (>=8)
  (void)WiFi.softAP(AP_SSID, AP_PASS);

  if (MDNS.begin("config")) {
    MDNS.addService("http", "tcp", 80);
  }
}

// =================== setup/loop ===================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(50);

  // Cargar config MQTT
  cfgStore.load(cfg);

  // Cargar config de riego (o defaults si no hay)
  if (!loadIrrConfig(gIrrCfg)) {
    ensureIrrDefaults(gIrrCfg);
    saveIrrConfig(gIrrCfg);
  }

  // **ENLACE NUEVO**: pasar programa/calibración vivos al motor Auto
  modesSetProgram(gIrrCfg.program, gIrrCfg.flowCal);

  // Cargar catálogo de ESTADOS (o crear defaults si vacío)
  if (!loadRelayStates(gStates)) {
    gStates.clear();
  }
  ensureStateDefaults(gStates);

  // 🔧 Asegurar que el programa tenga un paso por cada zona
  ensureStepsCoverAllStates();

  // Zona horaria + NTP
  setenv("TZ", gIrrCfg.tz.c_str(), 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.windows.com");

  // Iniciar Wi-Fi Manager sin tocar UART0
  static NullStream nullIO;
  wifi.begin(&nullIO, SERIAL_CLI_TIMEOUT_MS);

  // MQTT chat
  chat.setServer(cfg.host, cfg.port);
  chat.setAuth(cfg.user, cfg.pass);
  chat.setTopic(cfg.topic);
  chat.setSubTopic(cfg.subTopic);
  chat.begin();

  // ===== NUEVO: cableado de publicación de eventos de riego =====
  modesSetEventPublisher(
    [&](const String& topic, const String& payload){
      (void)chat.publishTo(topic, payload);
    },
    String(RIEGO_TOPIC)
  );

  // ===== NUEVO: resolver nombre de estado (por índice de paso dentro del set) =====
  modesSetStateNameResolver([&](int stepIdx)->String{
    if (stepIdx >= 0 && stepIdx < (int)gStates.size() && gStates[stepIdx].name.length()) {
      return gStates[stepIdx].name;
    }
    return String("Paso ")+String(stepIdx);
  });

  // Web UI
  static WebUI ui(server, chat, cfg, cfgStore);
  webui = &ui;
  webui->begin();
  webui->attachMqttSink();

  // API de Programación para WebUI (/sched)
  webui->attachScheduleAPI(
    [](){ return gIrrCfg.program.enabled; },
    [](bool en){ gIrrCfg.program.enabled = en; applyAndSave(); },
    [](){ return gIrrCfg.program.starts; },
    [](const StartSpec& st){ gIrrCfg.program.starts.push_back(st); applyAndSave(); return true; },
    [](unsigned int idx){
      if (idx >= gIrrCfg.program.starts.size()) return false;
      gIrrCfg.program.starts.erase(gIrrCfg.program.starts.begin()+idx);
      applyAndSave(); return true;
    },
    [](){
      if (gIrrCfg.program.sets.empty()) return std::vector<StepSpec>{};
      return gIrrCfg.program.sets[0].steps;
    },
    [](const std::vector<StepSpec>& v){
      if (gIrrCfg.program.sets.empty()) {
        StepSet s; s.name="Default"; s.pauseMsBetweenSteps=10000;
        gIrrCfg.program.sets.push_back(s);
      }
      gIrrCfg.program.sets[0].steps = v;
      applyAndSave(); return true;
    },
    [](){ return nowString(); },
    [](const std::vector<StartSpec>& v){
      gIrrCfg.program.starts = v;
      applyAndSave(); return true;
    }
  );

  // API de ESTADOS (/states)
  webui->attachStateAPI(
    [](){ return gStates; },
    [](const std::vector<RelayState>& v){
      gStates = v;
      return saveRelayStates(gStates);
    },
    [](){ return std::make_pair(HW_NUM_MAINS, HW_NUM_SECS); },
    [](int idx, bool isMain){
      if (isMain) return String("M") + String(idx);
      else        return String("S") + String(idx);
    }
  );

  // AP “config” + mDNS “config.local”
  startApAndMdns();

  // Intenta conectar STA a la red marcada como automática
  (void)tryAutoConnectFromPrefs();

  // Task de riego (core 0, prioridad baja)
  xTaskCreatePinnedToCore(irrigationTask, "irrigationTask", 6144, nullptr, 1, &gIrrigationTask, 0);
}

void loop() {
  wifi.service();                      // Wi-Fi Manager (CLI)
  if (wifi.isConnected()) chat.loop(); // MQTT si hay Wi-Fi
  if (webui) webui->loop();            // HTTP

  // Reintento suave de Wi-Fi cada 10 s si está caído
  static unsigned long lastWiFiKick = 0;
  unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED && now - lastWiFiKick > 10000UL) {
    lastWiFiKick = now;
    if (!WiFi.reconnect()) {
      (void)tryAutoConnectFromPrefs();
    }
  }

  delay(2);
}

// File: src/main.cpp
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

// =================== HELPERS: Tiempo ===================
static String nowString() {
  time_t now = time(nullptr);
  if (now < 1600000000) return F("(sin hora)");
  struct tm tm; localtime_r(&now, &tm);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
  return String(buf);
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

  // Guardar sólo el StepSet 0 (la WebUI actual edita ese set)
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
      irrPrefs.putInt ((String("p")+i+"_idx").c_str(), s0.steps[i].idx);          // idx = índice de estado del catálogo
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
  (void)irrPrefs.getUChar("set_cnt", 1); // por ahora sólo usamos el 0

  StepSet s0;
  s0.name = irrPrefs.getString("set0_name", "Default");
  s0.pauseMsBetweenSteps = irrPrefs.getUInt("set0_pause", 10000);

  uint8_t pc = irrPrefs.getUChar("p_cnt", 0);
  for (uint8_t i=0; i<pc; ++i) {
    StepSpec stp;
    stp.idx           = irrPrefs.getInt ((String("p")+i+"_idx").c_str(), 0);              // idx = índice de estado (catálogo)
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

  // Horarios por defecto: 05:00 y 17:30 todos los días, set 0, escalas 1.0
  uint8_t everyday = (DOW_MON|DOW_TUE|DOW_WED|DOW_THU|DOW_FRI|DOW_SAT|DOW_SUN);
  c.program.starts.clear();
  c.program.starts.push_back(StartSpec(5,  0, everyday, 0, true, 1.00f, 1.00f));
  c.program.starts.push_back(StartSpec(17,30, everyday, 0, true, 1.00f, 1.00f));

  // StepSet 0 inicial (los índices de steps apuntan a estados del catálogo gStates)
  c.program.sets.clear();
  StepSet s0;
  s0.name = "Default";
  s0.pauseMsBetweenSteps = 10000; // 10 s
  // Tres ejemplos (se corresponden con estados 0,1,2 creados en defaults de estados)
  s0.steps.push_back(StepSpec{0, 5UL*60UL*1000UL, 0}); // usa estado #0
  s0.steps.push_back(StepSpec{1, 5UL*60UL*1000UL, 0}); // usa estado #1
  s0.steps.push_back(StepSpec{2, 5UL*60UL*1000UL, 0}); // usa estado #2
  c.program.sets.push_back(s0);
}

// =================== Persistencia de ESTADOS ===================
// Namespace: "states"
// Claves:
//   count                         (UChar)
//   s{i}_name                     (String)
//   s{i}_always, s{i}_a12         (UChar 0/1)
//   s{i}_mm, s{i}_sm              (UShort: máscaras mains/secs)
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

// Defaults del catálogo de estados si está vacío.
static void ensureStateDefaults(std::vector<RelayState>& out) {
  if (!out.empty()) return;
  out.clear();
  {
    RelayState a;
    a.name = "Zona 1";
    a.alwaysOn = true; a.alwaysOn12 = true;
    a.mainsMask = (1u << 0);  // M0 ON
    a.secsMask  = (1u << 0);  // S0 ON
    out.push_back(a);
  }
  {
    RelayState b;
    b.name = "Zona 2";
    b.alwaysOn = true; b.alwaysOn12 = true;
    b.mainsMask = (1u << 1);  // M1 ON
    b.secsMask  = (1u << 0);
    out.push_back(b);
  }
  {
    RelayState c;
    c.name = "Zona 3";
    c.alwaysOn = true; c.alwaysOn12 = true;
    c.mainsMask = (1u << 2);  // M2 ON
    c.secsMask  = (1u << 1);  // S1 ON
    out.push_back(c);
  }
  saveRelayStates(out);
}

// Aplica y salva; reinicia para que AutoMode tome el nuevo programa (no reinicia por cambios en estados)
static void applyAndSave() {
  saveIrrConfig(gIrrCfg);
  delay(50);
  ESP.restart();
}

// =================== TASK RIEGO ===================
static void irrigationTask(void* /*pv*/) {
  pinMode(PIN_SWITCH_MANUAL, INPUT_PULLDOWN);

  bool lastRaw    = (digitalRead(PIN_SWITCH_MANUAL) == HIGH);
  uint32_t lastCh = millis();
  bool manual     = lastRaw;

  if (manual) resetFullMode(); else resetBlinkMode();

  for (;;) {
    bool raw = (digitalRead(PIN_SWITCH_MANUAL) == HIGH);
    uint32_t now = millis();
    if (raw != lastRaw && (now - lastCh) > MODE_DEBOUNCE_MS) {
      lastRaw = raw; lastCh = now;
      bool newManual = raw;
      if (newManual != manual) {
        manual = newManual;
        if (manual) resetFullMode(); else resetBlinkMode();
      }
    }

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
  // Cargar config MQTT
  cfgStore.load(cfg);

  // Cargar config de riego (o defaults si no hay)
  if (!loadIrrConfig(gIrrCfg)) {
    ensureIrrDefaults(gIrrCfg);
    saveIrrConfig(gIrrCfg);
  }

  // Cargar catálogo de ESTADOS (o crear defaults si vacío)
  if (!loadRelayStates(gStates)) {
    gStates.clear();
  }
  ensureStateDefaults(gStates);

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

  // Web UI
  static WebUI ui(server, chat, cfg, cfgStore);
  webui = &ui;
  webui->begin();
  webui->attachMqttSink();

  // API de Programación para WebUI (/sched)
  webui->attachScheduleAPI(
    // getProgramEnabled
    [](){ return gIrrCfg.program.enabled; },
    // setProgramEnabled
    [](bool en){ gIrrCfg.program.enabled = en; applyAndSave(); },
    // getStarts
    [](){ return gIrrCfg.program.starts; },
    // addStart
    [](const StartSpec& st){ gIrrCfg.program.starts.push_back(st); applyAndSave(); return true; },
    // deleteStart
    [](unsigned int idx){
      if (idx >= gIrrCfg.program.starts.size()) return false;
      gIrrCfg.program.starts.erase(gIrrCfg.program.starts.begin()+idx);
      applyAndSave(); return true;
    },
    // getSteps (StepSet 0)
    [](){
      if (gIrrCfg.program.sets.empty()) return std::vector<StepSpec>{};
      return gIrrCfg.program.sets[0].steps;
    },
    // setSteps (StepSet 0)
    [](const std::vector<StepSpec>& v){
      if (gIrrCfg.program.sets.empty()) {
        StepSet s; s.name="Default"; s.pauseMsBetweenSteps=10000;
        gIrrCfg.program.sets.push_back(s);
      }
      gIrrCfg.program.sets[0].steps = v;
      applyAndSave(); return true;
    },
    // getNowStr
    [](){ return nowString(); },
    // setStarts (actualización masiva con set/ts/vs/en)
    [](const std::vector<StartSpec>& v){
      gIrrCfg.program.starts = v;
      applyAndSave(); return true;
    }
  );

  // API de ESTADOS (/states)
  webui->attachStateAPI(
    // getter
    [](){ return gStates; },
    // setter (guarda en RAM + persistencia)
    [](const std::vector<RelayState>& v){
      gStates = v;
      return saveRelayStates(gStates);
    },
    // counts (num mains, num secs)
    [](){ return std::make_pair(HW_NUM_MAINS, HW_NUM_SECS); },
    // nombres de columnas (opcional)
    [](int idx, bool isMain){
      if (isMain) return String("M") + String(idx);
      else        return String("S") + String(idx);
    }
  );

  // AP “config” + mDNS “config.local”
  startApAndMdns();

  // Task de riego (core 0, prioridad baja)
  xTaskCreatePinnedToCore(irrigationTask, "irrigationTask", 6144, nullptr, 1, &gIrrigationTask, 0);
}

void loop() {
  wifi.service();                 // Wi-Fi
  if (wifi.isConnected()) chat.loop(); // MQTT
  if (webui) webui->loop();       // HTTP
  delay(2);
}

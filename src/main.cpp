#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "WiFiManagerESP32.h"
#include "MqttChat.h"

// Silenciar logs IDF (opcional)
#include "esp_log.h"
#ifndef MUTE_IDF_LOGS
#define MUTE_IDF_LOGS 0
#endif

#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif
#ifndef SERIAL_CLI_TIMEOUT_MS
#define SERIAL_CLI_TIMEOUT_MS 120000UL
#endif

WiFiManagerESP32 wifi;
Preferences      prefsMQTT;

// ===== Config MQTT por defecto (idéntico a tu mosquitto_pub) =====
struct MqttCfg {
  String host  = "mqtt.arandanosdemipueblo.online";
  uint16_t port= 8883;
  String user  = "Elisaul";
  String pass  = "Elisaulp";
  String topic = "public/chat";
} cfg;

MqttChat chat(cfg.host.c_str(), cfg.port, cfg.user.c_str(), cfg.pass.c_str(), cfg.topic.c_str());

// ---------- util: leer línea no bloqueante (ahora retorna también líneas vacías) ----------
static bool readLineNonBlocking(String& out, size_t maxLen = 512) {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\r' || c == '\n') {
      // Swallow CRLF/ LFCR
      if (Serial.available()) {
        int p = Serial.peek();
        if ((c == '\r' && p == '\n') || (c == '\n' && p == '\r')) (void)Serial.read();
      }
      out = buf;   // puede ser cadena vacía
      buf = "";
      return true;
    }

    if (isPrintable(c)) {
      if (buf.length() < maxLen) buf += c;
    }
  }
  return false;
}

// ---------- MQTT Prefs ----------
static void loadMqttPrefs() {
  if (!prefsMQTT.begin("mqtt", true)) return;
  cfg.host  = prefsMQTT.getString("host", cfg.host);
  cfg.port  = (uint16_t)prefsMQTT.getUShort("port", cfg.port);
  cfg.user  = prefsMQTT.getString("user", cfg.user);
  cfg.pass  = prefsMQTT.getString("pass", cfg.pass);
  cfg.topic = prefsMQTT.getString("topic", cfg.topic);
  prefsMQTT.end();
}
static void saveMqttPrefs() {
  if (!prefsMQTT.begin("mqtt", false)) return;
  prefsMQTT.putString("host",  cfg.host);
  prefsMQTT.putUShort("port",  cfg.port);
  prefsMQTT.putString("user",  cfg.user);
  prefsMQTT.putString("pass",  cfg.pass);
  prefsMQTT.putString("topic", cfg.topic);
  prefsMQTT.end();
}

// ---------- anexar el “MQTT MENU” bajo el menú Wi-Fi ----------
static void printMqttAnnex() {
  Serial.println(F("===== MQTT MENU ====="));
  Serial.println(F("[y] Abrir menú MQTT"));
  Serial.println();
  Serial.print(F("> "));
}

// Muestra ambos menús (Wi-Fi + anexo MQTT)
static void showRootMenusOnce() {
  wifi.showMenu();   // imprime el menú Wi-Fi y su prompt
  printMqttAnnex();  // añadimos el bloque MQTT y prompt
}
static void safeShowMenusTwice() {
  showRootMenusOnce();
  delay(250);
  showRootMenusOnce();
}

// ---------- Menú de Chat (comandos con slash) ----------
static void chatPrintMenu() {
  Serial.println();
  Serial.println(F("----- CHAT MQTT -----"));
  Serial.println(F("Comandos:"));
  Serial.println(F("  /m        -> pedir mensaje y publicar"));
  Serial.println(F("  /s        -> estado MQTT/WiFi/tópico"));
  Serial.println(F("  /e        -> salir al menú MQTT"));
  Serial.println(F("  (cualquier otra línea se publica tal cual)"));
  Serial.print (F("Topic actual: ")); Serial.println(cfg.topic);
  Serial.println(F("---------------------"));
  Serial.print(F("(chat)> "));
}

// Estados de submenús
enum MqttMenuState {
  MQTT_MENU,
  MQTT_EDIT_HOST,  // paso 1 (host) luego pide puerto
  MQTT_EDIT_PORT,  // paso 2 (port)
  MQTT_EDIT_USER,
  MQTT_EDIT_PASS,
  MQTT_EDIT_TOPIC,
  MQTT_CHAT_MENU,
  MQTT_CHAT_WAIT_MSG
};

void setup() {
  Serial.begin(SERIAL_BAUD);

#if MUTE_IDF_LOGS
  esp_log_level_set("*", ESP_LOG_NONE);
  Serial.setDebugOutput(false);
#endif

  // Espera a que el monitor agarre
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0) < 2500) { delay(10); }
  delay(150);

  Serial.println();
  Serial.println(F("=== Riego ESP32 - WiFi + MQTT ==="));

  // Cargar configuración MQTT persistida (si existe)
  loadMqttPrefs();

  // Inicia Wi-Fi
  wifi.begin(&Serial, SERIAL_CLI_TIMEOUT_MS);

  // Reconfigura chat con prefs (por si cambiaron)
  chat.setServer(cfg.host, cfg.port);
  chat.setAuth(cfg.user, cfg.pass);
  chat.setTopic(cfg.topic);
  chat.begin();

  // Muestra los menús
  safeShowMenusTwice();
}

void loop() {
  static bool inMqttMenu = false;
  static MqttMenuState state = MQTT_MENU;

  // --- Entrada al menú MQTT desde el menú principal (tecla 'y'/'Y') ---
  if (!inMqttMenu) {
    bool appendAnnexAfterHandle = false;

    if (Serial.available()) {
      int pk = Serial.peek();

      // y/Y -> entrar al menú MQTT (consumimos nosotros la línea)
      if (pk == 'y' || pk == 'Y') {
        String l; if (readLineNonBlocking(l)) {
          inMqttMenu = true;
          state = MQTT_MENU;
          Serial.println();
          Serial.println(F("===== MQTT MENU ====="));
          Serial.println(F("[h] Host/puerto"));
          Serial.println(F("[u] Usuario"));
          Serial.println(F("[p] Contraseña"));
          Serial.println(F("[t] Tópico"));
          Serial.println(F("[c] Chat MQTT"));
          Serial.println(F("[s] Estado"));
          Serial.println(F("[e] Volver al menú Wi-Fi"));
          Serial.println(F("---------------------"));
          Serial.printf ("broker: %s:%u  user: %s  topic: %s\n",
                         cfg.host.c_str(), cfg.port, cfg.user.c_str(), cfg.topic.c_str());
          Serial.print  (F("(mqtt)> "));
          return;
        }
      }

      // q/Q -> el WiFiManager reimprime su menú; al terminar, anexamos nuestro bloque
      if (pk == 'q' || pk == 'Q') {
        appendAnnexAfterHandle = true;
      }
    }

    // Menú Wi-Fi normal
    wifi.handleSerial();
    wifi.service();

    if (appendAnnexAfterHandle) {
      // Añadimos nuestro bloque debajo del menú Wi-Fi reimpreso
      printMqttAnnex();
    }

    // Mantén MQTT vivo (por si luego entras a estado/chat)
    if (wifi.isConnected()) chat.loop();

    delay(2);
    return;
  }

  // --- Estamos en el MENÚ MQTT ---
  wifi.service();
  if (wifi.isConnected()) chat.loop();

  String line;
  if (!readLineNonBlocking(line)) { delay(2); return; }
  line.trim();

  // Subestados de edición (con /k para mantener)
  switch (state) {
    case MQTT_EDIT_HOST: {
      if (line == "/k") {
        // keep
      } else if (line.length()) {
        cfg.host = line;
      }
      chat.setServer(cfg.host, cfg.port);
      saveMqttPrefs();

      // Paso 2: pedir puerto
      Serial.printf("Puerto actual: %u\n", cfg.port);
      Serial.print (F("Nuevo puerto (1-65535) o /k para mantener: "));
      state = MQTT_EDIT_PORT;
      return;
    }
    case MQTT_EDIT_PORT: {
      if (line == "/k") {
        // keep
      } else {
        long p = line.toInt();
        if (p >= 1 && p <= 65535) {
          cfg.port = (uint16_t)p;
        } else {
          Serial.println(F("[mqtt] puerto inválido, se mantiene el actual"));
        }
      }
      chat.setServer(cfg.host, cfg.port);
      saveMqttPrefs();
      Serial.printf("[mqtt] broker -> %s:%u\n", cfg.host.c_str(), cfg.port);
      state = MQTT_MENU;
      Serial.print(F("(mqtt)> "));
      return;
    }
    case MQTT_EDIT_USER: {
      if (line != "/k" && line.length()) cfg.user = line;
      chat.setAuth(cfg.user, cfg.pass);
      saveMqttPrefs();
      Serial.printf("[mqtt] user -> %s\n", cfg.user.c_str());
      state = MQTT_MENU;
      Serial.print(F("(mqtt)> "));
      return;
    }
    case MQTT_EDIT_PASS: {
      if (line != "/k" && line.length()) cfg.pass = line;
      chat.setAuth(cfg.user, cfg.pass);
      saveMqttPrefs();
      Serial.println(F("[mqtt] pass -> (actualizada)"));
      state = MQTT_MENU;
      Serial.print(F("(mqtt)> "));
      return;
    }
    case MQTT_EDIT_TOPIC: {
      if (line != "/k" && line.length()) cfg.topic = line;
      chat.setTopic(cfg.topic);
      saveMqttPrefs();
      Serial.printf("[mqtt] topic -> %s\n", cfg.topic.c_str());
      state = MQTT_MENU;
      Serial.print(F("(mqtt)> "));
      return;
    }

    // Esperando el texto del mensaje para publicar
    case MQTT_CHAT_WAIT_MSG: {
      if (!line.length()) {
        // Enter vacío: reimprimir menú de chat
        state = MQTT_CHAT_MENU;
        chatPrintMenu();
        return;
      }
      if (wifi.isConnected() && chat.publish(line)) {
        Serial.print(F("[mqtt-> ")); Serial.print(cfg.topic); Serial.print(F("] "));
        Serial.println(line);
      } else {
        Serial.println(F("[chat] no publicado: MQTT/Wi-Fi no conectado"));
      }
      state = MQTT_CHAT_MENU;
      Serial.print(F("(chat)> "));
      return;
    }
    default: break;
  }

  // Estado principal del menú MQTT
  if (state == MQTT_MENU) {

    // === NUEVO: Enter vacío => reimprimir el menú MQTT ===
    if (!line.length()) {
      Serial.println();
      Serial.println(F("===== MQTT MENU ====="));
      Serial.println(F("[h] Host/puerto"));
      Serial.println(F("[u] Usuario"));
      Serial.println(F("[p] Contraseña"));
      Serial.println(F("[t] Tópico"));
      Serial.println(F("[c] Chat MQTT"));
      Serial.println(F("[s] Estado"));
      Serial.println(F("[e] Volver al menú Wi-Fi"));
      Serial.println(F("---------------------"));
      Serial.printf ("broker: %s:%u  user: %s  topic: %s\n",
                     cfg.host.c_str(), cfg.port, cfg.user.c_str(), cfg.topic.c_str());
      Serial.print  (F("(mqtt)> "));
      return;
    }
    // === FIN NUEVO ===

    if (line.length() == 1) {
      switch (line[0]) {
        case 'h': case 'H':
          Serial.printf("Host actual: %s\n", cfg.host.c_str());
          Serial.print  (F("Nuevo host o /k para mantener: "));
          state = MQTT_EDIT_HOST;
          return;
        case 'u': case 'U':
          Serial.printf("Usuario actual: %s\n", cfg.user.c_str());
          Serial.print  (F("Nuevo usuario o /k para mantener: "));
          state = MQTT_EDIT_USER;
          return;
        case 'p': case 'P':
          Serial.println(F("Introduce nueva contraseña o /k para mantener:"));
          state = MQTT_EDIT_PASS;
          return;
        case 't': case 'T':
          Serial.printf("Tópico actual: %s\n", cfg.topic.c_str());
          Serial.print  (F("Nuevo tópico o /k para mantener: "));
          state = MQTT_EDIT_TOPIC;
          return;
        case 's': case 'S':
          Serial.println(chat.status());
          Serial.print(F("(mqtt)> "));
          return;
        case 'c': case 'C':
          // Submenú chat
          Serial.println();
          chatPrintMenu();
          state = MQTT_CHAT_MENU;
          return;
        case 'e': case 'E':
          // Volver al menú Wi-Fi
          inMqttMenu = false;
          safeShowMenusTwice();
          return;
      }
    }
    // No reconocido -> prompt
    Serial.print(F("(mqtt)> "));
    return;
  }

  // Menú del chat (comandos con slash)
  if (state == MQTT_CHAT_MENU) {
    // Enter vacío: reimprimir menú de chat
    if (!line.length()) {
      chatPrintMenu();
      return;
    }

    if (line == "/m" || line == "/M") {
      Serial.print(F("Mensaje: "));
      state = MQTT_CHAT_WAIT_MSG;
      return;
    }
    if (line == "/s" || line == "/S") {
      Serial.println(chat.status());
      Serial.print (F("(chat)> "));
      return;
    }
    if (line == "/e" || line == "/E") {
      // Salir del chat al menú MQTT
      state = MQTT_MENU;
      Serial.print(F("(mqtt)> "));
      return;
    }

    // Publicación directa si no es un comando
    if (line.length()) {
      if (wifi.isConnected() && chat.publish(line)) {
        Serial.print(F("[mqtt-> ")); Serial.print(cfg.topic); Serial.print(F("] "));
        Serial.println(line);
      } else {
        Serial.println(F("[chat] no publicado: MQTT/Wi-Fi no conectado"));
      }
    }
    Serial.print(F("(chat)> "));
    return;
  }
}

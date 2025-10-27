#include "MqttChat.h"

static String macToStr(const uint8_t mac[6]) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(b);
}

MqttChat::MqttChat(const char* host,
                   uint16_t port,
                   const char* user,
                   const char* pass,
                   const char* topic_default)
: host_(host ? host : ""),
  port_(port),
  user_(user ? user : ""),
  pass_(pass ? pass : ""),
  topic_(topic_default ? topic_default : "public/chat"),
  mqtt_(tls_) {}

void MqttChat::setRootCA(const char* ca_pem) { root_ca_pem_ = ca_pem; }

String MqttChat::makeClientId() const {
  uint8_t mac[6]; WiFi.macAddress(mac);
  String id = "esp32-chat-" + macToStr(mac);
  id.replace(":", "");
  id.toLowerCase();
  return id;
}

void MqttChat::begin() {
  mqtt_.setServer(host_.c_str(), port_);
  clientId_ = makeClientId();
  if (root_ca_pem_) tls_.setCACert(root_ca_pem_);
  else              tls_.setInsecure();    // simple para pruebas

  // Callback de mensajes entrantes
  mqtt_.setCallback([this](char* topic, uint8_t* payload, unsigned int len){
    if (!handler_) return;
    String t(topic);
    String p; p.reserve(len);
    for (unsigned int i = 0; i < len; ++i) p += static_cast<char>(payload[i]);
    handler_(t, p);
  });
}

bool MqttChat::ensureConnected() {
  if (mqtt_.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  const char* willTopic = "status/esp32-chat";
  const char* willMsg   = "offline";

  bool ok = mqtt_.connect(clientId_.c_str(),
                          user_.c_str(), pass_.c_str(),
                          willTopic, 0, false, willMsg);
  if (ok) {
    mqtt_.publish(willTopic, "online", false);
    subscribed_ = false; // tras reconexión, forzar re-subscribe si activo
  }
  return ok;
}

void MqttChat::loop() {
  if (WiFi.status() != WL_CONNECTED) return;
  (void)ensureConnected();

  // Suscribirse si es necesario (incluida re-suscripción tras reconexión)
  if (mqtt_.connected() && subActive_ && !subscribed_ && subTopic_.length()) {
    if (mqtt_.subscribe(subTopic_.c_str())) {
      subscribed_ = true;
    }
  }

  mqtt_.loop();
}

bool MqttChat::publish(const String& msg) {
  if (!ensureConnected()) return false;
  return mqtt_.publish(topic_.c_str(), msg.c_str(), false);
}

bool MqttChat::publishTo(const String& topic, const String& msg) {
  if (!ensureConnected()) return false;
  return mqtt_.publish(topic.c_str(), msg.c_str(), false);
}

void MqttChat::setTopic(const String& topic) {
  if (topic.length()) topic_ = topic;
}

void MqttChat::setSubTopic(const String& topic) {
  if (!topic.length()) return;
  // Si ya estoy suscrito, cambiar exige desuscribir y volver a suscribir
  if (subActive_ && mqtt_.connected() && subTopic_.length()) {
    mqtt_.unsubscribe(subTopic_.c_str());
  }
  subTopic_  = topic;
  subscribed_ = false; // forzar re-suscripción en siguiente loop()
}

void MqttChat::subscribe() {
  subActive_  = true;
  subscribed_ = false;
}

void MqttChat::unsubscribe() {
  if (mqtt_.connected() && subTopic_.length()) {
    mqtt_.unsubscribe(subTopic_.c_str());
  }
  subActive_  = false;
  subscribed_ = false;
}

String MqttChat::status() {
  String s = "[chat] ";
  s += WiFi.isConnected() ? "wifi:up " : "wifi:down ";
  s += mqtt_.connected() ? "mqtt:up " : "mqtt:down ";
  s += "broker=" + host_ + ":" + String(port_) + " ";
  s += "topic=" + topic_;
  if (subTopic_.length()) s += " sub=" + subTopic_;
  return s;
}

void MqttChat::setServer(const String& host, uint16_t port) {
  if (host.length()) host_ = host;
  port_ = port;
  mqtt_.setServer(host_.c_str(), port_);
}

void MqttChat::setAuth(const String& user, const String& pass) {
  user_ = user;
  pass_ = pass;
}

void MqttChat::onMessage(MqttChat::MessageHandler cb) {
  handler_ = cb;
}

// Opcional: ampliar el buffer para payloads JSON más grandes
void MqttChat::setBufferSize(size_t n) {
  mqtt_.setBufferSize(n);
}

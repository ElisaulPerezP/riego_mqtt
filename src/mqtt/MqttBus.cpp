#include "MqttBus.h"
#include <WiFi.h>

static String macToStr(const uint8_t mac[6]) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(b);
}

MqttBus::MqttBus(const String& host, uint16_t port,
                 const String& user, const String& pass)
: host_(host), port_(port), user_(user), pass_(pass), mqtt_(tls_) {}

void MqttBus::setServer(const String& host, uint16_t port) {
  host_ = host; port_ = port;
  mqtt_.setServer(host_.c_str(), port_);
}
void MqttBus::setAuth(const String& u, const String& p) { user_=u; pass_=p; }
void MqttBus::setRootCA(const char* ca_pem) { root_ca_ = ca_pem; }

String MqttBus::makeClientId() const {
  uint8_t mac[6]; WiFi.macAddress(mac);
  String id = "esp32-bus-" + macToStr(mac);
  id.replace(":", ""); id.toLowerCase();
  return id;
}

void MqttBus::begin() {
  if (root_ca_) tls_.setCACert(root_ca_);
  else          tls_.setInsecure();

  mqtt_.setServer(host_.c_str(), port_);
  mqtt_.setCallback([this](char* topic, uint8_t* payload, unsigned len){
    if (!handler_) return;
    String t(topic);
    String p; p.reserve(len);
    for (unsigned i=0;i<len;i++) p += (char)payload[i];
    handler_(t,p);
  });
}

bool MqttBus::connected() { return mqtt_.connected(); }

bool MqttBus::ensureConnected() {
  if (connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  const char* willTopic = "riego/status";
  const char* willMsg   = "offline";
  String cid = makeClientId();
  bool ok = mqtt_.connect(cid.c_str(), user_.c_str(), pass_.c_str(), willTopic, 0, false, willMsg);
  if (ok) mqtt_.publish(willTopic, "online", false);
  return ok;
}

void MqttBus::loop() {
  if (WiFi.status() != WL_CONNECTED) return;
  (void)ensureConnected();
  mqtt_.loop();
}

void MqttBus::subscribe(const String& topic) {
  if (ensureConnected()) mqtt_.subscribe(topic.c_str());
}

void MqttBus::onMessage(MessageHandler cb) { handler_ = cb; }

bool MqttBus::publish(const String& topic, const String& payload, bool retained) {
  if (!ensureConnected()) return false;
  return mqtt_.publish(topic.c_str(), payload.c_str(), retained);
}

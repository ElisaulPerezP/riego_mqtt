// File: src/web/WebUI_Mqtt.cpp
#include "web/WebUI.h"

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
  chat_.subscribe();

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
  unsigned long last = 0;
  if (server_.hasArg("last")) last = strtoul(server_.arg("last").c_str(), nullptr, 10);

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

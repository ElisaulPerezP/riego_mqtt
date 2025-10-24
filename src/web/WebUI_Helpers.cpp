// File: src/web/WebUI_Hellers.cpp
#include "web/WebUI.h"
#include <WiFi.h>

String WebUI::htmlHeader(const String& title) const {
  String ipSTA = (uint32_t)WiFi.localIP()   ? WiFi.localIP().toString()   : F("(sin IP)");
  String ipAP  = (uint32_t)WiFi.softAPIP()  ? WiFi.softAPIP().toString()  : F("(sin AP)");
  String s;
  s += F("<!doctype html><html><head><meta charset='utf-8'>");
  s += F("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  s += F("<title>"); s += title; s += F("</title>");
  s += F("<style>"
        "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;margin:16px}"
        "code,pre{background:#f4f4f4;padding:2px 4px;border-radius:4px}"
        "table{border-collapse:collapse}td,th{border:1px solid #ddd;padding:6px}"
        "input[type=checkbox]{transform:scale(1.2)} .rowform{display:inline}"
        ".btn{padding:6px 10px;border:1px solid #ccc;border-radius:6px;background:#fafafa;cursor:pointer}"
        ".btn-link{border:none;background:none;color:#06c;text-decoration:underline;cursor:pointer}"
        ".formcard{border:1px solid #ddd;border-radius:8px;padding:12px;margin:8px 0;background:#fff}"
        "</style>");
  s += F("</head><body>");
  s += F("<h2>Riego ESP32 - WebUI</h2>");
  s += F("<p>AP: <b>config</b> · mDNS: <a href='http://config.local/'>config.local</a></p>");
  s += F("<p>IP AP: <code>");  s += ipAP;  s += F("</code><br>");
  s += F("IP LAN (STA): <code>"); s += ipSTA; s += F("</code></p>");
  s += F("<nav><a href='/'>Home</a> · <a href='/states'>Estados</a> · <a href='/mode'>Modo</a> · "
         "<a href='/wifi/info'>WiFi</a> · <a href='/wifi/scan'>Escanear</a> · "
         "<a href='/wifi/saved'>Guardadas</a> · <a href='/mqtt'>MQTT</a></nav><hr/>");
  return s;
}

String WebUI::htmlFooter() const { return F("<hr/><small>WebUI minimal · ESP32</small></body></html>"); }

String WebUI::jsonEscape(String s) {
  s.replace("\\","\\\\");
  s.replace("\"","\\\"");
  s.replace("\r","\\r");
  s.replace("\n","\\n");
  s.replace("\t","\\t");
  return s;
}

void WebUI::pushMsg_(const String& t, const String& p) {
  Msg& m = inbox_[writePos_];
  m.topic = t; m.payload = p; m.ms = millis(); m.seq = ++seqCounter_;
  writePos_ = (writePos_ + 1) % MSG_BUF;
  if (used_ < MSG_BUF) used_++;
}

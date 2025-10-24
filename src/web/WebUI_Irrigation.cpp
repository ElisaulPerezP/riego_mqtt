// File: src/web/WebUI_Irrigation.cpp
#include "web/WebUI.h"

void WebUI::handleIrrigation() {
  String s = htmlHeader(F("Riego"));
  s += F("<h3>Estado (resumen)</h3>");
  s += F("<p>Ver <a href='/states'>Estados</a> para configurar combinaciones de relés.</p>");
  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}

void WebUI::handleIrrigationJson() {
  String out = F("{\"ok\":true}");
  server_.send(200, F("application/json"), out);
}

// File: src/web/WebUI_TimeWindows.cpp
#include "web/WebUI.h"
#include <Preferences.h>

// ========== Persistencia ==========

bool WebUI::loadTimeWindows(std::vector<TimeWindow>& out) {
  out.clear();
  Preferences p;
  if (!p.begin(NS_WINDOWS, /*ro*/ true)) return false;
  uint8_t cnt = p.getUChar("count", 0);
  for (uint8_t i = 0; i < cnt; ++i) {
    TimeWindow w;
    w.name = p.getString((String("w")+i+"_name").c_str(), String("Franja ")+String(i));
    w.sh   = p.getUChar ((String("w")+i+"_sh").c_str(), 0);
    w.sm   = p.getUChar ((String("w")+i+"_sm").c_str(), 0);
    w.eh   = p.getUChar ((String("w")+i+"_eh").c_str(), 0);
    w.em   = p.getUChar ((String("w")+i+"_em").c_str(), 0);
    out.push_back(w);
  }
  p.end();
  return true;
}

bool WebUI::saveTimeWindows(const std::vector<TimeWindow>& v) {
  Preferences p;
  if (!p.begin(NS_WINDOWS, /*ro*/ false)) return false;
  p.clear();
  uint8_t cnt = (uint8_t)((v.size() > 60) ? 60 : v.size());
  p.putUChar("count", cnt);
  for (uint8_t i = 0; i < cnt; ++i) {
    const TimeWindow& w = v[i];
    p.putString((String("w")+i+"_name").c_str(), w.name);
    p.putUChar ((String("w")+i+"_sh").c_str(),   w.sh);
    p.putUChar ((String("w")+i+"_sm").c_str(),   w.sm);
    p.putUChar ((String("w")+i+"_eh").c_str(),   w.eh);
    p.putUChar ((String("w")+i+"_em").c_str(),   w.em);
  }
  p.end();
  return true;
}

// ---- helpers ----
static inline int minutesOf(uint8_t h, uint8_t m) { return ((int)h)*60 + (int)m; }

// Intervalo semi-abierto [start, end), sin wrap nocturno (no se permite cruzar medianoche).
// NO hay solape si un intervalo termina exactamente cuando el otro empieza.
bool WebUI::windowsOverlap(const TimeWindow& a, const TimeWindow& b) const {
  int as = minutesOf(a.sh,a.sm), ae = minutesOf(a.eh,a.em);
  int bs = minutesOf(b.sh,b.sm), be = minutesOf(b.eh,b.em);

  // Si alguno es inválido (start >= end), lo consideramos solapado/invalidante.
  if (as >= ae || bs >= be) return true;

  // Solapan si as < be && bs < ae  (semi-abiertos)
  return (as < be) && (bs < ae);
}

bool WebUI::validateNoOverlap(const std::vector<TimeWindow>& v, const TimeWindow& cand, int ignoreIndex) const {
  int cs = minutesOf(cand.sh,cand.sm), ce = minutesOf(cand.eh,cand.em);
  if (cs >= ce) return false; // rango inválido

  for (size_t i=0;i<v.size();++i) {
    if ((int)i == ignoreIndex) continue;
    if (windowsOverlap(cand, v[i])) return false;
  }
  return true;
}

// ========== Pages/handlers ==========

void WebUI::handleWindowsPage() {
  std::vector<TimeWindow> ws;
  (void)loadTimeWindows(ws);

  String s = htmlHeader(F("Franjas horarias"));
  s += F("<h3>Franjas horarias</h3>");
  s += F("<p>Cada franja tiene <b>nombre</b>, <b>hora inicio</b> y <b>hora fin</b>. "
         "No se permiten solapes ni rangos invertidos (no cruza medianoche).</p>");

  s += F("<table><tr>"
         "<th>#</th><th>Nombre</th>"
         "<th>Inicio (HH:MM)</th><th>Fin (HH:MM)</th><th>Acciones</th>"
         "</tr>");

  // Filas existentes (editables)
  for (size_t i=0;i<ws.size();++i) {
    const auto& w = ws[i];
    s += F("<tr><td>"); s += String((int)i); s += F("</td><td>");
    s += F("<form class='rowform' method='post' action='/windows/save'>");
    s += F("<input type='hidden' name='idx' value='"); s += String((int)i); s += F("'>");
    s += F("<input name='name' value='"); s += w.name; s += F("' style='min-width:140px'>");
    s += F("</td><td>");
    // inicio
    s += F("<input name='sh' type='number' min='0' max='23' value='"); s += String((int)w.sh); s += F("' style='width:60px'>:");
    s += F("<input name='sm' type='number' min='0' max='59' value='"); s += String((int)w.sm); s += F("' style='width:60px'>");
    s += F("</td><td>");
    // fin
    s += F("<input name='eh' type='number' min='0' max='23' value='"); s += String((int)w.eh); s += F("' style='width:60px'>:");
    s += F("<input name='em' type='number' min='0' max='59' value='"); s += String((int)w.em); s += F("' style='width:60px'>");
    s += F("</td><td>");
    s += F("<button class='btn'>Guardar</button> ");
    s += F("</form> ");
    s += F("<form class='rowform' method='post' action='/windows/delete' onsubmit='return confirm(\"¿Eliminar franja?\")'>");
    s += F("<input type='hidden' name='idx' value='"); s += String((int)i); s += F("'>");
    s += F("<button class='btn'>Eliminar</button>");
    s += F("</form></td></tr>");
  }

  // Fila nueva
  s += F("<tr><td>+</td><td>");
  s += F("<form class='rowform' method='post' action='/windows/save'>");
  s += F("<input type='hidden' name='idx' value='-1'>");
  s += F("<input name='name' placeholder='Nueva franja' style='min-width:140px'>");
  s += F("</td><td>");
  s += F("<input name='sh' type='number' min='0' max='23' value='5' style='width:60px'>:");
  s += F("<input name='sm' type='number' min='0' max='59' value='0' style='width:60px'>");
  s += F("</td><td>");
  s += F("<input name='eh' type='number' min='0' max='23' value='6' style='width:60px'>:");
  s += F("<input name='em' type='number' min='0' max='59' value='0' style='width:60px'>");
  s += F("</td><td><button class='btn'>Agregar</button></td></tr>");
  s += F("</form>");

  s += F("</table>");
  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}

void WebUI::handleWindowsSave() {
  int idx = server_.hasArg("idx") ? server_.arg("idx").toInt() : -1;

  TimeWindow w;
  w.name = server_.hasArg("name") ? server_.arg("name") : String("");
  w.sh = (uint8_t)constrain(server_.hasArg("sh") ? server_.arg("sh").toInt() : 0, 0, 23);
  w.sm = (uint8_t)constrain(server_.hasArg("sm") ? server_.arg("sm").toInt() : 0, 0, 59);
  w.eh = (uint8_t)constrain(server_.hasArg("eh") ? server_.arg("eh").toInt() : 0, 0, 23);
  w.em = (uint8_t)constrain(server_.hasArg("em") ? server_.arg("em").toInt() : 0, 0, 59);

  if (w.name.length() == 0) {
    server_.send(400, F("text/plain"), F("Nombre vacío"));
    return;
  }

  std::vector<TimeWindow> ws;
  (void)loadTimeWindows(ws);

  // Validación anti-solape y rango válido
  if (!validateNoOverlap(ws, w, idx)) {
    server_.send(400, F("text/plain"), F("Franja solapada o rango inválido (inicio < fin, sin cruzar medianoche)."));
    return;
  }

  if (idx >= 0 && idx < (int)ws.size()) {
    ws[idx] = w;
  } else {
    ws.push_back(w);
  }

  bool ok = saveTimeWindows(ws);
  server_.sendHeader(F("Location"), "/windows");
  server_.send(302, F("text/plain"), ok ? "ok" : "fail");
}

void WebUI::handleWindowsDelete() {
  int idx = server_.hasArg("idx") ? server_.arg("idx").toInt() : -1;

  std::vector<TimeWindow> ws;
  (void)loadTimeWindows(ws);

  if (idx >= 0 && idx < (int)ws.size()) {
    ws.erase(ws.begin() + idx);
    (void)saveTimeWindows(ws);
  }

  server_.sendHeader(F("Location"), "/windows");
  server_.send(302, F("text/plain"), "");
}

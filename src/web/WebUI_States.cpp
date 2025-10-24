// File: src/web/WebUI_States.cpp
#include "web/WebUI.h"
#include <Preferences.h>

/* ===== Persistencia de parámetros por zona ===== */
bool WebUI::loadZoneParams(int idx, ZoneParams& out) {
  if (idx < 0) return false;
  Preferences p;
  if (!p.begin(NS_ZONES, true)) return false;
  String base = String("z") + String(idx) + "_";
  out.volumeMl = p.getUInt((base + "vol").c_str(), 0);
  out.timeMs   = p.getUInt((base + "time").c_str(), 0);
  out.fert1Pct = p.getUChar((base + "f1").c_str(), 0);
  out.fert2Pct = p.getUChar((base + "f2").c_str(), 0);
  p.end();
  return true;
}
bool WebUI::saveZoneParams(int idx, const ZoneParams& z) {
  if (idx < 0) return false;
  Preferences p;
  if (!p.begin(NS_ZONES, false)) return false;
  String base = String("z") + String(idx) + "_";
  p.putUInt ((base + "vol").c_str(),  z.volumeMl);
  p.putUInt ((base + "time").c_str(), z.timeMs);
  p.putUChar((base + "f1").c_str(),   z.fert1Pct);
  p.putUChar((base + "f2").c_str(),   z.fert2Pct);
  int count = p.getInt("count", 0);
  if (idx + 1 > count) p.putInt("count", idx + 1);
  p.end();
  return true;
}
bool WebUI::deleteZoneParams(int idx) {
  if (idx < 0) return false;
  Preferences p;
  if (!p.begin(NS_ZONES, false)) return false;
  String base = String("z") + String(idx) + "_";
  p.remove((base + "vol").c_str());
  p.remove((base + "time").c_str());
  p.remove((base + "f1").c_str());
  p.remove((base + "f2").c_str());
  p.end();
  return true;
}
int WebUI::getZonesCount() {
  Preferences p;
  if (!p.begin(NS_ZONES, true)) return 0;
  int c = p.getInt("count", 0);
  p.end();
  return c;
}
void WebUI::setZonesCount(int count) {
  Preferences p;
  if (!p.begin(NS_ZONES, false)) return;
  p.putInt("count", count < 0 ? 0 : count);
  p.end();
}
void WebUI::compactZonesAfterDelete(int deletedIdx, int newCount) {
  if (deletedIdx < 0) return;
  Preferences p;
  if (!p.begin(NS_ZONES, false)) return;

  for (int k = deletedIdx; k < newCount; ++k) {
    String src = String("z") + String(k+1) + "_";
    uint32_t vol  = p.getUInt ((src + "vol").c_str(),  0);
    uint32_t time = p.getUInt ((src + "time").c_str(), 0);
    uint8_t  f1   = p.getUChar((src + "f1").c_str(),   0);
    uint8_t  f2   = p.getUChar((src + "f2").c_str(),   0);

    String dst = String("z") + String(k) + "_";
    p.putUInt ((dst + "vol").c_str(),  vol);
    p.putUInt ((dst + "time").c_str(), time);
    p.putUChar((dst + "f1").c_str(),   f1);
    p.putUChar((dst + "f2").c_str(),   f2);
  }

  String last = String("z") + String(newCount) + "_";
  p.remove((last + "vol").c_str());
  p.remove((last + "time").c_str());
  p.remove((last + "f1").c_str());
  p.remove((last + "f2").c_str());

  p.putInt("count", newCount);
  p.end();
}

/* ===================== ESTADOS (tabla) ===================== */
void WebUI::handleStatesList() {
  if (!getStates_ || !getCounts_) {
    server_.send(500, F("text/plain"), F("States API no inicializada"));
    return;
  }
  std::vector<RelayState> st = getStates_();
  std::pair<int,int> counts = getCounts_();
  int numM = counts.first;
  int numS = counts.second;

  setZonesCount((int)st.size());

  String s = htmlHeader(F("Estados"));
  s += F("<h3>Estados de relés</h3>");
  s += F("<p>Cada fila es un <b>estado</b>; cada columna un relé. Marca los que deben encenderse en ese estado.</p>");
  s += F("<table><tr><th>#</th><th>Nombre</th><th>Always</th><th>Always12</th>");

  for (int i=0;i<numM;i++){
    s += F("<th>");
    if (relayNameGetter_) s += relayNameGetter_(i,true);
    else { s += F("M"); s += String(i); }
    s += F("</th>");
  }
  for (int j=0;j<numS;j++){
    s += F("<th>");
    if (relayNameGetter_) s += relayNameGetter_(j,false);
    else { s += F("S"); s += String(j); }
    s += F("</th>");
  }
  s += F("<th>Acciones</th></tr>");

  for (size_t r=0;r<st.size();r++){
    const RelayState& rs = st[r];
    s += F("<tr><td>"); s += String((int)r); s += F("</td><td>");
    s += F("<form class='rowform' method='post' action='/states/save'>");
    s += F("<input type='hidden' name='idx' value='"); s += String((int)r); s += F("'>");
    s += F("<input name='name' value='"); s += rs.name; s += F("' style='min-width:140px'>");
    s += F("</td><td>");
    s += F("<input type='checkbox' name='always' ");   if (rs.alwaysOn)  s += F("checked"); s += F(">");
    s += F("</td><td>");
    s += F("<input type='checkbox' name='a12' ");      if (rs.alwaysOn12) s += F("checked"); s += F(">");

    for (int i=0;i<numM;i++){
      bool on = (rs.mainsMask & (1u<<i)) != 0;
      s += F("</td><td><input type='checkbox' name='m"); s += String(i); s += F("' ");
      if (on) s += F("checked");
      s += F(">");
    }
    for (int j=0;j<numS;j++){
      bool on = (rs.secsMask & (1u<<j)) != 0;
      s += F("</td><td><input type='checkbox' name='s"); s += String(j); s += F("' ");
      if (on) s += F("checked");
      s += F(">");
    }

    s += F("</td><td>");
    s += F("<button class='btn'>Guardar</button> ");
    s += F("</form> ");
    s += F("<a class='btn' href='/states/edit?idx="); s += String((int)r); s += F("'>Configurar</a> ");
    s += F("<form class='rowform' method='post' action='/states/delete' onsubmit='return confirm(\"¿Eliminar estado? \")'>");
    s += F("<input type='hidden' name='idx' value='"); s += String((int)r); s += F("'>");
    s += F("<button class='btn'>Eliminar</button></form>");
    s += F("</td></tr>");
  }

  // fila para agregar
  s += F("<tr><td>+</td><td>");
  s += F("<form class='rowform' method='post' action='/states/save'>");
  s += F("<input type='hidden' name='idx' value='-1'>");
  s += F("<input name='name' placeholder='Nuevo estado' style='min-width:140px'>");
  s += F("</td><td><input type='checkbox' name='always'>");
  s += F("</td><td><input type='checkbox' name='a12'>");
  for (int i=0;i<numM;i++){ s += F("</td><td><input type='checkbox' name='m"); s += String(i); s += F("'>"); }
  for (int j=0;j<numS;j++){ s += F("</td><td><input type='checkbox' name='s"); s += String(j); s += F("'>"); }
  s += F("</td><td><button class='btn'>Agregar</button></td></tr>");
  s += F("</form>");

  s += F("</table>");
  s += htmlFooter();
  server_.send(200, F("text/html; charset=utf-8"), s);
}

void WebUI::handleStatesSave() {
  if (!getStates_ || !setStates_ || !getCounts_) {
    server_.send(500, F("text/plain"), F("States API no inicializada"));
    return;
  }
  int idx = server_.hasArg("idx") ? server_.arg("idx").toInt() : -1;
  std::vector<RelayState> st = getStates_();

  std::pair<int,int> counts = getCounts_();
  int numM = counts.first;
  int numS = counts.second;

  RelayState rs;
  if (server_.hasArg("name"))  rs.name = server_.arg("name");
  rs.alwaysOn   = server_.hasArg("always");
  rs.alwaysOn12 = server_.hasArg("a12");

  uint16_t mm = 0, sm = 0;
  for (int i=0;i<numM;i++) if (server_.hasArg("m"+String(i))) mm |= (1u<<i);
  for (int j=0;j<numS;j++) if (server_.hasArg("s"+String(j))) sm |= (1u<<j);
  rs.mainsMask = mm; rs.secsMask = sm;

  if (idx >= 0 && idx < (int)st.size()) st[idx] = rs;
  else st.push_back(rs);

  bool ok = setStates_(st);
  if (ok) setZonesCount((int)st.size());

  server_.sendHeader(F("Location"), "/states");
  server_.send(302, F("text/plain"), ok ? "ok" : "fail");
}

void WebUI::handleStatesDelete() {
  if (!getStates_ || !setStates_) {
    server_.send(500, F("text/plain"), F("States API no inicializada"));
    return;
  }
  int idx = server_.hasArg("idx") ? server_.arg("idx").toInt() : -1;
  std::vector<RelayState> st = getStates_();
  if (idx >= 0 && idx < (int)st.size()) {
    st.erase(st.begin() + idx);
    bool ok = setStates_(st);
    if (ok) {
      int newCount = (int)st.size();
      compactZonesAfterDelete(idx, newCount);
    }
  }
  server_.sendHeader(F("Location"), "/states");
  server_.send(302, F("text/plain"), "");
}

/* ===================== DETALLE DE ZONA ===================== */
void WebUI::handleStateEdit() {
  int idx = server_.hasArg("idx") ? server_.arg("idx").toInt() : -1;
  if (idx < 0) { server_.send(400, F("text/plain"), F("idx inválido")); return; }

  String zoneName = String("Zona ") + String(idx);
  if (getStates_) {
    auto v = getStates_();
    if (idx >= 0 && idx < (int)v.size() && v[idx].name.length()) zoneName = v[idx].name;
  }

  ZoneParams zp; loadZoneParams(idx, zp);

  String s = htmlHeader(F("Configurar zona"));
  s += F("<h3>Configurar zona</h3><div class='formcard'>");
  s += F("<p><b>Zona:</b> ");
  s += zoneName;
  s += F(" <small>(idx ");
  s += String(idx);
  s += F(")</small></p>");

  s += F("<form method='post' action='/states/edit/save'>");
  s += F("<input type='hidden' name='idx' value='");
  s += String(idx);
  s += F("'>");

  s += F("<p>Volumen total: <input type='number' name='vol' min='0' step='1' value='");
  s += String(zp.volumeMl);
  s += F("'> mL</p>");

  s += F("<p>Tiempo máximo: <input type='number' name='time' min='0' step='1000' value='");
  s += String(zp.timeMs);
  s += F("'> ms</p>");

  s += F("<p>p_fert_1 (0–100%): <input type='range' id='p1r' min='0' max='100' value='");
  s += String(zp.fert1Pct);
  s += F("' oninput='p1v.value=this.value'> ");
  s += F("<input type='number' id='p1v' name='p1' min='0' max='100' value='");
  s += String(zp.fert1Pct);
  s += F("' oninput='p1r.value=this.value'> %</p>");

  s += F("<p>p_fert_2 (0–100%): <input type='range' id='p2r' min='0' max='100' value='");
  s += String(zp.fert2Pct);
  s += F("' oninput='p2v.value=this.value'> ");
  s += F("<input type='number' id='p2v' name='p2' min='0' max='100' value='");
  s += String(zp.fert2Pct);
  s += F("' oninput='p2r.value=this.value'> %</p>");

  s += F("<p><button class='btn'>Guardar</button> ");
  s += F("<a class='btn' href='/states'>Volver</a></p>");
  s += F("</form></div>");
  s += htmlFooter();

  server_.send(200, F("text/html; charset=utf-8"), s);
}

void WebUI::handleStateEditSave() {
  int idx = server_.hasArg("idx") ? server_.arg("idx").toInt() : -1;
  if (idx < 0) { server_.send(400, F("text/plain"), F("idx inválido")); return; }

  ZoneParams z;
  z.volumeMl = server_.hasArg("vol") ? (uint32_t)strtoul(server_.arg("vol").c_str(), nullptr, 10) : 0;
  z.timeMs   = server_.hasArg("time")? (uint32_t)strtoul(server_.arg("time").c_str(), nullptr, 10) : 0;
  z.fert1Pct = server_.hasArg("p1")  ? (uint8_t)constrain(server_.arg("p1").toInt(), 0, 100) : 0;
  z.fert2Pct = server_.hasArg("p2")  ? (uint8_t)constrain(server_.arg("p2").toInt(), 0, 100) : 0;

  (void)saveZoneParams(idx, z);
  server_.sendHeader(F("Location"), "/states");
  server_.send(302, F("text/plain"), "");
}

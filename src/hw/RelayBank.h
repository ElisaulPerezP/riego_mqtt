#pragma once
#include "PinMap.h"

class RelayBank {
public:
  explicit RelayBank(const PinMap& map) : m_(map) {}

  void begin() {
    if (m_.pinAlwaysOn >= 0)       pinMode(m_.pinAlwaysOn, OUTPUT);
    if (m_.pinAlwaysOn12 >= 0)     pinMode(m_.pinAlwaysOn12, OUTPUT);
    if (m_.pinToggleNext >= 0)     pinMode(m_.pinToggleNext, OUTPUT);
    if (m_.pinTogglePrev >= 0)     pinMode(m_.pinTogglePrev, OUTPUT);
    for (int i=0;i<m_.numMains;i++) pinMode(m_.mainPins[i], OUTPUT);
    for (int j=0;j<m_.numSecs;j++)  pinMode(m_.secPins[j], OUTPUT);
    allMainsOff();
    allSecsOff();
    setAlways(false);
    setAlways12(false);
    setToggleNext(false);
    setTogglePrev(false);
  }

  void allMainsOff() {
    for (int i=0;i<m_.numMains;i++) digitalWrite(m_.mainPins[i], m_.mainActiveLow[i] ? HIGH : LOW);
  }
  void allSecsOff() {
    for (int j=0;j<m_.numSecs;j++) digitalWrite(m_.secPins[j], m_.secActiveLow[j] ? HIGH : LOW);
  }

  void setAlways(bool on)    { if (m_.pinAlwaysOn   >=0) digitalWrite(m_.pinAlwaysOn,   on ? HIGH : LOW); }
  void setAlways12(bool on)  { if (m_.pinAlwaysOn12 >=0) digitalWrite(m_.pinAlwaysOn12, on ? HIGH : LOW); }
  void setToggleNext(bool on){ if (m_.pinToggleNext >=0) digitalWrite(m_.pinToggleNext, on ? HIGH : LOW); }
  void setTogglePrev(bool on){ if (m_.pinTogglePrev >=0) digitalWrite(m_.pinTogglePrev, on ? HIGH : LOW); }

  // Aplica un “patrón” de MAIN: directo = solo 'm' encendida; complementario = todas menos 'm'
  void applyMainsPattern(int m, bool direct) {
    for (int i=0;i<m_.numMains;i++) {
      bool turnOn = direct ? (i == m) : (i != m);
      digitalWrite(m_.mainPins[i],
        m_.mainActiveLow[i]
          ? (turnOn ? LOW  : HIGH)
          : (turnOn ? HIGH : LOW));
    }
  }

  // Enciende un main específico DIRECTO (solo ese), helper
  void setMainDirect(int m) {
    for (int i=0;i<m_.numMains;i++) {
      bool turnOn = (i==m);
      digitalWrite(m_.mainPins[i],
        m_.mainActiveLow[i] ? (turnOn?LOW:HIGH) : (turnOn?HIGH:LOW));
    }
  }

  // Enciende un sec específico
  void setSec(int idx, bool on) {
    if (idx < 0 || idx >= m_.numSecs) return;
    digitalWrite(m_.secPins[idx],
      m_.secActiveLow[idx] ? (on?LOW:HIGH) : (on?HIGH:LOW));
  }

private:
  const PinMap& m_;
};

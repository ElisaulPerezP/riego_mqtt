// File: src/web/WebUI_Inbox.cpp
#include "web/WebUI.h"

void WebUI::pushMsg_(const String& t, const String& p) {
  Msg& m = inbox_[writePos_];
  m.topic   = t;
  m.payload = p;
  m.ms      = millis();
  m.seq     = ++seqCounter_;

  writePos_ = (writePos_ + 1) % MSG_BUF;
  if (used_ < MSG_BUF) used_++;
}

#include "core/LineReader.h"

bool LineReader::poll(String& out, size_t maxLen) {
  if (!io_) return false;

  while (io_->available()) {
    char c = (char)io_->read();
    if (c == '\r' || c == '\n') {
      if (buf_.length() == 0) continue;  // ignora CR/LF consecutivos
      out = buf_;
      buf_.remove(0);
      return true;
    }
    if (isPrintable(c) && buf_.length() < maxLen) {
      buf_ += c;
    }
  }
  return false;
}

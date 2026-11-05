#pragma once

#include <stdint.h>

// UI-only selection and shared rate limit. Opening or leaving never transmits.
class AdvertMenu {
public:
  enum class Choice { Local, Flood, Back };
  enum class Result { Ignored, Back, RateLimited, Failed, Sent };

private:
  Choice choice = Choice::Local;
  bool active = false;
  bool sentBefore = false;
  uint32_t lastSent = 0;

public:
  void open() { choice = Choice::Local; active = true; }
  void close() { active = false; }
  Choice selected() const { return choice; }
  void next() {
    if (active) choice = static_cast<Choice>((static_cast<uint8_t>(choice) + 1) % 3);
  }
  void previous() {
    if (active) choice = static_cast<Choice>((static_cast<uint8_t>(choice) + 2) % 3);
  }
  uint32_t waitSeconds(uint32_t now) const {
    const uint32_t elapsed = now - lastSent;
    return sentBefore && elapsed < 60000UL ? (60000UL - elapsed + 999UL) / 1000UL : 0;
  }

  // send(bool flood) returns true once MeshCore accepts the advert for sending.
  template<typename Send>
  Result activate(uint32_t now, bool visible, Send send) {
    if (!active || !visible) return Result::Ignored;
    if (choice == Choice::Back) {
      close();
      return Result::Back;
    }
    if (waitSeconds(now)) return Result::RateLimited;
    if (!send(choice == Choice::Flood)) return Result::Failed;
    lastSent = now;
    sentBefore = true;
    close();
    return Result::Sent;
  }
};

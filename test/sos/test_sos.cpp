// Unit tests for the SOS logic (examples/companion_radio/ui-new/MeckSos.h).
// Plain C++, no Arduino or PlatformIO needed:
//
//   g++ -std=c++11 -Wall -I examples/companion_radio/ui-new test/sos/test_sos.cpp -o test_sos && ./test_sos
//
// On Windows through WSL (g++ is installed in the "Ubuntu" distro), from the
// repository root:
//
//   wsl -d Ubuntu -- bash -c "g++ -std=c++11 -Wall -fsanitize=address,undefined -I examples/companion_radio/ui-new test/sos/test_sos.cpp -o /tmp/test_sos && /tmp/test_sos"
//
// Each test drives MeckSos with made-up times, as main.cpp does with millis().

#include "MeckSos.h"
#include <stdio.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const double LAT = 45.598765, LON = 24.612345;   // Fagaras ridge

// Ticks with no GPS fix from `from` to `to` in 1 s steps; returns the OR of all actions
static uint16_t run(MeckSos& s, uint32_t from, uint32_t to, bool fix = false) {
  uint16_t all = 0;
  for (uint32_t t = from; (int32_t)(to - t) >= 0; t += 1000) {
    uint16_t a = s.tick(t, fix, LAT, LON);
    all |= a;
    if (a & MeckSos::ACT_SEND_SOS) s.onSent(MeckSos::MSG_SOS, t);
    if (a & MeckSos::ACT_SEND_OK) s.onSent(s.pendingOkMsg(), t);
  }
  return all;
}

static void testCountdownAndCancel() {
  MeckSos s;
  CHECK(s.arm(1000, MeckSos::COUNTDOWN_HOLD_MS));
  CHECK(s.state() == MeckSos::COUNTDOWN);
  CHECK(!s.arm(1500, MeckSos::COUNTDOWN_HOLD_MS));           // already counting
  CHECK(s.countdownSecs(1000) == 10);
  CHECK(s.countdownSecs(9500) == 2);
  uint16_t a = s.tick(1000, false, 0, 0);
  CHECK(a & MeckSos::ACT_PULSE);
  CHECK(s.gpsOn());                                          // GPS starts with the countdown
  CHECK(!(s.tick(1200, false, 0, 0) & MeckSos::ACT_PULSE));  // one pulse per second
  CHECK(s.tick(2000, false, 0, 0) & MeckSos::ACT_PULSE);
  CHECK(s.wantsAwake(2000));
  CHECK(s.cancelCountdown());
  CHECK(s.state() == MeckSos::IDLE);
  CHECK(s.outcome() == MeckSos::OUT_COUNTDOWN_CANCELLED);
  s.tick(3000, false, 0, 0);
  CHECK(!s.gpsOn());
  CHECK(!s.cancelCountdown());                               // nothing to cancel any more
  CHECK(!(run(s, 3000, 20000) & MeckSos::ACT_SEND_SOS));     // and nothing is sent
}

static void testFirstSendAndSchedule() {
  MeckSos s;
  s.arm(0, MeckSos::COUNTDOWN_HOLD_MS);
  uint16_t a = run(s, 0, 9000);
  CHECK(!(a & MeckSos::ACT_SEND_SOS));                       // not before the countdown ends
  a = s.tick(10000, false, 0, 0);
  CHECK(s.state() == MeckSos::ACTIVE);
  CHECK(a & MeckSos::ACT_SEND_SOS);
  s.onSent(MeckSos::MSG_SOS, 10000);
  CHECK(s.sends() == 1);
  CHECK(s.nextSendIn(10000) == 60000);                       // then 1 min
  // Sends at +1, +2 (3 min), +5 (8 min), then every 10 min
  uint32_t expect[] = { 70000, 190000, 490000, 1090000, 1690000 };
  uint32_t t = 11000;
  for (int i = 0; i < 5; i++) {
    uint32_t sentAt = 0;
    for (; t <= expect[i]; t += 1000) {
      uint16_t x = s.tick(t, false, 0, 0);
      if (x & MeckSos::ACT_SEND_SOS) { s.onSent(MeckSos::MSG_SOS, t); sentAt = t; }
    }
    CHECK(sentAt == expect[i]);
  }
  CHECK(s.sends() == 6);
}

static void testFailedSendRetries() {
  MeckSos s;
  s.arm(0, 1000);
  CHECK(s.tick(1000, false, 0, 0) & MeckSos::ACT_SEND_SOS);
  s.onSendFailed(1000);
  CHECK(!(s.tick(2000, false, 0, 0) & MeckSos::ACT_SEND_SOS));   // not on every loop
  CHECK(!(s.tick(10999, false, 0, 0) & MeckSos::ACT_SEND_SOS));
  CHECK(s.tick(11000, false, 0, 0) & MeckSos::ACT_SEND_SOS);
  CHECK(s.sends() == 0);
}

static void testPositionPolicy() {
  char text[160];
  MeckSos s;
  // No fix ever: no coordinates, "Position unknown."
  s.buildText(MeckSos::MSG_SOS, text, sizeof(text), 0, 63);
  CHECK(strstr(text, "SOS! Need help.") == text);
  CHECK(strstr(text, "Position unknown.") != nullptr);
  CHECK(strstr(text, "[LOC]") == nullptr);
  CHECK(strstr(text, "bat 63%") != nullptr);
  CHECK(strstr(text, "#1") != nullptr);

  // A fix: coordinates with its age, parsed back by the chat's location reader
  s.tick(1000, true, LAT, LON);
  s.buildText(MeckSos::MSG_SOS, text, sizeof(text), 121000, 63);
  CHECK(strstr(text, "[LOC]45.598765,24.612345 fix 2m") != nullptr);
  MeckGeoPoint g;
  CHECK(meckGeoFind(text, g) && g.tagged);
  CHECK(g.lat > 45.5987 && g.lat < 45.5988);

  // 1 h 59 min old: still sent, with its age
  uint32_t t = 1000 + 7140000;
  s.buildText(MeckSos::MSG_SOS, text, sizeof(text), t, 63);
  CHECK(strstr(text, "fix 1h59m") != nullptr);
  // Older than 2 h: not sent, only its age
  t = 1000 + 7200001;
  s.buildText(MeckSos::MSG_SOS, text, sizeof(text), t, 63);
  CHECK(strstr(text, "[LOC]") == nullptr);
  CHECK(strstr(text, "Position unknown, last fix 2h00m ago.") != nullptr);
}

static void testTextLength() {
  // Worst case: far-west, far-south coordinates, 3-digit send count, 100%
  // battery, plus a 32-char node name and ": " added by the mesh: <= 133
  MeckSos s;
  s.tick(0, true, -89.999999, -179.999999);
  char text[160];
  for (int i = 0; i < 3; i++) {
    MeckSos::Msg m = i == 0 ? MeckSos::MSG_SOS : i == 1 ? MeckSos::MSG_OK_CANCEL : MeckSos::MSG_OK;
    int n = s.buildText(m, text, sizeof(text), 7199000, 100);
    CHECK(n == (int)strlen(text));
    CHECK(n + 32 + 2 <= 133);
  }
  // Truncation never overruns a small buffer
  char small[20];
  int n = s.buildText(MeckSos::MSG_SOS, small, sizeof(small), 0, 100);
  CHECK(n == (int)strlen(small) && n < 20);
}

static void testGpsWindows() {
  MeckSos s;
  s.arm(0, 10000);
  run(s, 0, 9000);
  CHECK(s.gpsOn());
  // Fix arrives just before the first send: GPS goes off after it
  s.tick(9500, true, LAT, LON);
  uint16_t a = s.tick(10000, true, LAT, LON);
  CHECK(a & MeckSos::ACT_SEND_SOS);
  s.onSent(MeckSos::MSG_SOS, 10000);
  a = s.tick(11000, false, 0, 0);
  CHECK(!s.gpsOn());
  CHECK(!s.wantsAwake(11000));                               // may sleep now
  // 3rd send is at 190 s (after the one at 70 s): GPS back on 90 s before
  run(s, 12000, 99000);                                      // sends at 70 s with the 10 s fix (fresh)
  CHECK(s.sends() == 2);
  CHECK(!s.gpsOn());
  CHECK(s.nextEventIn(99000) == 1000);                       // window opens at 100 s
  s.tick(100000, false, 0, 0);
  CHECK(s.gpsOn());
  CHECK(s.wantsAwake(100000));
  s.tick(130000, true, LAT, LON);                            // fix in the window
  s.tick(131000, false, 0, 0);
  CHECK(!s.gpsOn());                                          // off again until the send
  CHECK(s.nextEventIn(131000) == 59000);                     // sleep until the send at 190 s
}

static void testPositionUpdateAfterSendWithoutFix() {
  MeckSos s;
  s.arm(0, 10000);
  run(s, 0, 10000);                                          // sent without any fix
  CHECK(s.sends() == 1);
  CHECK(s.gpsOn());                                          // keeps looking (GPS_EXTRA_MS)
  // Fix at 40 s: an update goes out at once, outside the schedule
  uint16_t a = s.tick(40000, true, LAT, LON);
  CHECK(a & MeckSos::ACT_SEND_SOS);
  s.onSent(MeckSos::MSG_SOS, 40000);
  CHECK(s.sends() == 2);
  CHECK(s.nextSendIn(40000) == 30000);                       // schedule unchanged (70 s)
  CHECK(!(s.tick(41000, true, LAT, LON) & MeckSos::ACT_SEND_SOS));   // only one update

  // No fix within GPS_EXTRA_MS: GPS gives up until the next window
  MeckSos s2;
  s2.arm(0, 10000);
  run(s2, 0, 10000);
  run(s2, 11000, 69000);                                     // sends at... not yet (70 s)
  CHECK(s2.gpsOn());
}

static void testOkConfirmation() {
  MeckSos s;
  s.arm(0, 1000);
  run(s, 0, 5000);
  CHECK(s.state() == MeckSos::ACTIVE);
  s.requestOk(5000);
  CHECK(s.state() == MeckSos::CANCELLING);
  CHECK(s.pendingOkMsg() == MeckSos::MSG_OK_CANCEL);
  uint16_t a = s.tick(5000, false, 0, 0);
  CHECK(a & MeckSos::ACT_SEND_OK);
  CHECK(!(a & MeckSos::ACT_SEND_SOS));                       // no more SOS
  s.onSent(MeckSos::MSG_OK_CANCEL, 5000);
  // Heard once, but sent only once: not done yet
  const uint8_t h1[] = { 0xAB };
  s.addEcho(MeckSos::MSG_OK_CANCEL, h1, 1, 20);
  CHECK(!(s.tick(6000, false, 0, 0) & MeckSos::ACT_FINISHED));
  CHECK(s.state() == MeckSos::CANCELLING);
  // Second send at +1 min, then it ends
  a = run(s, 7000, 66000);
  CHECK(s.okSends() == 2);
  CHECK(a & MeckSos::ACT_FINISHED);
  CHECK(s.state() == MeckSos::IDLE);
  CHECK(s.outcome() == MeckSos::OUT_OK_CONFIRMED);
}

static void testOkNeverHeardKeepsTrying() {
  MeckSos s;
  s.arm(0, 1000);
  run(s, 0, 5000);
  s.requestOk(5000);
  run(s, 5000, 5000 + 3600000);                              // an hour, nobody hears it
  CHECK(s.state() == MeckSos::CANCELLING);
  CHECK(s.okSends() >= 7);
  // An echo of an SOS (not the OK) does not count
  const uint8_t h[] = { 0x11 };
  s.addEcho(MeckSos::MSG_SOS, h, 1, 0);
  run(s, 3606000, 3607000);
  CHECK(s.state() == MeckSos::CANCELLING);
  s.stop();
  CHECK(s.state() == MeckSos::IDLE);
  CHECK(s.outcome() == MeckSos::OUT_STOPPED);
}

static void testOneShotOk() {
  MeckSos s;
  s.requestOk(0);                                            // no SOS: a single check-in
  CHECK(s.state() == MeckSos::IDLE);
  CHECK(s.pendingOkMsg() == MeckSos::MSG_OK);
  uint16_t a = s.tick(0, false, 0, 0);
  CHECK(a & MeckSos::ACT_SEND_OK);
  s.onSent(MeckSos::MSG_OK, 0);
  CHECK(!(run(s, 1000, 600000) & MeckSos::ACT_SEND_OK));     // once only
}

static void testHeardCounting() {
  MeckSos s;
  s.arm(0, 1000);
  run(s, 0, 2000);
  const uint8_t a[] = { 0x10, 0x20 }, b[] = { 0x10, 0x21 };
  s.addEcho(MeckSos::MSG_SOS, a, 2, 8);
  s.addEcho(MeckSos::MSG_SOS, a, 2, 30);                     // same repeater again (next send)
  s.addEcho(MeckSos::MSG_SOS, b, 2, -4);
  CHECK(s.heardCount() == 2);
  CHECK(s.bestSnr() == 30);
  CHECK(s.okHeardCount() == 0);
  for (int i = 0; i < 40; i++) { uint8_t h = (uint8_t)i; s.addEcho(MeckSos::MSG_SOS, &h, 1, 0); }
  CHECK(s.heardCount() == MeckSos::ECHO_MAX);                // capped, no overflow
}

static void testMenuSosWhileCancelling() {
  // The menu's SOS during "I'm OK": stop, then a fresh countdown
  MeckSos s;
  s.arm(0, 1000);
  run(s, 0, 2000);
  s.requestOk(2000);
  s.stop();
  CHECK(s.arm(3000, MeckSos::COUNTDOWN_MENU_MS));
  CHECK(s.countdownSecs(3000) == 5);
  CHECK(s.sends() == 0);
}

static void testReplyPriority() {
  MeckSos s;
  s.arm(0, 1000);
  s.setReply("Public", "Tom: anyone on the ridge?", 1000, false);
  CHECK(strstr(s.reply(), "ridge") != nullptr);
  s.setReply("Salvamont", "We see you, stay put", 2000, true);       // a DM
  s.setReply("Public", "Eva: nice weather", 3000, false);            // chatter
  CHECK(strstr(s.reply(), "stay put") != nullptr);                   // the DM stays
  s.setReply("Public", "Eva: still nice", 2000 + MeckSos::REPLY_KEEP_MS, false);
  CHECK(strstr(s.reply(), "still nice") != nullptr);                 // until it is old
  s.setReply("Salvamont", "On our way", 2000 + MeckSos::REPLY_KEEP_MS + 1000, true);
  s.setReply("#family", "Coming too", 2000 + MeckSos::REPLY_KEEP_MS + 2000, true);
  CHECK(strstr(s.reply(), "Coming too") != nullptr);                 // important replaces important
  s.stop();
  CHECK(s.arm(10000000, 1000));                                      // a new SOS starts clean
  CHECK(s.reply()[0] == 0);
}

static void testMillisWrap() {
  // millis() wraps after 49.7 days; the schedule must not stall or fire early
  MeckSos s;
  uint32_t t0 = 0xFFFFFFFFu - 20000;
  s.arm(t0, 10000);
  uint32_t t = t0;
  int sent = 0;
  for (int i = 0; i < 200; i++, t += 1000) {
    uint16_t a = s.tick(t, false, 0, 0);
    if (a & MeckSos::ACT_SEND_SOS) { s.onSent(MeckSos::MSG_SOS, t); sent++; }
  }
  CHECK(sent == 3);                                          // t0+10 s, +70 s, +190 s
}

static void testFormatAge() {
  char b[12];
  MeckSos::formatAge(b, sizeof(b), 45000);   CHECK(!strcmp(b, "45s"));
  MeckSos::formatAge(b, sizeof(b), 720000);  CHECK(!strcmp(b, "12m"));
  MeckSos::formatAge(b, sizeof(b), 3900000); CHECK(!strcmp(b, "1h05m"));
}

int main() {
  testCountdownAndCancel();
  testFirstSendAndSchedule();
  testFailedSendRetries();
  testPositionPolicy();
  testTextLength();
  testGpsWindows();
  testPositionUpdateAfterSendWithoutFix();
  testOkConfirmation();
  testOkNeverHeardKeepsTrying();
  testOneShotOk();
  testHeardCounting();
  testMenuSosWhileCancelling();
  testReplyPriority();
  testMillisWrap();
  testFormatAge();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}

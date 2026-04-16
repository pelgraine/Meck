#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

#ifdef HAS_4G_MODEM
  #include "SMSScreen.h"
#endif

#ifdef MECK_WEB_READER
  #include "WebReaderScreen.h"
#endif

#ifdef MECK_AUDIO_VARIANT
  #include "AlarmScreen.h"
#endif

#if defined(LilyGo_T5S3_EPaper_Pro)
  #include "VirtualKeyboard.h"
#endif

// MapScreen.h included in UITask.cpp and main.cpp only (PNGdec headers
// conflict with BLE if pulled into the global include chain)

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  unsigned long _kb_flash_off_at;          // Keyboard flash turn-off timer
#ifdef HAS_4G_MODEM
  bool _incomingCallRinging;               // Currently ringing (incoming call)
  unsigned long _nextCallFlash;            // Next LED toggle time
  bool _callFlashState;                    // Current LED state during ring
#endif
  NodePrefs* _node_prefs;
  char _alert[80];
  unsigned long _alert_expiry;
  bool _hintActive = false;          // Boot navigation hint overlay
  unsigned long _hintExpiry = 0;     // Auto-dismiss time for hint
  bool _pendingBootHint = false;     // Deferred hint — show after splash screen
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;
  uint8_t _low_batt_count = 0;  // Consecutive low-voltage readings for debounce
  int next_backlight_btn_check = 0;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif

  UIScreen* splash;
  UIScreen* home;
#ifndef HELTEC_MESH_POCKET
  UIScreen* msg_preview;
#endif
  UIScreen* channel_screen;  // Channel message history screen
  UIScreen* contacts_screen; // Contacts list screen
  UIScreen* text_reader;     // *** NEW: Text reader screen ***
  UIScreen* notes_screen;    // Notes editor screen
  UIScreen* settings_screen; // Settings/onboarding screen
  UIScreen* audiobook_screen; // Audiobook player screen (null if not available)
#ifdef MECK_AUDIO_VARIANT
  UIScreen* alarm_screen;     // Alarm clock screen (audio variant only)
  UIScreen* voice_screen;     // Voice message screen (audio variant only)
#endif
#ifdef HAS_4G_MODEM
  UIScreen* sms_screen;      // SMS messaging screen (4G variant only)
#endif
  UIScreen* repeater_admin;   // Repeater admin screen
  UIScreen* path_editor;      // Custom path editor screen (lazy-init)
  UIScreen* discovery_screen;  // Node discovery scan screen
  UIScreen* last_heard_screen; // Last heard passive advert list
#ifdef MECK_WEB_READER
  UIScreen* web_reader;       // Web reader screen (lazy-init, WiFi required)
#endif
  UIScreen* map_screen;       // Map tile screen (GPS + SD card tiles)
  UIScreen* curr;
  bool _homeShowingTiles = false;  // Set by HomeScreen render when tile grid is visible
  int _tileGridVY = 44;           // Virtual Y of tile grid top (updated each render)
#if defined(LilyGo_T5S3_EPaper_Pro)
  UIScreen* lock_screen;     // Lock screen (big clock + battery + unread)
  UIScreen* _screenBeforeLock = nullptr;
  bool _locked = false;
  unsigned long _lastInputMillis = 0;  // Auto-lock idle tracking
  unsigned long _lastLockRefresh = 0;  // Periodic lock screen clock update

  VirtualKeyboard _vkb;
  bool _vkbActive = false;
  UIScreen* _screenBeforeVKB = nullptr;
  unsigned long _vkbOpenedAt = 0;

  // Powersaving: light sleep when locked + idle (standalone only — no BLE/WiFi)
  // Wakes on LoRa packet (DIO1), boot button (GPIO0), or 30-min timer
#if !defined(BLE_PIN_CODE) && !defined(MECK_WIFI_COMPANION)
  unsigned long _psLastActive = 0;       // millis() at last wake or lock entry
  unsigned long _psNextSleepSecs = 60;   // Seconds before first sleep (60s), then 5s cycles
#endif
#ifdef MECK_CARDKB
  bool _cardkbDetected = false;
#endif
#elif defined(LilyGo_TDeck_Pro)
  UIScreen* lock_screen;     // Lock screen (big clock + battery + unread)
  UIScreen* _screenBeforeLock = nullptr;
  bool _locked = false;
  unsigned long _lastInputMillis = 0;  // Auto-lock idle tracking
  unsigned long _lastLockRefresh = 0;  // Periodic lock screen clock update
#endif

  // --- Message dedup ring buffer (suppress retry spam at UI level) ---
  #define MSG_DEDUP_SIZE 8
  #define MSG_DEDUP_WINDOW_MS 60000  // 60 seconds
  struct MsgDedup {
    uint32_t name_hash;
    uint32_t text_hash;
    unsigned long millis;
  };
  MsgDedup _dedup[MSG_DEDUP_SIZE];
  int _dedupIdx = 0;

  // --- Per-contact DM unread tracking ---
  uint8_t* _dmUnread = nullptr;  // PSRAM-allocated, MAX_CONTACTS entries

  static uint32_t simpleHash(const char* s) {
    uint32_t h = 5381;
    while (*s) { h = ((h << 5) + h) ^ (uint8_t)*s++; }
    return h;
  }

  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    _kb_flash_off_at = 0;
  #ifdef HAS_4G_MODEM
    _incomingCallRinging = false;
    _nextCallFlash = 0;
    _callFlashState = false;
  #endif
    ui_started_at = 0;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen();
  void gotoChannelScreen();  // Navigate to channel message screen
  void gotoDMTab();          // Navigate directly to DM tab on channel screen
  void gotoDMConversation(const char* contactName, int contactIdx = -1, uint8_t perms = 0);
  void gotoContactsScreen(); // Navigate to contacts list
  void gotoTextReader();     // *** NEW: Navigate to text reader ***
  void gotoNotesScreen();    // Navigate to notes editor
  void gotoSettingsScreen(); // Navigate to settings
  void gotoOnboarding();     // Navigate to settings in onboarding mode
  void gotoAudiobookPlayer(); // Navigate to audiobook player
#ifdef MECK_AUDIO_VARIANT
  void gotoAlarmScreen();     // Navigate to alarm clock
  void gotoVoiceScreen();     // Navigate to voice message recorder
#endif
  void gotoRepeaterAdmin(int contactIdx);  // Navigate to repeater admin
  void gotoRepeaterAdminDirect(int contactIdx);  // Auto-login admin (L key from conversation)
  void gotoPathEditor(int contactIdx);     // Navigate to custom path editor
  void gotoDiscoveryScreen();              // Navigate to node discovery scan
  void gotoLastHeardScreen();              // Navigate to last heard passive list
#if HAS_GPS
  void gotoMapScreen();         // Navigate to map tile screen
#endif
#ifdef MECK_WEB_READER
  void gotoWebReader();         // Navigate to web reader (browser)
#endif
#ifdef HAS_4G_MODEM
  void gotoSMSScreen();
  bool isOnSMSScreen() const { return curr == sms_screen; }
  SMSScreen* getSMSScreen() const { return (SMSScreen*)sms_screen; }
#endif
  void showAlert(const char* text, int duration_millis) override;
  void forceRefresh() override { _next_refresh = 100; }
  void showBootHint(bool immediate = false);  // Show navigation hint overlay on first boot
  void dismissBootHint();  // Dismiss hint and save preference
  bool isHintActive() const { return _hintActive; }
  // Wake display and extend auto-off timer. Call this when handling keys
  // outside of injectKey() to prevent display auto-off during direct input.
  void keepAlive() {
    if (_display != NULL && !_display->isOn()) _display->turnOn();
    _auto_off = millis() + 15000;  // matches AUTO_OFF_MILLIS default
  }
  int  getMsgCount() const { return _msgcount; }
  int  getUnreadMsgCount() const;  // Per-channel unread tracking (standalone)
  
  // Per-contact DM unread tracking
  bool hasDMUnread(int contactIdx) const;
  int  getDMUnreadCount(int contactIdx) const;
  void clearDMUnread(int contactIdx);

  // Flag: suppress room→conversation redirect on next login (L key admin access)
  bool _skipRoomRedirect = false;
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;
  bool isOnChannelScreen() const { return curr == channel_screen; }
  bool isOnContactsScreen() const { return curr == contacts_screen; }
  bool isOnTextReader() const { return curr == text_reader; }  // *** NEW ***
  bool isOnHomeScreen() const { return curr == home; }
  bool isHomeShowingTiles() const { return _homeShowingTiles; }
  void setHomeShowingTiles(bool v) { _homeShowingTiles = v; }
  int  getTileGridVY() const { return _tileGridVY; }
  void setTileGridVY(int vy) { _tileGridVY = vy; }
  bool isOnNotesScreen() const { return curr == notes_screen; }
  bool isOnSettingsScreen() const { return curr == settings_screen; }
  bool isOnAudiobookPlayer() const { return curr == audiobook_screen; }
#ifdef MECK_AUDIO_VARIANT
  bool isOnAlarmScreen() const { return curr == alarm_screen; }
  bool isOnVoiceScreen() const { return curr == voice_screen; }
#endif
  bool isOnRepeaterAdmin() const { return curr == repeater_admin; }
  bool isOnPathEditor() const { return curr == path_editor; }
  bool isOnDiscoveryScreen() const { return curr == discovery_screen; }
  bool isOnLastHeardScreen() const { return curr == last_heard_screen; }
  bool isOnMapScreen() const { return curr == map_screen; }
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
  bool isLocked() const { return _locked; }
  void lockScreen();
  void unlockScreen();
#endif
#if defined(LilyGo_T5S3_EPaper_Pro)
  bool isVKBActive() const { return _vkbActive; }
  unsigned long vkbOpenedAt() const { return _vkbOpenedAt; }
  VirtualKeyboard& getVKB() { return _vkb; }
  void showVirtualKeyboard(VKBPurpose purpose, const char* label, const char* initial, int maxLen, int contextIdx = 0);
  void onVKBSubmit();
  void onVKBCancel();
#ifdef MECK_CARDKB
  void setCardKBDetected(bool v) { _cardkbDetected = v; }
  bool hasCardKB() const { return _cardkbDetected; }
  void feedCardKBChar(char c);
#endif
#endif
#ifdef MECK_WEB_READER
  bool isOnWebReader() const { return curr == web_reader; }
#endif

#ifdef MECK_AUDIO_VARIANT
  // Check if audio is playing/paused in the background (for status indicators)
  bool isAudioPlayingInBackground() const;
  bool isAudioPausedInBackground() const;
#endif
  uint8_t getChannelScreenViewIdx() const;

  void toggleBuzzer();
  bool getGPSState();
  void toggleGPS();

  // Check if home screen is in an editing mode (e.g. UTC offset editor)
  bool isEditingHomeScreen() const;
  // Check if home screen is showing the Recent Adverts page
  bool isHomeOnRecentPage() const;

  // Inject a key press from external source (e.g., keyboard)
  void injectKey(char c);
  
  // Add a sent message to the channel screen history
  void addSentChannelMessage(uint8_t channel_idx, const char* sender, const char* text) override;
  void addSentDM(const char* recipientName, const char* sender, const char* text);

  // Mark channel as read when BLE companion app syncs messages
  void markChannelReadFromBLE(uint8_t channel_idx) override;

  // Repeater admin callbacks
  void onAdminLoginResult(bool success, uint8_t permissions, uint32_t server_time) override;
  void onAdminCliResponse(const char* from_name, const char* text) override;
  void onAdminTelemetryResult(const uint8_t* data, uint8_t len) override;
  
  // Get current screen for checking state
  UIScreen* getCurrentScreen() const { return curr; }
#ifndef HELTEC_MESH_POCKET
  UIScreen* getMsgPreviewScreen() const { return msg_preview; }
#endif
  UIScreen* getTextReaderScreen() const { return text_reader; }  // *** NEW ***
  UIScreen* getNotesScreen() const { return notes_screen; }
  UIScreen* getContactsScreen() const { return contacts_screen; }
  UIScreen* getChannelScreen() const { return channel_screen; }
  UIScreen* getSettingsScreen() const { return settings_screen; }
  NodePrefs* getNodePrefs() const { return _node_prefs; }
  UIScreen* getAudiobookScreen() const { return audiobook_screen; }
  void setAudiobookScreen(UIScreen* s) { audiobook_screen = s; }
#ifdef MECK_AUDIO_VARIANT
  UIScreen* getAlarmScreen() const { return alarm_screen; }
  void setAlarmScreen(UIScreen* s) { alarm_screen = s; }
  UIScreen* getVoiceScreen() const { return voice_screen; }
  void setVoiceScreen(UIScreen* s) { voice_screen = s; }
#endif
  UIScreen* getRepeaterAdminScreen() const { return repeater_admin; }
  UIScreen* getPathEditorScreen() const { return path_editor; }
  UIScreen* getDiscoveryScreen() const { return discovery_screen; }
  UIScreen* getLastHeardScreen() const { return last_heard_screen; }
  UIScreen* getMapScreen() const { return map_screen; }
#ifdef MECK_WEB_READER
  UIScreen* getWebReaderScreen() const { return web_reader; }
#endif

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount,
             const uint8_t* path = nullptr, int8_t snr = 0) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
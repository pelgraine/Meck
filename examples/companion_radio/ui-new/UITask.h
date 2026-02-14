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
  NodePrefs* _node_prefs;
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;
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
  UIScreen* msg_preview;
  UIScreen* channel_screen;  // Channel message history screen
  UIScreen* contacts_screen; // Contacts list screen
  UIScreen* text_reader;     // *** NEW: Text reader screen ***
  UIScreen* notes_screen;    // Notes editor screen
  UIScreen* settings_screen; // Settings/onboarding screen
  UIScreen* repeater_admin;   // Repeater admin screen
  UIScreen* audiobook_screen; // Audiobook player screen (null if not available)
  UIScreen* curr;

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
    ui_started_at = 0;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen();
  void gotoChannelScreen();  // Navigate to channel message screen
  void gotoContactsScreen(); // Navigate to contacts list
  void gotoTextReader();     // *** NEW: Navigate to text reader ***
  void gotoNotesScreen();    // Navigate to notes editor
  void gotoSettingsScreen(); // Navigate to settings
  void gotoOnboarding();     // Navigate to settings in onboarding mode
  void gotoAudiobookPlayer(); // Navigate to audiobook player
  void gotoRepeaterAdmin(int contactIdx); // Navigate to repeater admin
  void showAlert(const char* text, int duration_millis) override;
  void forceRefresh() override { _next_refresh = 100; }
  int  getMsgCount() const { return _msgcount; }
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;
  bool isOnChannelScreen() const { return curr == channel_screen; }
  bool isOnContactsScreen() const { return curr == contacts_screen; }
  bool isOnTextReader() const { return curr == text_reader; }  // *** NEW ***
  bool isOnNotesScreen() const { return curr == notes_screen; }
  bool isOnSettingsScreen() const { return curr == settings_screen; }
  bool isOnAudiobookPlayer() const { return curr == audiobook_screen; }
  bool isOnRepeaterAdmin() const { return curr == repeater_admin; }
  uint8_t getChannelScreenViewIdx() const;

  void toggleBuzzer();
  bool getGPSState();
  void toggleGPS();

  // Check if home screen is in an editing mode (e.g. UTC offset editor)
  bool isEditingHomeScreen() const;

  // Inject a key press from external source (e.g., keyboard)
  void injectKey(char c);
  
  // Add a sent message to the channel screen history
  void addSentChannelMessage(uint8_t channel_idx, const char* sender, const char* text) override;
  
  // Get current screen for checking state
  UIScreen* getCurrentScreen() const { return curr; }
  UIScreen* getMsgPreviewScreen() const { return msg_preview; }
  UIScreen* getTextReaderScreen() const { return text_reader; }  // *** NEW ***
  UIScreen* getNotesScreen() const { return notes_screen; }
  UIScreen* getContactsScreen() const { return contacts_screen; }
  UIScreen* getChannelScreen() const { return channel_screen; }
  UIScreen* getSettingsScreen() const { return settings_screen; }
  UIScreen* getAudiobookScreen() const { return audiobook_screen; }
  UIScreen* getRepeaterAdminScreen() const { return repeater_admin; }
  void setAudiobookScreen(UIScreen* s) { audiobook_screen = s; }

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  // Repeater admin callbacks (from MyMesh via AbstractUITask)
  void onAdminLoginResult(bool success, uint8_t permissions, uint32_t server_time) override;
  void onAdminCliResponse(const char* from_name, const char* text) override;

  void shutdown(bool restart = false);
};
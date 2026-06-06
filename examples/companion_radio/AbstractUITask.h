#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <Arduino.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "NodePrefs.h"

enum class UIEventType {
    none,
    contactMessage,
    channelMessage,
    roomMessage,
    newContactMessage,
    ack
};

class AbstractUITask {
protected:
  mesh::MainBoard* _board;
  BaseSerialInterface* _serial;
  bool _connected;

  AbstractUITask(mesh::MainBoard* board, BaseSerialInterface* serial) : _board(board), _serial(serial) {
    _connected = false;
  }

public:
  void setHasConnection(bool connected) { _connected = connected; }
  bool hasConnection() const { return _connected; }
  uint16_t getBattMilliVolts() const { return _board->getBattMilliVolts(); }
  uint8_t getBatteryPercent() const { return _board->getBatteryPercent(); }
  bool isSerialEnabled() const { return _serial->isEnabled(); }
  void enableSerial() { _serial->enable(); }
  void disableSerial() { _serial->disable(); }
  virtual void msgRead(int msgcount) = 0;
  virtual void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount,
                      const uint8_t* path = nullptr, int8_t snr = 0,
                      uint8_t scope_idx = 0xFF) = 0;  // 0xFF = unscoped
  virtual void notify(UIEventType t = UIEventType::none) = 0;
  virtual void loop() = 0;
  virtual void showAlert(const char* text, int duration_millis) {}
  virtual void forceRefresh() {}
  virtual void addSentChannelMessage(uint8_t channel_idx, const char* sender, const char* text) {}

  // Mark a channel as read when BLE companion app syncs a message
  virtual void markChannelReadFromBLE(uint8_t channel_idx) {}
  virtual void markAllChannelsRead() {}  // Companion builds: zero all unread on app connect

  // Repeater admin callbacks (from MyMesh)
  virtual void onAdminLoginResult(bool success, uint8_t permissions, uint32_t server_time) {}
  virtual void onAdminCliResponse(const char* from_name, const char* text) {}
  virtual void onAdminTelemetryResult(const uint8_t* data, uint8_t len) {}

  // Trace path callback (from MyMesh::onTraceRecv)
  virtual void onTraceResult(uint32_t tag, uint8_t flags, const uint8_t* path_snrs,
                             const uint8_t* path_hashes, uint8_t path_len, int8_t final_snr) {}
};
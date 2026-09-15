#pragma once

#include "../BaseSerialInterface.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

class SerialBLEInterface : public BaseSerialInterface, BLESecurityCallbacks, BLEServerCallbacks, BLECharacteristicCallbacks {
  BLEServer *pServer;
  BLEService *pService;
  BLECharacteristic * pTxCharacteristic;
  bool deviceConnected;
  bool oldDeviceConnected;
  bool _isEnabled;
  bool _begun;              // has _realBegin() run? (deferred BLE bring-up)
  bool _auth_ok;            // authentication completed on the current link (see checkRecvFrame)
#ifdef MECK_BLE_SMALL_MTU_SPLIT
  // Small-MTU peers (e.g. a Garmin watch, which never raises the ATT MTU above
  // 23) can only carry 20 bytes per notification or write. Frames to and from
  // such a peer travel as tagged slices; see SLICE_TAG_* in the .cpp.
  uint16_t _tx_off;                  // bytes of send_queue[0] already sent (0 = none yet)
  uint8_t  _rx_buf[MAX_FRAME_SIZE];  // reassembly buffer for sliced writes
  int      _rx_len;                  // bytes collected so far (-1 = not mid-frame)
#endif
  uint16_t last_conn_id;
  uint8_t _remote_bda[6];   // peer BDA, stored in onConnect for conn param updates
  uint32_t _pin_code;
  char _dev_name[48];      // stored in begin(), consumed by deferred _realBegin()
  unsigned long _last_write;
  unsigned long adv_restart_time;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE  16
  int recv_queue_len;
  Frame recv_queue[FRAME_QUEUE_SIZE];
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers() {
    recv_queue_len = 0; send_queue_len = 0;
#ifdef MECK_BLE_SMALL_MTU_SPLIT
    _tx_off = 0; _rx_len = -1;
#endif
  }

  void _realBegin();       // deferred BLE controller + GATT bring-up

protected:
  // BLESecurityCallbacks methods
  uint32_t onPassKeyRequest() override;
  void onPassKeyNotify(uint32_t pass_key) override;
  bool onConfirmPIN(uint32_t pass_key) override;
  bool onSecurityRequest() override;
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override;

  // BLEServerCallbacks methods
  void onConnect(BLEServer* pServer) override;
  void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) override;
  void onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override;
  void onDisconnect(BLEServer* pServer) override;

  // BLECharacteristicCallbacks methods
  void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) override;

public:
  SerialBLEInterface() {
    pServer = NULL;
    pService = NULL;
    deviceConnected = false;
    oldDeviceConnected = false;
    adv_restart_time = 0;
    _isEnabled = false;
    _begun = false;
    _auth_ok = false;
#ifdef MECK_BLE_SMALL_MTU_SPLIT
    _tx_off = 0; _rx_len = -1;
#endif
    _last_write = 0;
    last_conn_id = 0;
    memset(_remote_bda, 0, 6);
    send_queue_len = recv_queue_len = 0;
  }

  /**
   * init the BLE interface.
   * @param prefix   a prefix for the device name
   * @param name  IN/OUT - a name for the device (combined with prefix). If "@@MAC", is modified and returned
   * @param pin_code   the BLE security pin
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  bool isWriteBusy() const override;
  bool hasPendingData() const override { return deviceConnected && send_queue_len > 0; }
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif
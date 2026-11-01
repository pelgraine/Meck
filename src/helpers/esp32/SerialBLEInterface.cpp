#include "SerialBLEInterface.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ADVERT_RESTART_DELAY  1000   // millis

// A phone can hold the link with no app behind it: iOS reconnects on behalf
// of an app that has since been closed or has given up. The deck then stops
// advertising, so the app cannot find it even when opened, until the link
// drops. The app sends its first frame right after connecting, so a link
// that stays silent this long is dropped to bring advertising back. Off
// (0) for the watch build, whose app may stay quiet after connecting.
#ifndef MECK_BLE_SILENT_LINK_MS
#ifdef MECK_WATCH_EXT
#define MECK_BLE_SILENT_LINK_MS  0UL
#else
#define MECK_BLE_SILENT_LINK_MS  30000UL
#endif
#endif

// BLE transmit power for connections, advertising and scanning: +9 dBm unless
// a build sets another esp_power_level_t level with a flag, for example
//   -D MECK_BLE_TX_POWER=ESP_PWR_LVL_P15
// (the ESP32-S3 levels in esp_bt.h run from ESP_PWR_LVL_N24 to ESP_PWR_LVL_P21
// in 3 dB steps).
#ifndef MECK_BLE_TX_POWER
#define MECK_BLE_TX_POWER  ESP_PWR_LVL_P9
#endif

#ifdef MECK_BLE_SMALL_MTU_SPLIT
// Slice tags for small-MTU peers. No real frame can start with one of these:
// command codes are below 0x80 and push codes stop at 0x90. When slicing is in
// effect, every frame to or from the peer travels as [tag][len][payload]
// slices; the length byte lets stream-style transports
// recover slice boundaries as well.
#define SLICE_TAG_FIRST   0xF0   // first slice of a multi-slice frame
#define SLICE_TAG_MIDDLE  0xF1
#define SLICE_TAG_LAST    0xF2
#define SLICE_TAG_SINGLE  0xF3   // whole frame fits in one slice
#define SLICE_TAG_MASK    0xFC   // (b & MASK) == SLICE_TAG_FIRST matches any tag
#ifndef MECK_BLE_SPLIT_MAX_MTU
#define MECK_BLE_SPLIT_MAX_MTU 23   // slice outgoing frames when the peer MTU is at or below this
#endif
#endif

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  _pin_code = pin_code;

  if (strcmp(name, "@@MAC") == 0) {
    uint8_t addr[8];
    memset(addr, 0, sizeof(addr));
    esp_efuse_mac_get_default(addr);
    sprintf(name, "%02X%02X%02X%02X%02X%02X",    // modify (IN-OUT param)
          addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  }
  // Store the device name and defer the actual controller bring-up
  // (BLEDevice::init + GATT setup) until the first enable(). This keeps the BT
  // controller powered down while BLE is disabled -- the standalone-first
  // default -- which reclaims its idle current. The controller comes up on the
  // first enable and, as with the web-reader teardown path, stays up until the
  // next reboot thereafter.
  snprintf(_dev_name, sizeof(_dev_name), "%s%s", prefix, name);
  _begun = false;
}

// What the phone accepted: it may round a request or ignore it, and only
// this event says which (the power log needs the real interval)
static volatile uint16_t s_link_interval = 0, s_link_latency = 0;

static void onGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
  switch (event) {
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
      if (param->update_conn_params.status == ESP_BT_STATUS_SUCCESS) {
        s_link_interval = param->update_conn_params.conn_int;
        s_link_latency = param->update_conn_params.latency;
      }
      BLE_DEBUG_PRINTLN("conn params: status %d, interval %u x 1.25ms, latency %u, timeout %u x 10ms",
                        param->update_conn_params.status, param->update_conn_params.conn_int,
                        param->update_conn_params.latency, param->update_conn_params.timeout);
      break;
    case ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT:
      BLE_DEBUG_PRINTLN("phy update: status %d, tx %d, rx %d", param->phy_update.status,
                        param->phy_update.tx_phy, param->phy_update.rx_phy);
      break;
    case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
      BLE_DEBUG_PRINTLN("data length: status %d, rx %u, tx %u", param->pkt_data_lenth_cmpl.status,
                        param->pkt_data_lenth_cmpl.params.rx_len, param->pkt_data_lenth_cmpl.params.tx_len);
      break;
    default:
      break;
  }
}

uint16_t SerialBLEInterface::linkInterval() { return s_link_interval; }
uint16_t SerialBLEInterface::linkLatency() { return s_link_latency; }

// Deferred BLE controller + GATT bring-up. Called from the first enable() so
// the controller is not powered while BLE is disabled.
void SerialBLEInterface::_realBegin() {
  // Create the BLE Device
  BLEDevice::init(_dev_name);
  BLEDevice::setSecurityCallbacks(this);
  BLEDevice::setCustomGapHandler(onGapEvent);
  BLEDevice::setMTU(MAX_FRAME_SIZE);

  // Boost BLE TX power for improved range (MECK_BLE_TX_POWER above: +9 dBm
  // unless the build sets another level; up from default +3 dBm)
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, MECK_BLE_TX_POWER);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, MECK_BLE_TX_POWER);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, MECK_BLE_TX_POWER);

  BLESecurity  sec;
  sec.setStaticPIN(_pin_code);
#ifdef MECK_BLE_JUST_WORKS
  // Garmin Connect IQ has no way to enter a PIN, so a watch can only reach an
  // encrypted-but-unverified ("just works") link. Drop the MITM requirement so
  // that link is accepted. The static PIN above is then never asked for.
  sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
#else
  sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
#endif

  //BLEDevice::setPower(ESP_PWR_LVL_N8);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(this);

  // Create the BLE Service
  pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
#ifdef MECK_BLE_JUST_WORKS
  pTxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
#else
  pTxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
#endif
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic * pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
#ifdef MECK_BLE_JUST_WORKS
  pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
#else
  pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
#endif
  pRxCharacteristic->setCallbacks(this);

  pServer->getAdvertising()->addServiceUUID(SERVICE_UUID);
}

// -------- BLESecurityCallbacks methods

uint32_t SerialBLEInterface::onPassKeyRequest() {
  BLE_DEBUG_PRINTLN("onPassKeyRequest()");
  return _pin_code;
}

void SerialBLEInterface::onPassKeyNotify(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("onPassKeyNotify(%u)", pass_key);
}

bool SerialBLEInterface::onConfirmPIN(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("onConfirmPIN(%u)", pass_key);
  return true;
}

bool SerialBLEInterface::onSecurityRequest() {
  BLE_DEBUG_PRINTLN("onSecurityRequest()");
  return true;  // allow
}

void SerialBLEInterface::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
  if (cmpl.success) {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Success");
    deviceConnected = true;
    _auth_ok = true;
    _link_at = millis() | 1;   // never 0 (= not waiting)

    // Take the peer address from the event itself. A peer that pairs at
    // connect time (e.g. a bonded Garmin watch) reaches this callback before
    // onConnect() has stored _remote_bda, leaving it zeroed and making the
    // three requests below fail with "Invalid connection remote_bda".
    memcpy(_remote_bda, cmpl.bd_addr, 6);

    // Request fast connection interval (15ms) for faster contact sync.
    // Phone may negotiate higher, but most modern phones accept 15ms.
    // Units are 1.25ms, so 12 = 15ms, 16 = 20ms.
    esp_ble_conn_update_params_t conn_params;
    memcpy(conn_params.bda, _remote_bda, 6);
#ifdef MECK_BLE_SLOW_INTERVAL
    // Watch link: a message every few minutes does not need a 15 ms interval,
    // and a fast interval keeps the watch radio (and this stack) busy all day.
    conn_params.min_int = 80;   // 100ms  (80 x 1.25ms)
    conn_params.max_int = 160;  // 200ms  (160 x 1.25ms)
    conn_params.latency = 2;    // peer may skip 2 intervals when idle
    conn_params.timeout = 600;  // 6 seconds supervision timeout
    esp_ble_gap_update_conn_params(&conn_params);
    BLE_DEBUG_PRINTLN(" - Requested relaxed connection interval (100-200ms)");
#else
    conn_params.min_int = 12;   // 15ms   (12 × 1.25ms)
    conn_params.max_int = 16;   // 20ms   (16 × 1.25ms)
    conn_params.latency = 0;    // no skipped intervals
    conn_params.timeout = 400;  // 4 seconds supervision timeout
    esp_ble_gap_update_conn_params(&conn_params);
    BLE_DEBUG_PRINTLN(" - Requested fast connection interval (15-20ms)");
#endif

    // Request 2M PHY for doubled air data rate (BLE 5.0, supported on ESP32-S3)
    // Note: ESP-IDF misspells "preferred" as "prefered" in their API
    esp_ble_gap_set_prefered_phy(_remote_bda,
                                  0,   // all_phys: no preference flags, use tx/rx masks
                                  ESP_BLE_GAP_PHY_2M_PREF_MASK,
                                  ESP_BLE_GAP_PHY_2M_PREF_MASK,
                                  ESP_BLE_GAP_PHY_OPTIONS_NO_PREF);
    esp_ble_gap_set_pkt_data_len(_remote_bda, 251);   // Request DLE (max link-layer PDU)
    BLE_DEBUG_PRINTLN(" - Requested 2M PHY and DLE (251 bytes)");
  } else {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Failure*");
    _auth_ok = false;

    //pServer->removePeerDevice(pServer->getConnId(), true);
    pServer->disconnect(pServer->getConnId());
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- BLEServerCallbacks methods

void SerialBLEInterface::onConnect(BLEServer* pServer) {
}

void SerialBLEInterface::onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {
  BLE_DEBUG_PRINTLN("onConnect(), conn_id=%d, mtu=%d", param->connect.conn_id, pServer->getPeerMTU(param->connect.conn_id));
  last_conn_id = param->connect.conn_id;
  memcpy(_remote_bda, param->connect.remote_bda, 6);
}

void SerialBLEInterface::onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
  BLE_DEBUG_PRINTLN("onMtuChanged(), mtu=%d", pServer->getPeerMTU(param->mtu.conn_id));
}

void SerialBLEInterface::onDisconnect(BLEServer* pServer) {
  BLE_DEBUG_PRINTLN("onDisconnect()");
  _auth_ok = false;
  // Mark the link down right away (MeshCore #3005): a bonded phone can
  // reconnect before loop() polls getConnectedCount() and sees it at 0
  deviceConnected = false;
  if (_isEnabled) {
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

void SerialBLEInterface::onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
  _drop_reason = (uint8_t)param->disconnect.reason;
  _drops++;
  _link_at = 0;
  BLE_DEBUG_PRINTLN("onDisconnect(), reason 0x%02x", _drop_reason);
}

// -------- BLECharacteristicCallbacks methods

void SerialBLEInterface::onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) {
  uint8_t* rxValue = pCharacteristic->getData();
  int len = pCharacteristic->getLength();

#ifdef MECK_BLE_SMALL_MTU_SPLIT
  if (len >= 1 && (rxValue[0] & SLICE_TAG_MASK) == SLICE_TAG_FIRST) {   // any SLICE_TAG_*
    uint8_t tag = rxValue[0];
    if (tag == SLICE_TAG_FIRST || tag == SLICE_TAG_SINGLE) _rx_len = 0;
    if (_rx_len < 0) return;                                  // middle/last with no start: drop
    if (len < 2) return;                                      // no length byte
    int n = rxValue[1];
    if (n > len - 2) n = len - 2;                             // never read past the write
    if (_rx_len + n > MAX_FRAME_SIZE) { _rx_len = -1; return; }   // oversize: drop the frame
    memcpy(&_rx_buf[_rx_len], rxValue + 2, n);
    _rx_len += n;
    if (tag == SLICE_TAG_LAST || tag == SLICE_TAG_SINGLE) {
      Frame frame;
      frame.len = _rx_len;
      memcpy(frame.buf, _rx_buf, _rx_len);
      if (xQueueSend(recv_queue, &frame, 0) != pdTRUE) {
        BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full! (sliced)");
      }
      _rx_len = -1;
    }
    return;
  }
#endif
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", len);
  } else {
    Frame frame;
    frame.len = len;
    memcpy(frame.buf, rxValue, len);

    if (xQueueSend(recv_queue, &frame, 0) != pdTRUE) {
      BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
    }
  }
}

// ---------- public methods

void SerialBLEInterface::enable() { 
  if (_isEnabled) return;

  if (!_begun) {        // deferred controller bring-up on first enable
    _realBegin();
    _begun = true;
  }

  _isEnabled = true;
  clearBuffers();

  // Start the service
  pService->start();

  // Start advertising

  //pServer->getAdvertising()->setMinInterval(500);
  //pServer->getAdvertising()->setMaxInterval(1000);

  pServer->getAdvertising()->start();
  adv_restart_time = 0;
}

void SerialBLEInterface::disable() {
  bool wasEnabled = _isEnabled;
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  // Only try BLE operations if we were previously enabled
  // (avoids accessing dead BLE objects after btStop/mem_release)
  if (wasEnabled && pServer) {
    pServer->getAdvertising()->stop();
    pServer->disconnect(last_conn_id);
    pService->stop();
  }
  oldDeviceConnected = deviceConnected = false;
  adv_restart_time = 0;
  clearBuffers();
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

#define  BLE_WRITE_MIN_INTERVAL   7

// MyMesh::checkSerialInterface() queues up to 8 contacts and then the end
// frame in one go, and a contact it could not queue is lost: report busy
// until that much room is free.
#define  BLE_WRITE_BATCH_ROOM     9

// The stack refuses a notification when its buffers for the link are full
// (a contact sync outruns a 30 ms iOS link: "notify(): rc=-1"), and the
// frame was lost. Wait while the controller has no room, but not forever,
// in case the count is stuck.
#define  BLE_CONGESTED_MAX_WAIT   1000

bool SerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL     // still too soon to start another write?
      || send_queue_len > FRAME_QUEUE_SIZE - BLE_WRITE_BATCH_ROOM;
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  if (!_isEnabled) return 0;  // BLE disabled — skip all BLE operations

  if (send_queue_len > 0   // first, check send queue
    && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL    // space the writes apart
    && (esp_ble_get_cur_sendable_packets_num(last_conn_id) > 0
        || millis() >= _last_write + BLE_CONGESTED_MAX_WAIT)
  ) {
    _last_write = millis();
#ifdef MECK_BLE_SMALL_MTU_SPLIT
    bool popFrame = true;
    uint16_t mtu = pServer->getPeerMTU(last_conn_id);
    if (mtu <= MECK_BLE_SPLIT_MAX_MTU) {
      // Small-MTU peer: send the next slice of the frame at the head of the
      // queue, one slice per pass (keeps BLE_WRITE_MIN_INTERVAL pacing). The
      // frame is only popped once its last slice has gone.
      int room = (int)mtu - 3 - 2;                 // ATT payload minus tag and length bytes
      if (room < 1) room = 1;
      int total = send_queue[0].len;
      int n = total - _tx_off;
      if (n > room) n = room;
      bool first = (_tx_off == 0);
      bool last  = (_tx_off + n >= total);
      uint8_t chunk[MAX_FRAME_SIZE + 1];
      chunk[0] = (first && last) ? SLICE_TAG_SINGLE
               : first ? SLICE_TAG_FIRST
               : last  ? SLICE_TAG_LAST : SLICE_TAG_MIDDLE;
      chunk[1] = (uint8_t)n;
      memcpy(&chunk[2], &send_queue[0].buf[_tx_off], n);
      pTxCharacteristic->setValue(chunk, n + 2);
      pTxCharacteristic->notify();
      BLE_DEBUG_PRINTLN("writeSlice: tag=%02X off=%d n=%d of %d", chunk[0], (int)_tx_off, n, total);
      _tx_off += n;
      if (last) {
        _tx_off = 0;
        BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d (sliced)", total, (uint32_t) send_queue[0].buf[0]);
      } else {
        popFrame = false;
      }
    } else {
#endif
    pTxCharacteristic->setValue(send_queue[0].buf, send_queue[0].len);
    pTxCharacteristic->notify();

    BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);
#ifdef MECK_BLE_SMALL_MTU_SPLIT
    }
    if (popFrame) {
#endif
    send_queue_len--;
    if (send_queue_len > 0) {
      memmove(&send_queue[0], &send_queue[1], send_queue_len * sizeof(Frame));
    }
#ifdef MECK_BLE_SMALL_MTU_SPLIT
    }
#endif
  }

  Frame frame;
  if (xQueueReceive(recv_queue, &frame, 0) == pdTRUE) {   // check recv queue
    _last_rx = millis();
    _rx_frames++;
    _link_at = 0;
    memcpy(dest, frame.buf, frame.len);
    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", (uint32_t) frame.len, (uint32_t) dest[0]);
    return frame.len;
  }

  if (pServer->getConnectedCount() == 0) {
    deviceConnected = false;
  } else if (_auth_ok) {
    // Authentication can complete before the GATT connect event (a bonded
    // peer, e.g. a Garmin watch, encrypts immediately). The connected count
    // is still 0 then, so the line above resets deviceConnected; restore it
    // once the connect event has landed.
    deviceConnected = true;
  }

  if (deviceConnected != oldDeviceConnected) {
    if (!deviceConnected) {    // disconnecting
      clearBuffers();

      BLE_DEBUG_PRINTLN("SerialBLEInterface -> disconnecting...");

      //pServer->getAdvertising()->setMinInterval(500);
      //pServer->getAdvertising()->setMaxInterval(1000);

      adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    } else {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> stopping advertising");
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> connecting...");
      // connecting
      // do stuff here on connecting
      pServer->getAdvertising()->stop();
      adv_restart_time = 0;
    }
    oldDeviceConnected = deviceConnected;
  }

  unsigned long linkAt = _link_at;
  if (MECK_BLE_SILENT_LINK_MS && deviceConnected && linkAt && millis() - linkAt >= MECK_BLE_SILENT_LINK_MS) {
    Serial.println("[BLE] Link up but the app is silent: dropping it to advertise again");
    _link_at = 0;
    pServer->disconnect(last_conn_id);
  }

  if (adv_restart_time && millis() >= adv_restart_time) {
    if (pServer->getConnectedCount() == 0) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> re-starting advertising");
      pServer->getAdvertising()->start();  // re-Start advertising
    }
    adv_restart_time = 0;
  }
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}
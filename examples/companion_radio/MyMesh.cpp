#include "MyMesh.h"

#include <Arduino.h> // needed for PlatformIO
#include <Mesh.h>
#include "RadioPresets.h"        // Shared radio presets (serial CLI + settings screen)

#if defined(LilyGo_T5S3_EPaper_Pro)
  #include "target.h"            // for board.setBacklight() CLI command
#endif

#ifdef HAS_4G_MODEM
  #include "ModemManager.h"      // Serial CLI modem commands
#endif

#define CMD_APP_START                 1
#define CMD_SEND_TXT_MSG              2
#define CMD_SEND_CHANNEL_TXT_MSG      3
#define CMD_GET_CONTACTS              4 // with optional 'since' (for efficient sync)
#define CMD_GET_DEVICE_TIME           5
#define CMD_SET_DEVICE_TIME           6
#define CMD_SEND_SELF_ADVERT          7
#define CMD_SET_ADVERT_NAME           8
#define CMD_ADD_UPDATE_CONTACT        9
#define CMD_SYNC_NEXT_MESSAGE         10
#define CMD_SET_RADIO_PARAMS          11
#define CMD_SET_RADIO_TX_POWER        12
#define CMD_RESET_PATH                13
#define CMD_SET_ADVERT_LATLON         14
#define CMD_REMOVE_CONTACT            15
#define CMD_SHARE_CONTACT             16
#define CMD_EXPORT_CONTACT            17
#define CMD_IMPORT_CONTACT            18
#define CMD_REBOOT                    19
#define CMD_GET_BATT_AND_STORAGE      20   // was CMD_GET_BATTERY_VOLTAGE
#define CMD_SET_TUNING_PARAMS         21
#define CMD_DEVICE_QEURY              22
#define CMD_EXPORT_PRIVATE_KEY        23
#define CMD_IMPORT_PRIVATE_KEY        24
#define CMD_SEND_RAW_DATA             25
#define CMD_SEND_LOGIN                26
#define CMD_SEND_STATUS_REQ           27
#define CMD_HAS_CONNECTION            28
#define CMD_LOGOUT                    29 // 'Disconnect'
#define CMD_GET_CONTACT_BY_KEY        30
#define CMD_GET_CHANNEL               31
#define CMD_SET_CHANNEL               32
#define CMD_SIGN_START                33
#define CMD_SIGN_DATA                 34
#define CMD_SIGN_FINISH               35
#define CMD_SEND_TRACE_PATH           36
#define CMD_SET_DEVICE_PIN            37
#define CMD_SET_OTHER_PARAMS          38
#define CMD_SEND_TELEMETRY_REQ        39  // can deprecate this
#define CMD_GET_CUSTOM_VARS           40
#define CMD_SET_CUSTOM_VAR            41
#define CMD_GET_ADVERT_PATH           42
#define CMD_GET_TUNING_PARAMS         43
// NOTE: CMD range 44..49 parked, potentially for WiFi operations
#define CMD_SEND_BINARY_REQ           50
#define CMD_FACTORY_RESET             51
#define CMD_SEND_PATH_DISCOVERY_REQ   52
#define CMD_SET_FLOOD_SCOPE           54   // v8+
#define CMD_SEND_CONTROL_DATA         55   // v8+
#define CMD_GET_STATS                 56   // v8+, second byte is stats type

// Control data sub-types for active node discovery
#define CTL_TYPE_NODE_DISCOVER_REQ    0x80
#define CTL_TYPE_NODE_DISCOVER_RESP   0x90
#define CMD_SEND_ANON_REQ             57
#define CMD_SET_AUTOADD_CONFIG        58
#define CMD_GET_AUTOADD_CONFIG        59
#define CMD_SET_PATH_HASH_MODE        61

// Stats sub-types for CMD_GET_STATS
#define STATS_TYPE_CORE               0
#define STATS_TYPE_RADIO              1
#define STATS_TYPE_PACKETS             2

#define RESP_CODE_OK                  0
#define RESP_CODE_ERR                 1
#define RESP_CODE_CONTACTS_START      2  // first reply to CMD_GET_CONTACTS
#define RESP_CODE_CONTACT             3  // multiple of these (after CMD_GET_CONTACTS)
#define RESP_CODE_END_OF_CONTACTS     4  // last reply to CMD_GET_CONTACTS
#define RESP_CODE_SELF_INFO           5  // reply to CMD_APP_START
#define RESP_CODE_SENT                6  // reply to CMD_SEND_TXT_MSG
#define RESP_CODE_CONTACT_MSG_RECV    7  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CHANNEL_MSG_RECV    8  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CURR_TIME           9  // a reply to CMD_GET_DEVICE_TIME
#define RESP_CODE_NO_MORE_MESSAGES    10 // a reply to CMD_SYNC_NEXT_MESSAGE
#define RESP_CODE_EXPORT_CONTACT      11
#define RESP_CODE_BATT_AND_STORAGE    12 // a reply to a CMD_GET_BATT_AND_STORAGE
#define RESP_CODE_DEVICE_INFO         13 // a reply to CMD_DEVICE_QEURY
#define RESP_CODE_PRIVATE_KEY         14 // a reply to CMD_EXPORT_PRIVATE_KEY
#define RESP_CODE_DISABLED            15
#define RESP_CODE_CONTACT_MSG_RECV_V3 16 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_MSG_RECV_V3 17 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_INFO        18 // a reply to CMD_GET_CHANNEL
#define RESP_CODE_SIGN_START          19
#define RESP_CODE_SIGNATURE           20
#define RESP_CODE_CUSTOM_VARS         21
#define RESP_CODE_ADVERT_PATH         22
#define RESP_CODE_TUNING_PARAMS       23
#define RESP_CODE_STATS               24   // v8+, second byte is stats type
#define RESP_CODE_AUTOADD_CONFIG      25

#define SEND_TIMEOUT_BASE_MILLIS        500
#define FLOOD_SEND_TIMEOUT_FACTOR       16.0f
#define DIRECT_SEND_PERHOP_FACTOR       6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS 250
#define LAZY_CONTACTS_WRITE_DELAY       5000

#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="

// these are _pushed_ to client app at any time
#define PUSH_CODE_ADVERT                0x80
#define PUSH_CODE_PATH_UPDATED          0x81
#define PUSH_CODE_SEND_CONFIRMED        0x82
#define PUSH_CODE_MSG_WAITING           0x83
#define PUSH_CODE_RAW_DATA              0x84
#define PUSH_CODE_LOGIN_SUCCESS         0x85
#define PUSH_CODE_LOGIN_FAIL            0x86
#define PUSH_CODE_STATUS_RESPONSE       0x87
#define PUSH_CODE_LOG_RX_DATA           0x88
#define PUSH_CODE_TRACE_DATA            0x89
#define PUSH_CODE_NEW_ADVERT            0x8A
#define PUSH_CODE_TELEMETRY_RESPONSE    0x8B
#define PUSH_CODE_BINARY_RESPONSE       0x8C
#define PUSH_CODE_PATH_DISCOVERY_RESPONSE 0x8D
#define PUSH_CODE_CONTROL_DATA          0x8E   // v8+
#define PUSH_CODE_CONTACT_DELETED       0x8F // used to notify client app of deleted contact when overwriting oldest
#define PUSH_CODE_CONTACTS_FULL         0x90 // used to notify client app that contacts storage is full

#define ERR_CODE_UNSUPPORTED_CMD        1
#define ERR_CODE_NOT_FOUND              2
#define ERR_CODE_TABLE_FULL             3
#define ERR_CODE_BAD_STATE              4
#define ERR_CODE_FILE_IO_ERROR          5
#define ERR_CODE_ILLEGAL_ARG            6

#define MAX_SIGN_DATA_LEN               (8 * 1024) // 8K

// Auto-add config bitmask
// Bit 0: If set, overwrite oldest non-favourite contact when contacts file is full
// Bits 1-4: these indicate which contact types to auto-add when manual_contact_mode = 0x01
#define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)  // 0x01 - overwrite oldest non-favourite when full
#define AUTO_ADD_CHAT             (1 << 1)  // 0x02 - auto-add Chat (Companion) (ADV_TYPE_CHAT)
#define AUTO_ADD_REPEATER         (1 << 2)  // 0x04 - auto-add Repeater (ADV_TYPE_REPEATER)
#define AUTO_ADD_ROOM_SERVER      (1 << 3)  // 0x08 - auto-add Room Server (ADV_TYPE_ROOM)
#define AUTO_ADD_SENSOR           (1 << 4)  // 0x10 - auto-add Sensor (ADV_TYPE_SENSOR)

void MyMesh::writeOKFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_OK;
  _serial->writeFrame(buf, 1);
}
void MyMesh::writeErrFrame(uint8_t err_code) {
  uint8_t buf[2];
  buf[0] = RESP_CODE_ERR;
  buf[1] = err_code;
  _serial->writeFrame(buf, 2);
}

void MyMesh::writeDisabledFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_DISABLED;
  _serial->writeFrame(buf, 1);
}

void MyMesh::writeContactRespFrame(uint8_t code, const ContactInfo &contact) {
  int i = 0;
  out_frame[i++] = code;
  memcpy(&out_frame[i], contact.id.pub_key, PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  out_frame[i++] = contact.type;
  out_frame[i++] = contact.flags;
  out_frame[i++] = contact.out_path_len;
  memcpy(&out_frame[i], contact.out_path, MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  StrHelper::strzcpy((char *)&out_frame[i], contact.name, 32);
  i += 32;
  memcpy(&out_frame[i], &contact.last_advert_timestamp, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lat, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lon, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.lastmod, 4);
  i += 4;
  _serial->writeFrame(out_frame, i);
}

void MyMesh::updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len) {
  int i = 0;
  uint8_t code = frame[i++]; // eg. CMD_ADD_UPDATE_CONTACT
  memcpy(contact.id.pub_key, &frame[i], PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  contact.type = frame[i++];
  contact.flags = frame[i++];
  contact.out_path_len = frame[i++];
  memcpy(contact.out_path, &frame[i], MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  memcpy(contact.name, &frame[i], 32);
  i += 32;
  memcpy(&contact.last_advert_timestamp, &frame[i], 4);
  i += 4;
  if (len >= i + 8) { // optional fields
    memcpy(&contact.gps_lat, &frame[i], 4);
    i += 4;
    memcpy(&contact.gps_lon, &frame[i], 4);
    i += 4;
    if (len >= i + 4) {
      memcpy(&last_mod, &frame[i], 4);
    }
  }
}

bool MyMesh::Frame::isChannelMsg() const {
  return buf[0] == RESP_CODE_CHANNEL_MSG_RECV || buf[0] == RESP_CODE_CHANNEL_MSG_RECV_V3;
}

void MyMesh::addToOfflineQueue(const uint8_t frame[], int len) {
  if (offline_queue_len >= OFFLINE_QUEUE_SIZE) {
    MESH_DEBUG_PRINTLN("WARN: offline_queue is full!");
    int pos = 0;
    while (pos < offline_queue_len) {
      if (offline_queue[pos].isChannelMsg()) {
        for (int i = pos; i < offline_queue_len - 1; i++) { // delete oldest channel msg from queue
          offline_queue[i] = offline_queue[i + 1];
        }
        MESH_DEBUG_PRINTLN("INFO: removed oldest channel message from queue.");
        offline_queue[offline_queue_len - 1].len = len;
        memcpy(offline_queue[offline_queue_len - 1].buf, frame, len);
        return;
      }
      pos++;
    }
    MESH_DEBUG_PRINTLN("INFO: no channel messages to remove from queue.");
  } else {
    offline_queue[offline_queue_len].len = len;
    memcpy(offline_queue[offline_queue_len].buf, frame, len);
    offline_queue_len++;
  }
}

int MyMesh::getFromOfflineQueue(uint8_t frame[]) {
  if (offline_queue_len > 0) {         // check offline queue
    size_t len = offline_queue[0].len; // take from top of queue
    memcpy(frame, offline_queue[0].buf, len);

    offline_queue_len--;
    for (int i = 0; i < offline_queue_len; i++) { // delete top item from queue
      offline_queue[i] = offline_queue[i + 1];
    }
    return len;
  }
  return 0; // queue is empty
}

float MyMesh::getAirtimeBudgetFactor() const {
  return _prefs.airtime_factor;
}

int MyMesh::getInterferenceThreshold() const {
  return _prefs.interference_threshold;
}

uint8_t MyMesh::getTxFailResetThreshold() const {
  return _prefs.tx_fail_reset_threshold;
}
uint8_t MyMesh::getRxFailRebootThreshold() const {
  return _prefs.rx_fail_reboot_threshold;
}
void MyMesh::onRxUnrecoverable() {
  board.reboot();
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint8_t MyMesh::getExtraAckTransmitCount() const {
  return _prefs.multi_acks;
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (uint32_t)(_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * 0.5f);
  return getRNG()->nextInt(0, 5*t + 1);
}

uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (uint32_t)(_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * 0.2f);
  return getRNG()->nextInt(0, 5*t + 1);
}

uint8_t MyMesh::getAutoAddMaxHops() const {
  return _prefs.autoadd_max_hops;
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  if (_serial->isConnected() && len + 3 <= MAX_FRAME_SIZE) {
    int i = 0;
    out_frame[i++] = PUSH_CODE_LOG_RX_DATA;
    out_frame[i++] = (int8_t)(snr * 4);
    out_frame[i++] = (int8_t)(rssi);
    memcpy(&out_frame[i], raw, len);
    i += len;

    _serial->writeFrame(out_frame, i);
  }
}

bool MyMesh::isAutoAddEnabled() const {
  return (_prefs.manual_add_contacts & 1) == 0;
}

bool MyMesh::shouldAutoAddContactType(uint8_t contact_type) const {
  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;
  }
  
  uint8_t type_bit = 0;
  switch (contact_type) {
    case ADV_TYPE_CHAT:
      type_bit = AUTO_ADD_CHAT;
      break;
    case ADV_TYPE_REPEATER:
      type_bit = AUTO_ADD_REPEATER;
      break;
    case ADV_TYPE_ROOM:
      type_bit = AUTO_ADD_ROOM_SERVER;
      break;
    case ADV_TYPE_SENSOR:
      type_bit = AUTO_ADD_SENSOR;
      break;
    default:
      return false;  // Unknown type, don't auto-add
  }
  
  return (_prefs.autoadd_config & type_bit) != 0;
}

bool MyMesh::shouldOverwriteWhenFull() const {
  return (_prefs.autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) != 0;
}

void MyMesh::onContactOverwrite(const uint8_t* pub_key) {
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACT_DELETED;
    memcpy(&out_frame[1], pub_key, PUB_KEY_SIZE);
    _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
  }
}

void MyMesh::onContactsFull() {
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACTS_FULL;
    _serial->writeFrame(out_frame, 1);
  }
}

void MyMesh::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (_serial->isConnected()) {
    if (is_new) {
      writeContactRespFrame(PUSH_CODE_NEW_ADVERT, contact);
    } else {
      out_frame[0] = PUSH_CODE_ADVERT;
      memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
      _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
    }
  } 
#ifdef DISPLAY_CLASS
  if (_ui && !_prefs.buzzer_quiet) _ui->notify(UIEventType::newContactMessage); //buzz if enabled
#endif

  // add inbound-path to mem cache
  if (path && mesh::Packet::isValidPathLen(path_len)) {  // check path is valid
    AdvertPath* p = advert_paths;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {   // check if already in table, otherwise evict oldest
      if (memcmp(advert_paths[i].pubkey_prefix, contact.id.pub_key, sizeof(AdvertPath::pubkey_prefix)) == 0) {
        p = &advert_paths[i];   // found
        break;
      }
      if (advert_paths[i].recv_timestamp < oldest) {
        oldest = advert_paths[i].recv_timestamp;
        p = &advert_paths[i];
      }
    }

    memcpy(p->pubkey_prefix, contact.id.pub_key, sizeof(p->pubkey_prefix));
    strcpy(p->name, contact.name);
    p->type = contact.type;
    p->recv_timestamp = getRTCClock()->getCurrentTime();
    p->path_len = mesh::Packet::copyPath(p->path, path, path_len);
  }

  // Buffer for on-device discovery UI
  if (_discoveryActive && _discoveredCount < MAX_DISCOVERED_NODES) {
    bool dup = false;
    for (int i = 0; i < _discoveredCount; i++) {
      if (contact.id.matches(_discovered[i].contact.id)) {
        // Update existing entry with fresher data
        _discovered[i].contact = contact;
        _discovered[i].path_len = path_len;
        _discovered[i].already_in_contacts = !is_new;
        // Preserve snr if already set by active discovery response
        dup = true;
        Serial.printf("[Discovery] Updated: %s (hops=%d)\n", contact.name, path_len);
        break;
      }
    }
    if (!dup) {
      _discovered[_discoveredCount].contact = contact;
      _discovered[_discoveredCount].path_len = path_len;
      _discovered[_discoveredCount].snr = 0;  // no SNR from passive advert
      _discovered[_discoveredCount].already_in_contacts = !is_new;
      _discoveredCount++;
      Serial.printf("[Discovery] Found: %s (hops=%d, is_new=%d, total=%d)\n",
                    contact.name, path_len, is_new, _discoveredCount);
    }
  }

  if (!is_new) dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY); // only schedule lazy write for contacts that are in contacts[]
}

static int sort_by_recent(const void *a, const void *b) {
  return ((AdvertPath *) b)->recv_timestamp - ((AdvertPath *) a)->recv_timestamp;
}

int MyMesh::getRecentlyHeard(AdvertPath dest[], int max_num) {
  if (max_num > ADVERT_PATH_TABLE_SIZE) max_num = ADVERT_PATH_TABLE_SIZE;
  qsort(advert_paths, ADVERT_PATH_TABLE_SIZE, sizeof(advert_paths[0]), sort_by_recent);

  for (int i = 0; i < max_num; i++) {
    dest[i] = advert_paths[i];
  }
  return max_num;
}

void MyMesh::scheduleLazyContactSave() {
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
}

void MyMesh::onContactPathUpdated(const ContactInfo &contact) {
  out_frame[0] = PUSH_CODE_PATH_UPDATED;
  memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
  _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE); // NOTE: app may not be connected

  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
}

ContactInfo*  MyMesh::processAck(const uint8_t *data) {
  // see if matches any in a table
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; i++) {
    if (memcmp(data, &expected_ack_table[i].ack, 4) == 0) { // got an ACK from recipient
      out_frame[0] = PUSH_CODE_SEND_CONFIRMED;
      memcpy(&out_frame[1], data, 4);
      uint32_t trip_time = _ms->getMillis() - expected_ack_table[i].msg_sent;
      memcpy(&out_frame[5], &trip_time, 4);
      _serial->writeFrame(out_frame, 9);

      // NOTE: the same ACK can be received multiple times!
      expected_ack_table[i].ack = 0; // clear expected hash, now that we have received ACK
      return expected_ack_table[i].contact;
    }
  }
  return checkConnectionsAck(data);
}

void MyMesh::queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt,
                          uint32_t sender_timestamp, const uint8_t *extra, int extra_len, const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV;
  }
  memcpy(&out_frame[i], from.id.pub_key, 6);
  i += 6; // just 6-byte prefix
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = txt_type;
  memcpy(&out_frame[i], &sender_timestamp, 4);
  i += 4;
  if (extra_len > 0) {
    memcpy(&out_frame[i], extra, extra_len);
    i += extra_len;
  }
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }

#ifdef DISPLAY_CLASS
  // we only want to show text messages on display, not cli data
  bool should_display = txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN;
  if (should_display && _ui) {
    const uint8_t* msg_path = (pkt->isRouteFlood() && pkt->path_len > 0) ? pkt->path : nullptr;

    // For signed messages (room server posts): the extra bytes contain the
    // original poster's pub_key prefix. Look up their name and format as
    // "PosterName: message" so the UI shows who actually wrote it.
    if (txt_type == TXT_TYPE_SIGNED_PLAIN && extra && extra_len >= 4) {
      ContactInfo* poster = lookupContactByPubKey(extra, extra_len);
      if (poster) {
        char formatted[MAX_PACKET_PAYLOAD];
        snprintf(formatted, sizeof(formatted), "%s: %s", poster->name, text);
        _ui->newMsg(path_len, from.name, formatted, offline_queue_len, msg_path, pkt->_snr);
      } else {
        // Poster not in contacts — show raw text (no name prefix)
        _ui->newMsg(path_len, from.name, text, offline_queue_len, msg_path, pkt->_snr);
      }
    } else {
      _ui->newMsg(path_len, from.name, text, offline_queue_len, msg_path, pkt->_snr);
    }

    if (!_prefs.buzzer_quiet) _ui->notify(UIEventType::contactMessage); //buzz if enabled
  }
#endif
}

bool MyMesh::filterRecvFloodPacket(mesh::Packet* packet) {
  // Check if this incoming flood packet is a repeat of a message we recently sent
  if (packet->payload_len >= SENT_FINGERPRINT_SIZE) {
    unsigned long now = millis();
    for (int i = 0; i < SENT_TRACK_SIZE; i++) {
      SentMsgTrack* t = &_sent_track[i];
      if (!t->active) continue;

      // Expire old entries
      if ((now - t->sent_millis) > SENT_TRACK_EXPIRY_MS) {
        t->active = false;
        continue;
      }

      // Compare payload fingerprint
      if (memcmp(packet->payload, t->fingerprint, SENT_FINGERPRINT_SIZE) == 0) {
        t->repeat_count++;
        MESH_DEBUG_PRINTLN("SentTrack: heard repeat #%d (SNR=%.1f)", t->repeat_count, packet->getSNR());
        
#ifdef DISPLAY_CLASS
        if (_ui) {
          char buf[40];
          snprintf(buf, sizeof(buf), "Sent! (%d)", t->repeat_count);
          _ui->showAlert(buf, 2000);  // show/extend alert with updated count
        }
#endif
        break;  // found match, no need to check other entries
      }
    }
  }

  return false;  // never filter Ã¢â‚¬â€ let normal processing continue
}

void MyMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  Serial.printf("[sendFloodScoped] to '%s', delay=%lu, hash_mode=%d, bph=%d\n",
                recipient.name, delay_millis, _prefs.path_hash_mode, _prefs.path_hash_mode + 1);
  // TODO: dynamic send_scope, depending on recipient and current 'home' Region
  if (send_scope.isNull()) {
    sendFlood(pkt, delay_millis, getPathHashSize());
  } else {
    uint16_t codes[2];
    codes[0] = send_scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, getPathHashSize());
  }
}
void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  // Capture payload fingerprint for repeat tracking before sending
  if (pkt->payload_len >= SENT_FINGERPRINT_SIZE) {
    SentMsgTrack* t = &_sent_track[_sent_track_idx];
    memcpy(t->fingerprint, pkt->payload, SENT_FINGERPRINT_SIZE);
    t->repeat_count = 0;
    t->sent_millis = millis();
    t->active = true;
    _sent_track_idx = (_sent_track_idx + 1) % SENT_TRACK_SIZE;
    MESH_DEBUG_PRINTLN("SentTrack: captured fingerprint for channel msg");
  }

  // TODO: have per-channel send_scope
  if (send_scope.isNull()) {
    sendFlood(pkt, delay_millis, getPathHashSize());
  } else {
    uint16_t codes[2];
    codes[0] = send_scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, getPathHashSize());
  }
}

void MyMesh::onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_PLAIN, pkt, sender_timestamp, NULL, 0, text);
}

void MyMesh::onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                               const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_CLI_DATA, pkt, sender_timestamp, NULL, 0, text);

  // Forward CLI response to UI admin screen if admin session is active
#ifdef DISPLAY_CLASS
  if (_admin_contact_idx >= 0 && _ui) {
    _ui->onAdminCliResponse(from.name, text);
  }
#endif
}

void MyMesh::onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                                 const uint8_t *sender_prefix, const char *text) {
  markConnectionActive(from);
  // from.sync_since change needs to be persisted
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  queueMessage(from, TXT_TYPE_SIGNED_PLAIN, pkt, sender_timestamp, sender_prefix, 4, text);
}

void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;
  }

  uint8_t channel_idx = findChannelIdx(channel);
  out_frame[i++] = channel_idx;
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;

  out_frame[i++] = TXT_TYPE_PLAIN;
  memcpy(&out_frame[i], &timestamp, 4);
  i += 4;
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }

#ifdef DISPLAY_CLASS
  // Get the channel name from the channel index
  const char *channel_name = "Unknown";
  ChannelDetails channel_details;
  if (getChannel(channel_idx, channel_details)) {
    channel_name = channel_details.name;
  }
  if (_ui) {
    const uint8_t* msg_path = (pkt->isRouteFlood() && pkt->path_len > 0) ? pkt->path : nullptr;
    _ui->newMsg(path_len, channel_name, text, offline_queue_len, msg_path, pkt->_snr);
    if (!_prefs.buzzer_quiet) _ui->notify(UIEventType::channelMessage); //buzz if enabled
  }
#endif
}

void MyMesh::queueSentChannelMessage(uint8_t channel_idx, uint32_t timestamp, const char* sender, const char* text) {
  // Format message the same way as onChannelMessageRecv for BLE app sync
  // This allows sent messages from device keyboard to appear in the app
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = 0;  // SNR not applicable for sent messages
    out_frame[i++] = 0;  // reserved1
    out_frame[i++] = 0;  // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;
  }

  out_frame[i++] = channel_idx;
  out_frame[i++] = 0;  // path_len = 0 indicates local/sent message

  out_frame[i++] = TXT_TYPE_PLAIN;
  memcpy(&out_frame[i], &timestamp, 4);
  i += 4;
  
  // Format as "sender: text" like the app expects
  char formatted[MAX_FRAME_SIZE];
  snprintf(formatted, sizeof(formatted), "%s: %s", sender, text);
  int tlen = strlen(formatted);
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], formatted, tlen);
  i += tlen;
  
  addToOfflineQueue(out_frame, i);

  // If app is connected, send push notification
  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING;
    _serial->writeFrame(frame, 1);
  }
}

bool MyMesh::uiSendDirectMessage(uint32_t contact_idx, const char* text) {
  ContactInfo contact;
  if (!getContactByIdx(contact_idx, contact)) return false;

  ContactInfo* recipient = lookupContactByPubKey(contact.id.pub_key, PUB_KEY_SIZE);
  if (!recipient) return false;

  uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
  uint32_t expected_ack, est_timeout;
  int result = sendMessage(*recipient, timestamp, 0, text, expected_ack, est_timeout);

  if (result == MSG_SEND_FAILED) {
    MESH_DEBUG_PRINTLN("UI: DM send failed to %s", recipient->name);
    return false;
  }

  // Track expected ACK for delivery confirmation
  if (expected_ack) {
    expected_ack_table[next_ack_idx].msg_sent = _ms->getMillis();
    expected_ack_table[next_ack_idx].ack = expected_ack;
    expected_ack_table[next_ack_idx].contact = recipient;
    next_ack_idx = (next_ack_idx + 1) % EXPECTED_ACK_TABLE_SIZE;
  }

  MESH_DEBUG_PRINTLN("UI: DM sent to %s (%s), ack=0x%08X timeout=%dms",
                     recipient->name, result == MSG_SEND_SENT_FLOOD ? "flood" : "direct",
                     expected_ack, est_timeout);
  return true;
}

bool MyMesh::uiLoginToRepeater(uint32_t contact_idx, const char* password, uint32_t& est_timeout_ms) {
  ContactInfo contact;
  if (!getContactByIdx(contact_idx, contact)) {
    Serial.println("[uiLogin] getContactByIdx FAILED");
    return false;
  }

  ContactInfo* recipient = lookupContactByPubKey(contact.id.pub_key, PUB_KEY_SIZE);
  if (!recipient) {
    Serial.println("[uiLogin] lookupContactByPubKey FAILED");
    return false;
  }

  // Force flood routing for login — a mobile repeater's direct path may be stale.
  // The companion protocol does the same for telemetry requests.
  uint8_t save_path_len = recipient->out_path_len;
  recipient->out_path_len = OUT_PATH_UNKNOWN;

  // For room servers: reset sync_since to zero so the server pushes ALL posts.
  // The device has no persistent DM storage, so every session needs full history.
  // sync_since naturally updates as messages arrive (BaseChatMesh::onPeerDataRecv).
  if (recipient->type == ADV_TYPE_ROOM) {
    recipient->sync_since = 0;
  }

  Serial.printf("[uiLogin] Sending login to '%s' (idx=%d, path was 0x%02X, now 0x%02X, hash_mode=%d)\n",
                recipient->name, contact_idx, save_path_len, recipient->out_path_len, _prefs.path_hash_mode);

  int result = sendLogin(*recipient, password, est_timeout_ms);

  recipient->out_path_len = save_path_len;  // restore

  Serial.printf("[uiLogin] sendLogin result=%d est_timeout=%ums\n", result, est_timeout_ms);

  if (result == MSG_SEND_FAILED) {
    Serial.println("[uiLogin] FAILED - MSG_SEND_FAILED");
    est_timeout_ms = 0;
    return false;
  }

  clearPendingReqs();
  memcpy(&pending_login, recipient->id.pub_key, 4);
  _admin_contact_idx = contact_idx;

  Serial.printf("[uiLogin] SUCCESS - login sent to %s (flood), timeout=%dms\n",
                recipient->name, est_timeout_ms);
  return true;
}

bool MyMesh::uiSendCliCommand(uint32_t contact_idx, const char* command) {
  ContactInfo contact;
  if (!getContactByIdx(contact_idx, contact)) return false;

  ContactInfo* recipient = lookupContactByPubKey(contact.id.pub_key, PUB_KEY_SIZE);
  if (!recipient) return false;

  uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
  uint32_t est_timeout;
  int result = sendCommandData(*recipient, timestamp, 0, command, est_timeout);
  if (result == MSG_SEND_FAILED) {
    MESH_DEBUG_PRINTLN("UI: CLI command send failed to %s: %s", recipient->name, command);
    return false;
  }

  _admin_contact_idx = contact_idx;

  MESH_DEBUG_PRINTLN("UI: CLI command sent to %s (%s): %s, timeout=%dms",
                     recipient->name, result == MSG_SEND_SENT_FLOOD ? "flood" : "direct",
                     command, est_timeout);
  return true;
}

bool MyMesh::uiSendTelemetryRequest(uint32_t contact_idx) {
  ContactInfo contact;
  if (!getContactByIdx(contact_idx, contact)) return false;

  ContactInfo* recipient = lookupContactByPubKey(contact.id.pub_key, PUB_KEY_SIZE);
  if (!recipient) return false;

  uint32_t tag, est_timeout;
  int result = sendRequest(*recipient, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout);
  if (result == MSG_SEND_FAILED) {
    MESH_DEBUG_PRINTLN("UI: Telemetry request send failed to %s", recipient->name);
    return false;
  }

  clearPendingReqs();
  pending_telemetry = tag;

  MESH_DEBUG_PRINTLN("UI: Telemetry request sent to %s (%s), timeout=%dms",
                     recipient->name, result == MSG_SEND_SENT_FLOOD ? "flood" : "direct",
                     est_timeout);
  return true;
}

uint8_t MyMesh::onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                 uint8_t len, uint8_t *reply) {
  if (data[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t permissions = 0;
    uint8_t cp = contact.flags >> 1; // LSB used as 'favourite' bit (so only use upper bits)

    if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_ALL) {
      permissions = TELEM_PERM_BASE;
    } else if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_FLAGS) {
      permissions = cp & TELEM_PERM_BASE;
    }

    if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_LOCATION;
    } else if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_LOCATION;
    }

    if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_ENVIRONMENT;
    } else if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_ENVIRONMENT;
    }

    uint8_t perm_mask = ~(data[1]);    // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions
    permissions &= perm_mask;

    if (permissions & TELEM_PERM_BASE) { // only respond if base permission bit is set
      telemetry.reset();
      telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      // query other sensors -- target specific
      sensors.querySensors(permissions, telemetry);

      memcpy(reply, &sender_timestamp,
             4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

      uint8_t tlen = telemetry.getSize();
      memcpy(&reply[4], telemetry.getBuffer(), tlen);
      return 4 + tlen;
    }
  }
  return 0; // unknown
}

void MyMesh::onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) {
  uint32_t tag;
  memcpy(&tag, data, 4);

  Serial.printf("[onContactResponse] from '%s', tag=0x%08X, len=%d, pending_login=0x%08X\n",
                contact.name, tag, len, pending_login);

  if (pending_login && memcmp(&pending_login, contact.id.pub_key, 4) == 0) { // check for login response
    // yes, is response to pending sendLogin()
    pending_login = 0;

    int i = 0;
    if (memcmp(&data[4], "OK", 2) == 0) { // legacy Repeater login OK response
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = 0; // legacy: is_admin = false
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6;                                     // pub_key_prefix

      #ifdef DISPLAY_CLASS
      // Notify UI of successful legacy login
      if (_ui) _ui->onAdminLoginResult(true, 0, tag);
      #endif
    } else if (data[4] == RESP_SERVER_LOGIN_OK) { // new login response
      uint16_t keep_alive_secs = ((uint16_t)data[5]) * 16;
      if (keep_alive_secs > 0) {
        startConnection(contact, keep_alive_secs);
      }
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = data[6]; // permissions (eg. is_admin)
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
      memcpy(&out_frame[i], &tag, 4);
      i += 4; // NEW: include server timestamp
      out_frame[i++] = data[7]; // NEW (v7): ACL permissions
      out_frame[i++] = data[12]; // FIRMWARE_VER_LEVEL

      #ifdef DISPLAY_CLASS
      // Notify UI of successful login
      if (_ui) _ui->onAdminLoginResult(true, data[6], tag);
      #endif
    } else {
      out_frame[i++] = PUSH_CODE_LOGIN_FAIL;
      out_frame[i++] = 0; // reserved
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix

      #ifdef DISPLAY_CLASS
      // Notify UI of login failure
      if (_ui) _ui->onAdminLoginResult(false, 0, 0);
      #endif
    }
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && // check for status response
             pending_status &&
             memcmp(&pending_status, contact.id.pub_key, 4) == 0 // legacy matching scheme
                                                                 // FUTURE: tag == pending_status
  ) {
    pending_status = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_STATUS_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_telemetry) {  // check for matching response tag
    pending_telemetry = 0;
    MESH_DEBUG_PRINTLN("Telemetry response received from %s, len=%d", contact.name, len);

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);

    #ifdef DISPLAY_CLASS
    // Route telemetry data to UI (LPP buffer after the 4-byte tag)
    if (_ui) _ui->onAdminTelemetryResult(&data[4], len - 4);
    #endif
  } else if (len > 4 && tag == pending_req) {  // check for matching response tag
    pending_req = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], &tag, 4);   // app needs to match this to RESP_CODE_SENT.tag
    i += 4;
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  }
}

bool MyMesh::onContactPathRecv(ContactInfo& contact, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  if (extra_type == PAYLOAD_TYPE_RESPONSE && extra_len > 4) {
    uint32_t tag;
    memcpy(&tag, extra, 4);

    if (tag == pending_discovery) {  // check for matching response tag)
      pending_discovery = 0;

      if (!mesh::Packet::isValidPathLen(in_path_len) || !mesh::Packet::isValidPathLen(out_path_len)) {
        MESH_DEBUG_PRINTLN("onContactPathRecv, invalid path sizes: %d, %d", in_path_len, out_path_len);
      } else {
        int i = 0;
        out_frame[i++] = PUSH_CODE_PATH_DISCOVERY_RESPONSE;
        out_frame[i++] = 0; // reserved
        memcpy(&out_frame[i], contact.id.pub_key, 6);
        i += 6; // pub_key_prefix
        out_frame[i++] = out_path_len;
        i += mesh::Packet::writePath(&out_frame[i], out_path, out_path_len);
        out_frame[i++] = in_path_len;
        i += mesh::Packet::writePath(&out_frame[i], in_path, in_path_len);
        // NOTE: telemetry data in 'extra' is discarded at present

        _serial->writeFrame(out_frame, i);
      }
      return false;  // DON'T send reciprocal path!
    }
  }
  // let base class handle received path and data
  return BaseChatMesh::onContactPathRecv(contact, in_path, in_path_len, out_path, out_path_len, extra_type, extra, extra_len);
}

void MyMesh::onControlDataRecv(mesh::Packet *packet) {
  // --- Active discovery response interception ---
  if (_discoveryActive && packet->payload_len >= 6) {
    uint8_t resp_type = packet->payload[0] & 0xF0;
    if (resp_type == CTL_TYPE_NODE_DISCOVER_RESP) {
      uint8_t node_type = packet->payload[0] & 0x0F;
      int8_t snr_scaled = (int8_t)packet->payload[1];   // SNR × 4 (how well repeater heard us)
      uint32_t tag;
      memcpy(&tag, &packet->payload[2], 4);

      // Validate: tag must match ours AND payload must include full 32-byte pubkey
      if (tag == _discoveryTag && packet->payload_len >= 6 + PUB_KEY_SIZE) {
        const uint8_t* pubkey = &packet->payload[6];

        // Dedup check against existing buffer entries (pre-seeded or earlier responses)
        for (int i = 0; i < _discoveredCount; i++) {
          if (_discovered[i].contact.id.matches(pubkey)) {
            // Already in buffer — update SNR (active discovery data is fresher)
            _discovered[i].snr = snr_scaled;
            Serial.printf("[Discovery] Updated SNR for %s: %d\n",
                          _discovered[i].contact.name, snr_scaled);
            return;
          }
        }

        // New node — add if room
        if (_discoveredCount < MAX_DISCOVERED_NODES) {
          DiscoveredNode& node = _discovered[_discoveredCount];
          memset(&node.contact, 0, sizeof(ContactInfo));
          memcpy(node.contact.id.pub_key, pubkey, PUB_KEY_SIZE);
          node.contact.type = node_type;
          node.snr = snr_scaled;
          node.path_len = packet->path_len;

          // Try to resolve name from contacts table
          ContactInfo* existing = lookupContactByPubKey(pubkey, PUB_KEY_SIZE);
          if (existing) {
            strncpy(node.contact.name, existing->name, sizeof(node.contact.name) - 1);
            node.already_in_contacts = true;
          } else {
            // Show hex prefix as placeholder name
            snprintf(node.contact.name, sizeof(node.contact.name),
                     "%02X%02X%02X%02X",
                     pubkey[0], pubkey[1], pubkey[2], pubkey[3]);
            node.already_in_contacts = false;
          }

          _discoveredCount++;
          Serial.printf("[Discovery] Active response: %s type=%d snr=%d hops=%d (total=%d)\n",
                        node.contact.name, node_type, snr_scaled, packet->path_len, _discoveredCount);
        }
      }
      return;  // consumed — don't forward discovery responses to BLE
    }
  }

  // --- Original BLE forwarding for non-discovery control data ---
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_CONTROL_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = packet->path_len;
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), data received while app offline");
  }
}

void MyMesh::onRawDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_RAW_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = 0xFF; // reserved (possibly path_len in future)
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), data received while app offline");
  }
}

void MyMesh::onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) {
  uint8_t path_sz = flags & 0x03;  // NEW v1.11+
  if (12 + path_len + (path_len >> path_sz) + 1 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onTraceRecv(), path_len is too long: %d", (uint32_t)path_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_TRACE_DATA;
  out_frame[i++] = 0; // reserved
  out_frame[i++] = path_len;
  out_frame[i++] = flags;
  memcpy(&out_frame[i], &tag, 4);
  i += 4;
  memcpy(&out_frame[i], &auth_code, 4);
  i += 4;
  memcpy(&out_frame[i], path_hashes, path_len);
  i += path_len;

  memcpy(&out_frame[i], path_snrs, path_len >> path_sz);
  i += path_len >> path_sz;
  out_frame[i++] = (int8_t)(packet->getSNR() * 4); // extra/final SNR (to this node)

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onTraceRecv(), data received while app offline");
  }
}

uint32_t MyMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
}
uint32_t MyMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  uint8_t hop_count = path_len & 63;  // extract hops, ignore mode bits
  return SEND_TIMEOUT_BASE_MILLIS +
         ((pkt_airtime_millis * DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) *
          (hop_count + 1));
}

void MyMesh::onSendTimeout() {}

MyMesh::MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store, AbstractUITask* ui)
    : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables),
      _serial(NULL), telemetry(MAX_PACKET_PAYLOAD - 4), _store(&store), _ui(ui) {
  _iter_started = false;
  _cli_rescue = false;
  offline_queue_len = 0;
  app_target_ver = 0;
  clearPendingReqs();
  next_ack_idx = 0;
  sign_data = NULL;
  dirty_contacts_expiry = 0;
  memset(advert_paths, 0, sizeof(advert_paths));
  memset(send_scope.key, 0, sizeof(send_scope.key));
  memset(_sent_track, 0, sizeof(_sent_track));
  _sent_track_idx = 0;
  _admin_contact_idx = -1;
  _discoveredCount = 0;
  _discoveryActive = false;
  _discoveryTimeout = 0;
  _discoveryTag = 0;

  // defaults
  memset(&_prefs, 0, sizeof(_prefs));
  _prefs.airtime_factor = 1.0; // one half
  _prefs.multi_acks = 1;      // redundant ACKs on by default
  strcpy(_prefs.node_name, "NONAME");
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.buzzer_quiet = 0;
  _prefs.gps_enabled = 0;       // GPS disabled by default
  _prefs.gps_interval = 0;      // No automatic GPS updates by default
  //_prefs.rx_delay_base = 10.0f;  enable once new algo fixed
}

void MyMesh::begin(bool has_display) {
  BaseChatMesh::begin();

  if (!_store->loadMainIdentity(self_id)) {
    self_id = radio_new_identity(); // create new random identity
    int count = 0;
    while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) { // reserved id hashes
      self_id = radio_new_identity();
      count++;
    }
    _store->saveMainIdentity(self_id);
  }

// if name is provided as a build flag, use that as default node name instead
#ifdef ADVERT_NAME
  strcpy(_prefs.node_name, ADVERT_NAME);
#else
  // use hex of first 4 bytes of identity public key as default node name
  char pub_key_hex[10];
  mesh::Utils::toHex(pub_key_hex, self_id.pub_key, 4);
  strcpy(_prefs.node_name, pub_key_hex);
#endif

  // load persisted prefs
  _store->loadPrefs(_prefs, sensors.node_lat, sensors.node_lon);

  // sanitise bad pref values
  _prefs.rx_delay_base = constrain(_prefs.rx_delay_base, 0, 20.0f);
  _prefs.airtime_factor = constrain(_prefs.airtime_factor, 0, 9.0f);
  _prefs.freq = constrain(_prefs.freq, 400.0f, 2500.0f);
  _prefs.bw = constrain(_prefs.bw, 7.8f, 500.0f);
  _prefs.sf = constrain(_prefs.sf, 5, 12);
  _prefs.cr = constrain(_prefs.cr, 5, 8);
  _prefs.tx_power_dbm = constrain(_prefs.tx_power_dbm, 1, MAX_LORA_TX_POWER);
  _prefs.buzzer_quiet = constrain(_prefs.buzzer_quiet, 0, 1);  // Ensure boolean 0 or 1
  _prefs.gps_enabled = constrain(_prefs.gps_enabled, 0, 1);  // Ensure boolean 0 or 1
  _prefs.gps_interval = constrain(_prefs.gps_interval, 0, 86400);  // Max 24 hours
  _prefs.utc_offset_hours = constrain(_prefs.utc_offset_hours, -12, 14);  // Valid timezone range
  // gps_baudrate: 0 means use compile-time default; validate known rates
  if (_prefs.gps_baudrate != 0 && _prefs.gps_baudrate != 4800 &&
      _prefs.gps_baudrate != 9600 && _prefs.gps_baudrate != 19200 &&
      _prefs.gps_baudrate != 38400 && _prefs.gps_baudrate != 57600 &&
      _prefs.gps_baudrate != 115200) {
    Serial.printf("PREFS: invalid gps_baudrate=%lu — reset to 0 (default)\n",
                  (unsigned long)_prefs.gps_baudrate);
    _prefs.gps_baudrate = 0;  // reset to default if invalid
  }
  // interference_threshold: 0 = disabled, minimum functional value is 14, max sane ~30
  if (_prefs.interference_threshold > 0 && _prefs.interference_threshold < 14) {
    _prefs.interference_threshold = 0;
  }
  if (_prefs.interference_threshold > 50) {
    Serial.printf("PREFS: invalid interference_threshold=%d — reset to 0 (disabled)\n",
                  _prefs.interference_threshold);
    _prefs.interference_threshold = 0;  // garbage from prefs upgrade — disable
  }
  // Clamp remaining v1.0 fields that may contain garbage after upgrade from older firmware
  if (_prefs.path_hash_mode > 2) _prefs.path_hash_mode = 0;
  if (_prefs.autoadd_max_hops > 64) _prefs.autoadd_max_hops = 0;
  if (_prefs.dark_mode > 1) _prefs.dark_mode = 0;
  if (_prefs.portrait_mode > 1) _prefs.portrait_mode = 0;
  if (_prefs.hint_shown > 1) _prefs.hint_shown = 0;

#ifdef BLE_PIN_CODE // 123456 by default
  if (_prefs.ble_pin == 0) {
#ifdef DISPLAY_CLASS
    if (has_display && BLE_PIN_CODE == 123456) {
      StdRNG rng;
      _active_ble_pin = rng.nextInt(100000, 999999); // random pin each session
    } else {
      _active_ble_pin = BLE_PIN_CODE; // otherwise static pin
    }
#else
    _active_ble_pin = BLE_PIN_CODE; // otherwise static pin
#endif
  } else {
    _active_ble_pin = _prefs.ble_pin;
  }
#else
  _active_ble_pin = 0;
#endif

  initContacts();    // allocate contacts array from PSRAM (deferred from constructor)
  resetContacts();
  _store->loadContacts(this);
  bootstrapRTCfromContacts();
  addChannel("Public", PUBLIC_GROUP_PSK); // pre-configure Andy's public channel
  _store->loadChannels(this);

  radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_set_tx_power(_prefs.tx_power_dbm);
}

const char *MyMesh::getNodeName() {
  return _prefs.node_name;
}
NodePrefs *MyMesh::getNodePrefs() {
  return &_prefs;
}
uint32_t MyMesh::getBLEPin() {
  return _active_ble_pin;
}

void MyMesh::startInterface(BaseSerialInterface &serial) {
  _serial = &serial;
  serial.enable();
}

void MyMesh::handleCmdFrame(size_t len) {
  if (cmd_frame[0] == CMD_DEVICE_QEURY && len >= 2) { // sent when app establishes connection
    app_target_ver = cmd_frame[1];                    // which version of protocol does app understand

    int i = 0;
    out_frame[i++] = RESP_CODE_DEVICE_INFO;
    out_frame[i++] = FIRMWARE_VER_CODE;
    out_frame[i++] = MAX_CONTACTS / 2;   // v3+
    out_frame[i++] = MAX_GROUP_CHANNELS; // v3+
    memcpy(&out_frame[i], &_prefs.ble_pin, 4);
    i += 4;
    memset(&out_frame[i], 0, 12);
    strcpy((char *)&out_frame[i], FIRMWARE_BUILD_DATE);
    i += 12;
    StrHelper::strzcpy((char *)&out_frame[i], board.getManufacturerName(), 40);
    i += 40;
    StrHelper::strzcpy((char *)&out_frame[i], FIRMWARE_VERSION, 20);
    i += 20;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_APP_START &&
             len >= 8) { // sent when app establishes connection, respond with node ID
    //  cmd_frame[1..7]  reserved future
    char *app_name = (char *)&cmd_frame[8];
    cmd_frame[len] = 0; // make app_name null terminated
    MESH_DEBUG_PRINTLN("App %s connected", app_name);

    _iter_started = false; // stop any left-over ContactsIterator
    int i = 0;
    out_frame[i++] = RESP_CODE_SELF_INFO;
    out_frame[i++] = ADV_TYPE_CHAT; // what this node Advert identifies as (maybe node's pronouns too?? :-)
    out_frame[i++] = _prefs.tx_power_dbm;
    out_frame[i++] = MAX_LORA_TX_POWER;
    memcpy(&out_frame[i], self_id.pub_key, PUB_KEY_SIZE);
    i += PUB_KEY_SIZE;

    int32_t lat, lon;
    lat = (sensors.node_lat * 1000000.0);
    lon = (sensors.node_lon * 1000000.0);
    memcpy(&out_frame[i], &lat, 4);
    i += 4;
    memcpy(&out_frame[i], &lon, 4);
    i += 4;
    out_frame[i++] = _prefs.multi_acks; // new v7+
    out_frame[i++] = _prefs.advert_loc_policy;
    out_frame[i++] = (_prefs.telemetry_mode_env << 4) | (_prefs.telemetry_mode_loc << 2) |
                     (_prefs.telemetry_mode_base); // v5+
    out_frame[i++] = _prefs.manual_add_contacts;

    uint32_t freq = _prefs.freq * 1000;
    memcpy(&out_frame[i], &freq, 4);
    i += 4;
    uint32_t bw = _prefs.bw * 1000;
    memcpy(&out_frame[i], &bw, 4);
    i += 4;
    out_frame[i++] = _prefs.sf;
    out_frame[i++] = _prefs.cr;

    int tlen = strlen(_prefs.node_name); // revisit: UTF_8 ??
    memcpy(&out_frame[i], _prefs.node_name, tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_TXT_MSG && len >= 14) {
    int i = 1;
    uint8_t txt_type = cmd_frame[i++];
    uint8_t attempt = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    uint8_t *pub_key_prefix = &cmd_frame[i];
    i += 6;
    ContactInfo *recipient = lookupContactByPubKey(pub_key_prefix, 6);
    if (recipient && (txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_CLI_DATA)) {
      char *text = (char *)&cmd_frame[i];
      int tlen = len - i;
      uint32_t est_timeout;
      text[tlen] = 0; // ensure null
      int result;
      uint32_t expected_ack;
      if (txt_type == TXT_TYPE_CLI_DATA) {
        msg_timestamp = getRTCClock()->getCurrentTimeUnique(); // Use node's RTC instead of app timestamp to avoid tripping replay protection
        result = sendCommandData(*recipient, msg_timestamp, attempt, text, est_timeout);
        expected_ack = 0; // no Ack expected
      } else {
        result = sendMessage(*recipient, msg_timestamp, attempt, text, expected_ack, est_timeout);
      }
      // TODO: add expected ACK to table
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        if (expected_ack) {
          expected_ack_table[next_ack_idx].msg_sent = _ms->getMillis(); // add to circular table
          expected_ack_table[next_ack_idx].ack = expected_ack;
          expected_ack_table[next_ack_idx].contact = recipient;
          next_ack_idx = (next_ack_idx + 1) % EXPECTED_ACK_TABLE_SIZE;
        }

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &expected_ack, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(recipient == NULL
                        ? ERR_CODE_NOT_FOUND
                        : ERR_CODE_UNSUPPORTED_CMD); // unknown recipient, or unsuported TXT_TYPE_*
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_TXT_MSG) { // send GroupChannel msg
    int i = 1;
    uint8_t txt_type = cmd_frame[i++]; // should be TXT_TYPE_PLAIN
    uint8_t channel_idx = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    const char *text = (char *)&cmd_frame[i];
    int text_len = len - i;
    cmd_frame[len] = '\0';  // Null-terminate for C string use

    if (txt_type != TXT_TYPE_PLAIN) {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    } else {
      ChannelDetails channel;
      bool success = getChannel(channel_idx, channel);
      if (success && sendGroupMessage(msg_timestamp, channel.channel, _prefs.node_name, text, len - i)) {
        writeOKFrame();
        #ifdef DISPLAY_CLASS
        if (_ui) {
          _ui->addSentChannelMessage(channel_idx, _prefs.node_name, text);
        }
#endif
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
      }
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACTS) { // get Contact list
    if (_iter_started) {
      writeErrFrame(ERR_CODE_BAD_STATE); // iterator is currently busy
    } else {
      if (len >= 5) { // has optional 'since' param
        memcpy(&_iter_filter_since, &cmd_frame[1], 4);
      } else {
        _iter_filter_since = 0;
      }

      uint8_t reply[5];
      reply[0] = RESP_CODE_CONTACTS_START;
      uint32_t count = getNumContacts(); // total, NOT filtered count
      memcpy(&reply[1], &count, 4);
      _serial->writeFrame(reply, 5);

      // start iterator
      _iter = startContactsIterator();
      _iter_started = true;
      _most_recent_lastmod = 0;
    }
  } else if (cmd_frame[0] == CMD_SET_ADVERT_NAME && len >= 2) {
    int nlen = len - 1;
    if (nlen > sizeof(_prefs.node_name) - 1) nlen = sizeof(_prefs.node_name) - 1; // max len
    memcpy(_prefs.node_name, &cmd_frame[1], nlen);
    _prefs.node_name[nlen] = 0; // null terminator
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_ADVERT_LATLON && len >= 9) {
    int32_t lat, lon, alt = 0;
    memcpy(&lat, &cmd_frame[1], 4);
    memcpy(&lon, &cmd_frame[5], 4);
    if (len >= 13) {
      memcpy(&alt, &cmd_frame[9], 4); // for FUTURE support
    }
    if (lat <= 90 * 1E6 && lat >= -90 * 1E6 && lon <= 180 * 1E6 && lon >= -180 * 1E6) {
      sensors.node_lat = ((double)lat) / 1000000.0;
      sensors.node_lon = ((double)lon) / 1000000.0;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid geo coordinate
    }
  } else if (cmd_frame[0] == CMD_GET_DEVICE_TIME) {
    uint8_t reply[5];
    reply[0] = RESP_CODE_CURR_TIME;
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply[1], &now, 4);
    _serial->writeFrame(reply, 5);
  } else if (cmd_frame[0] == CMD_SET_DEVICE_TIME && len >= 5) {
    uint32_t secs;
    memcpy(&secs, &cmd_frame[1], 4);
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (secs >= curr) {
      getRTCClock()->setCurrentTime(secs);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SEND_SELF_ADVERT) {
    mesh::Packet* pkt;
    if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
      pkt = createSelfAdvert(_prefs.node_name);
    } else {
      pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
    }
    if (pkt) {
      if (len >= 2 && cmd_frame[1] == 1) { // optional param (1 = flood, 0 = zero hop)
        unsigned long delay_millis = 0;
        sendFlood(pkt, delay_millis, getPathHashSize());
      } else {
        sendZeroHop(pkt);
      }
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_RESET_PATH && len >= 1 + 32) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      // recipient->lastmod = ??   shouldn't be needed, app already has this version of contact
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // unknown contact
    }
  } else if (cmd_frame[0] == CMD_ADD_UPDATE_CONTACT && len >= 1 + 32 + 2 + 1) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    uint32_t last_mod = getRTCClock()->getCurrentTime();  // fallback value if not present in cmd_frame
    if (recipient) {
      updateContactFromFrame(*recipient, last_mod, cmd_frame, len);
      recipient->lastmod = last_mod;
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      ContactInfo contact;
      updateContactFromFrame(contact, last_mod, cmd_frame, len);
      contact.lastmod = last_mod;
      contact.sync_since = 0;
      if (addContact(contact)) {
        dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_REMOVE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient && removeContact(*recipient)) {
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found, or unable to remove
    }
  } else if (cmd_frame[0] == CMD_SHARE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      if (shareContactZeroHop(*recipient)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // unable to send
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACT_BY_KEY) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *contact = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (contact) {
      writeContactRespFrame(RESP_CODE_CONTACT, *contact);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found
    }
  } else if (cmd_frame[0] == CMD_EXPORT_CONTACT) {
    if (len < 1 + PUB_KEY_SIZE) {
      // export SELF
      mesh::Packet* pkt;
      if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
        pkt = createSelfAdvert(_prefs.node_name);
      } else {
        pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
      }
      if (pkt) {
        pkt->header |= ROUTE_TYPE_FLOOD; // would normally be sent in this mode

        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        uint8_t out_len = pkt->writeTo(&out_frame[1]);
        releasePacket(pkt); // undo the obtainNewPacket()
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // Error
      }
    } else {
      uint8_t *pub_key = &cmd_frame[1];
      ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
      uint8_t out_len;
      if (recipient && (out_len = exportContact(*recipient, &out_frame[1])) > 0) {
        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // not found
      }
    }
  } else if (cmd_frame[0] == CMD_IMPORT_CONTACT && len > 2 + 32 + 64) {
    if (importContact(&cmd_frame[1], len - 1)) {
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SYNC_NEXT_MESSAGE) {
    int out_len;
    if ((out_len = getFromOfflineQueue(out_frame)) > 0) {
      _serial->writeFrame(out_frame, out_len);
#ifdef DISPLAY_CLASS
      if (_ui) {
        _ui->msgRead(offline_queue_len);

        // Mark channel as read when BLE companion app syncs the message.
        // Frame layout V3: [resp_code][snr][res1][res2][channel_idx][path_len]...
        // Frame layout V1: [resp_code][channel_idx][path_len]...
        bool is_v3_ch = (out_frame[0] == RESP_CODE_CHANNEL_MSG_RECV_V3);
        bool is_old_ch = (out_frame[0] == RESP_CODE_CHANNEL_MSG_RECV);
        if (is_v3_ch || is_old_ch) {
          uint8_t ch_idx = is_v3_ch ? out_frame[4] : out_frame[1];
          _ui->markChannelReadFromBLE(ch_idx);
        }

        // Mark DM slot read when companion app syncs a contact (DM/room) message
        bool is_v3_dm = (out_frame[0] == RESP_CODE_CONTACT_MSG_RECV_V3);
        bool is_old_dm = (out_frame[0] == RESP_CODE_CONTACT_MSG_RECV);
        if (is_v3_dm || is_old_dm) {
          _ui->markChannelReadFromBLE(0xFF);
        }
      }
#endif
    } else {
      out_frame[0] = RESP_CODE_NO_MORE_MESSAGES;
      _serial->writeFrame(out_frame, 1);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_PARAMS) {
    int i = 1;
    uint32_t freq;
    memcpy(&freq, &cmd_frame[i], 4);
    i += 4;
    uint32_t bw;
    memcpy(&bw, &cmd_frame[i], 4);
    i += 4;
    uint8_t sf = cmd_frame[i++];
    uint8_t cr = cmd_frame[i++];

    if (freq >= 300000 && freq <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7000 &&
        bw <= 500000) {
      _prefs.sf = sf;
      _prefs.cr = cr;
      _prefs.freq = (float)freq / 1000.0;
      _prefs.bw = (float)bw / 1000.0;
      savePrefs();

      radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
      MESH_DEBUG_PRINTLN("OK: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);

      writeOKFrame();
    } else {
      MESH_DEBUG_PRINTLN("Error: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_TX_POWER) {
    if (cmd_frame[1] > MAX_LORA_TX_POWER) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.tx_power_dbm = cmd_frame[1];
      savePrefs();
      radio_set_tx_power(_prefs.tx_power_dbm);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SET_TUNING_PARAMS) {
    int i = 1;
    uint32_t rx, af;
    memcpy(&rx, &cmd_frame[i], 4);
    i += 4;
    memcpy(&af, &cmd_frame[i], 4);
    i += 4;
    _prefs.rx_delay_base = ((float)rx) / 1000.0f;
    _prefs.airtime_factor = ((float)af) / 1000.0f;
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_TUNING_PARAMS) {
    uint32_t rx = _prefs.rx_delay_base * 1000, af = _prefs.airtime_factor * 1000;
    int i = 0;
    out_frame[i++] = RESP_CODE_TUNING_PARAMS;
    memcpy(&out_frame[i], &rx, 4); i += 4;
    memcpy(&out_frame[i], &af, 4); i += 4;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SET_OTHER_PARAMS) {
    _prefs.manual_add_contacts = cmd_frame[1];
    if (len >= 3) {
      _prefs.telemetry_mode_base = cmd_frame[2] & 0x03; // v5+
      _prefs.telemetry_mode_loc = (cmd_frame[2] >> 2) & 0x03;
      _prefs.telemetry_mode_env = (cmd_frame[2] >> 4) & 0x03;

      if (len >= 4) {
        _prefs.advert_loc_policy = cmd_frame[3];
        if (len >= 5) {
          _prefs.multi_acks = cmd_frame[4];
        }
      }
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_PATH_HASH_MODE && cmd_frame[1] == 0 && len >= 3) {
    if (cmd_frame[2] >= 3) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.path_hash_mode = cmd_frame[2];
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_REBOOT && memcmp(&cmd_frame[1], "reboot", 6) == 0) {
    if (dirty_contacts_expiry) { // is there are pending dirty contacts write needed?
      saveContacts();
    }
    board.reboot();
  } else if (cmd_frame[0] == CMD_GET_BATT_AND_STORAGE) {
    uint8_t reply[11];
    int i = 0;
    reply[i++] = RESP_CODE_BATT_AND_STORAGE;
    uint16_t battery_millivolts = board.getBattMilliVolts();
    uint32_t used = _store->getStorageUsedKb();
    uint32_t total = _store->getStorageTotalKb();
    memcpy(&reply[i], &battery_millivolts, 2); i += 2;
    memcpy(&reply[i], &used, 4); i += 4;
    memcpy(&reply[i], &total, 4); i += 4;
    _serial->writeFrame(reply, i);
  } else if (cmd_frame[0] == CMD_EXPORT_PRIVATE_KEY) {
#if ENABLE_PRIVATE_KEY_EXPORT
    uint8_t reply[65];
    reply[0] = RESP_CODE_PRIVATE_KEY;
    self_id.writeTo(&reply[1], 64);
    _serial->writeFrame(reply, 65);
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_IMPORT_PRIVATE_KEY && len >= 65) {
#if ENABLE_PRIVATE_KEY_IMPORT
    if (!mesh::LocalIdentity::validatePrivateKey(&cmd_frame[1])) {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid key
    } else {
        mesh::LocalIdentity identity;
        identity.readFrom(&cmd_frame[1], 64);
        if (_store->saveMainIdentity(identity)) {
          self_id = identity;
          writeOKFrame();
          // re-load contacts, to invalidate ecdh shared_secrets
          resetContacts();
          _store->loadContacts(this);
        } else {
          writeErrFrame(ERR_CODE_FILE_IO_ERROR);
        }
    }
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_SEND_RAW_DATA && len >= 6) {
    int i = 1;
    uint8_t path_len = cmd_frame[i++];
    if (path_len != OUT_PATH_UNKNOWN && i + mesh::Packet::getPathByteLenFor(path_len) + 4 <= len) { // minimum 4 byte payload
      uint8_t *path = &cmd_frame[i];
      i += mesh::Packet::getPathByteLenFor(path_len);
      auto pkt = createRawData(&cmd_frame[i], len - i);
      if (pkt) {
        sendDirect(pkt, path, path_len);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    } else {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // flood, not supported (yet)
    }
  } else if (cmd_frame[0] == CMD_SEND_LOGIN && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    char *password = (char *)&cmd_frame[1 + PUB_KEY_SIZE];
    cmd_frame[len] = 0; // ensure null terminator in password
    if (recipient) {
      uint32_t est_timeout;
      int result = sendLogin(*recipient, password, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        memcpy(&pending_login, recipient->id.pub_key, 4); // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &pending_login, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_ANON_REQ && len > 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    uint8_t *data = &cmd_frame[1 + PUB_KEY_SIZE];
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendAnonReq(*recipient, data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_STATUS_REQ && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_STATUS, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        // FUTURE:  pending_status = tag;  // match this in onContactResponse()
        memcpy(&pending_status, recipient->id.pub_key, 4); // legacy matching scheme
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_PATH_DISCOVERY_REQ && cmd_frame[1] == 0 && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[2];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      // 'Path Discovery' is just a special case of flood + Telemetry req
      uint8_t req_data[9];
      req_data[0] = REQ_TYPE_GET_TELEMETRY_DATA;
      req_data[1] = ~(TELEM_PERM_BASE);  // NEW: inverse permissions mask (ie. we only want BASE telemetry)
      memset(&req_data[2], 0, 3);  // reserved
      getRNG()->random(&req_data[5], 4);   // random blob to help make packet-hash unique
      auto save = recipient->out_path_len;    // temporarily force sendRequest() to flood
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      int result = sendRequest(*recipient, req_data, sizeof(req_data), tag, est_timeout);
      recipient->out_path_len = save;
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_discovery = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len >= 4 + PUB_KEY_SIZE) {  // can deprecate, in favour of CMD_SEND_BINARY_REQ
    uint8_t *pub_key = &cmd_frame[4];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_telemetry = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len == 4) {  // 'self' telemetry request
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    // query other sensors -- target specific
    sensors.querySensors(0xFF, telemetry);

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], self_id.pub_key, 6);
    i += 6; // pub_key_prefix
    uint8_t tlen = telemetry.getSize();
    memcpy(&out_frame[i], telemetry.getBuffer(), tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_BINARY_REQ && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint8_t *req_data = &cmd_frame[1 + PUB_KEY_SIZE];
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, req_data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_HAS_CONNECTION && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    if (hasConnectionTo(pub_key)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_LOGOUT && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    stopConnection(pub_key);
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_CHANNEL && len >= 2) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    if (getChannel(channel_idx, channel)) {
      int i = 0;
      out_frame[i++] = RESP_CODE_CHANNEL_INFO;
      out_frame[i++] = channel_idx;
      strcpy((char *)&out_frame[i], channel.name);
      i += 32;
      memcpy(&out_frame[i], channel.channel.secret, 16);
      i += 16; // NOTE: only 128-bit supported
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 32) {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // not supported (yet)
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 16) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    StrHelper::strncpy(channel.name, (char *)&cmd_frame[2], 32);
    memset(channel.channel.secret, 0, sizeof(channel.channel.secret));
    memcpy(channel.channel.secret, &cmd_frame[2 + 32], 16); // NOTE: only 128-bit supported
    if (setChannel(channel_idx, channel)) {
      saveChannels();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    }
  } else if (cmd_frame[0] == CMD_SIGN_START) {
    out_frame[0] = RESP_CODE_SIGN_START;
    out_frame[1] = 0; // reserved
    uint32_t len = MAX_SIGN_DATA_LEN;
    memcpy(&out_frame[2], &len, 4);
    _serial->writeFrame(out_frame, 6);

    if (sign_data) {
      free(sign_data);
    }
    sign_data = (uint8_t *)malloc(MAX_SIGN_DATA_LEN);
    sign_data_len = 0;
  } else if (cmd_frame[0] == CMD_SIGN_DATA && len > 1) {
    if (sign_data == NULL || sign_data_len + (len - 1) > MAX_SIGN_DATA_LEN) {
      writeErrFrame(sign_data == NULL ? ERR_CODE_BAD_STATE : ERR_CODE_TABLE_FULL); // error: too long
    } else {
      memcpy(&sign_data[sign_data_len], &cmd_frame[1], len - 1);
      sign_data_len += (len - 1);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SIGN_FINISH) {
    if (sign_data) {
      self_id.sign(&out_frame[1], sign_data, sign_data_len);

      free(sign_data); // don't need sign_data now
      sign_data = NULL;

      out_frame[0] = RESP_CODE_SIGNATURE;
      _serial->writeFrame(out_frame, 1 + SIGNATURE_SIZE);
    } else {
      writeErrFrame(ERR_CODE_BAD_STATE);
    }
  } else if (cmd_frame[0] == CMD_SEND_TRACE_PATH && len > 10 && len - 10 < MAX_PACKET_PAYLOAD-5) {
    uint8_t path_len = len - 10;
    uint8_t flags = cmd_frame[9];
    uint8_t path_sz = flags & 0x03;  // NEW v1.11+ 
    if ((path_len >> path_sz) > MAX_PATH_SIZE || (path_len % (1 << path_sz)) != 0) { // make sure is multiple of path_sz
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      uint32_t tag, auth;
      memcpy(&tag, &cmd_frame[1], 4);
      memcpy(&auth, &cmd_frame[5], 4);
      auto pkt = createTrace(tag, auth, flags);
      if (pkt) {
        sendDirect(pkt, &cmd_frame[10], path_len);

        uint32_t t = _radio->getEstAirtimeFor(pkt->payload_len + pkt->path_len + 2);
        uint32_t est_timeout = calcDirectTimeoutMillisFor(t, path_len);

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_SET_DEVICE_PIN && len >= 5) {

    // get pin from command frame
    uint32_t pin;
    memcpy(&pin, &cmd_frame[1], 4);

    // ensure pin is zero, or a valid 6 digit pin
    if (pin == 0 || (pin >= 100000 && pin <= 999999)) {
      _prefs.ble_pin = pin;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_CUSTOM_VARS) {
    out_frame[0] = RESP_CODE_CUSTOM_VARS;
    char *dp = (char *)&out_frame[1];
    for (int i = 0; i < sensors.getNumSettings() && dp - (char *)&out_frame[1] < 140; i++) {
      if (i > 0) {
        *dp++ = ',';
      }
      strcpy(dp, sensors.getSettingName(i));
      dp = strchr(dp, 0);
      *dp++ = ':';
      strcpy(dp, sensors.getSettingValue(i));
      dp = strchr(dp, 0);
    }
    _serial->writeFrame(out_frame, dp - (char *)out_frame);
  } else if (cmd_frame[0] == CMD_SET_CUSTOM_VAR && len >= 4) {
    cmd_frame[len] = 0;
    char *sp = (char *)&cmd_frame[1];
    char *np = strchr(sp, ':'); // look for separator char
    if (np) {
      *np++ = 0; // modify 'cmd_frame', replace ':' with null
      bool success = sensors.setSettingValue(sp, np);
      if (success) {
        #if ENV_INCLUDE_GPS == 1
        // Update node preferences for GPS settings
        if (strcmp(sp, "gps") == 0) {
          _prefs.gps_enabled = (np[0] == '1') ? 1 : 0;
          savePrefs();
        } else if (strcmp(sp, "gps_interval") == 0) {
          uint32_t interval_seconds = atoi(np);
          _prefs.gps_interval = constrain(interval_seconds, 0, 86400);
          savePrefs();
        }
        #endif
        // UTC offset for local clock display (works regardless of GPS)
        if (strcmp(sp, "utc_offset") == 0) {
          int offset = atoi(np);
          _prefs.utc_offset_hours = constrain(offset, -12, 14);
          savePrefs();
        }
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_ADVERT_PATH && len >= PUB_KEY_SIZE+2) {
    // FUTURE use:  uint8_t reserved = cmd_frame[1];
    uint8_t *pub_key = &cmd_frame[2];
    AdvertPath* found = NULL;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
      auto p = &advert_paths[i];
      if (memcmp(p->pubkey_prefix, pub_key, sizeof(p->pubkey_prefix)) == 0) {
        found = p;
        break;
      }
    }
    if (found) {
      int i = 0;
      out_frame[i++] = RESP_CODE_ADVERT_PATH;
      memcpy(&out_frame[i], &found->recv_timestamp, 4); i += 4;
      out_frame[i++] = found->path_len;
      i += mesh::Packet::writePath(&out_frame[i], found->path, found->path_len);
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_STATS && len >= 2) {
    uint8_t stats_type = cmd_frame[1];
    if (stats_type == STATS_TYPE_CORE) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_CORE;
      uint16_t battery_mv = board.getBattMilliVolts();
      uint32_t uptime_secs = _ms->getMillis() / 1000;
      uint8_t queue_len = (uint8_t)_mgr->getOutboundCount(0xFFFFFFFF);
      memcpy(&out_frame[i], &battery_mv, 2); i += 2;
      memcpy(&out_frame[i], &uptime_secs, 4); i += 4;
      memcpy(&out_frame[i], &_err_flags, 2); i += 2;
      out_frame[i++] = queue_len;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_RADIO) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_RADIO;
      int16_t noise_floor = (int16_t)_radio->getNoiseFloor();
      int8_t last_rssi = (int8_t)radio_driver.getLastRSSI();
      int8_t last_snr = (int8_t)(radio_driver.getLastSNR() * 4); // scaled by 4 for 0.25 dB precision
      uint32_t tx_air_secs = getTotalAirTime() / 1000;
      uint32_t rx_air_secs = getReceiveAirTime() / 1000;
      memcpy(&out_frame[i], &noise_floor, 2); i += 2;
      out_frame[i++] = last_rssi;
      out_frame[i++] = last_snr;
      memcpy(&out_frame[i], &tx_air_secs, 4); i += 4;
      memcpy(&out_frame[i], &rx_air_secs, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_PACKETS) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_PACKETS;
      uint32_t recv = radio_driver.getPacketsRecv();
      uint32_t sent = radio_driver.getPacketsSent();
      uint32_t n_sent_flood = getNumSentFlood();
      uint32_t n_sent_direct = getNumSentDirect();
      uint32_t n_recv_flood = getNumRecvFlood();
      uint32_t n_recv_direct = getNumRecvDirect();
      memcpy(&out_frame[i], &recv, 4); i += 4;
      memcpy(&out_frame[i], &sent, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_direct, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid stats sub-type
    }
  } else if (cmd_frame[0] == CMD_FACTORY_RESET && memcmp(&cmd_frame[1], "reset", 5) == 0) {
    if (_serial) {
      MESH_DEBUG_PRINTLN("Factory reset: disabling serial interface to prevent reconnects (BLE/WiFi)");
      _serial->disable(); // Phone app disconnects before we can send OK frame so it's safe here
    }
    bool success = _store->formatFileSystem();
    if (success) {
      writeOKFrame();
      delay(1000);
      board.reboot();  // doesn't return
    } else {
      writeErrFrame(ERR_CODE_FILE_IO_ERROR);
    }
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE && len >= 2 && cmd_frame[1] == 0) {
    if (len >= 2 + 16) {
      memcpy(send_scope.key, &cmd_frame[2], sizeof(send_scope.key));  // set curr scope TransportKey
    } else {
      memset(send_scope.key, 0, sizeof(send_scope.key));  // set scope to null
    }
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SEND_CONTROL_DATA && len >= 2 && (cmd_frame[1] & 0x80) != 0) {
    auto resp = createControlData(&cmd_frame[1], len - 1);
    if (resp) {
      sendZeroHop(resp);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_SET_AUTOADD_CONFIG) {
    _prefs.autoadd_config = cmd_frame[1];
    savePrefs();
    writeOKFrame();  
  } else if (cmd_frame[0] == CMD_GET_AUTOADD_CONFIG) {
    int i = 0;
    out_frame[i++] = RESP_CODE_AUTOADD_CONFIG;
    out_frame[i++] = _prefs.autoadd_config;
    _serial->writeFrame(out_frame, i);
  } else {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    MESH_DEBUG_PRINTLN("ERROR: unknown command: %02X", cmd_frame[0]);
  }
}

void MyMesh::enterCLIRescue() {
  _cli_rescue = true;
  cli_command[0] = 0;
  Serial.println("========= CLI Rescue =========");
}

void MyMesh::checkCLIRescueCmd() {
  int len = strlen(cli_command);
  bool line_complete = false;
  while (Serial.available() && len < sizeof(cli_command)-1) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (len > 0) {
        line_complete = true;
        Serial.println();  // echo newline
      }
      break;  // stop reading — remaining LF (from CR+LF) is consumed next loop
    }
    cli_command[len++] = c;
    cli_command[len] = 0;
    Serial.print(c);  // echo
  }
  if (len == sizeof(cli_command)-1) {  // buffer full — force processing
    line_complete = true;
  }

  if (line_complete && len > 0) {
    cli_command[len] = 0;  // ensure null terminated

    // =====================================================================
    // GET commands — read settings
    // =====================================================================
    if (memcmp(cli_command, "get ", 4) == 0) {
      const char* key = &cli_command[4];

      if (strcmp(key, "name") == 0) {
        Serial.printf("  > %s\n", _prefs.node_name);
      } else if (strcmp(key, "freq") == 0) {
        Serial.printf("  > %.3f\n", _prefs.freq);
      } else if (strcmp(key, "bw") == 0) {
        Serial.printf("  > %.1f\n", _prefs.bw);
      } else if (strcmp(key, "sf") == 0) {
        Serial.printf("  > %d\n", _prefs.sf);
      } else if (strcmp(key, "cr") == 0) {
        Serial.printf("  > %d\n", _prefs.cr);
      } else if (strcmp(key, "tx") == 0) {
        Serial.printf("  > %d\n", _prefs.tx_power_dbm);
      } else if (strcmp(key, "utc") == 0) {
        Serial.printf("  > %d\n", _prefs.utc_offset_hours);
      } else if (strcmp(key, "notify") == 0) {
        Serial.printf("  > %s\n", _prefs.kb_flash_notify ? "on" : "off");
      } else if (strcmp(key, "path.hash.mode") == 0) {
        Serial.printf("  > %d (%d-byte path hashes)\n", _prefs.path_hash_mode, _prefs.path_hash_mode + 1);
      } else if (strcmp(key, "gps") == 0) {
        Serial.printf("  > %s (interval: %ds)\n",
            _prefs.gps_enabled ? "on" : "off", _prefs.gps_interval);
      } else if (strcmp(key, "pin") == 0) {
        Serial.printf("  > %06d\n", _prefs.ble_pin);

      // --- Mesh tuning parameters ---
      } else if (strcmp(key, "rxdelay") == 0) {
        Serial.printf("  > %.1f\n", _prefs.rx_delay_base);
      } else if (strcmp(key, "af") == 0) {
        Serial.printf("  > %.1f\n", _prefs.airtime_factor);
      } else if (strcmp(key, "multi.acks") == 0) {
        Serial.printf("  > %d\n", _prefs.multi_acks);
      } else if (strcmp(key, "int.thresh") == 0) {
        Serial.printf("  > %d\n", _prefs.interference_threshold);
      } else if (strcmp(key, "tx.fail.threshold") == 0) {
        Serial.printf("  > %d\n", _prefs.tx_fail_reset_threshold);
      } else if (strcmp(key, "rx.fail.threshold") == 0) {
        Serial.printf("  > %d\n", _prefs.rx_fail_reboot_threshold);
      } else if (strcmp(key, "gps.baud") == 0) {
        uint32_t effective = _prefs.gps_baudrate ? _prefs.gps_baudrate : GPS_BAUDRATE;
        Serial.printf("  > %lu (effective: %lu)\n",
            (unsigned long)_prefs.gps_baudrate, (unsigned long)effective);

      } else if (strcmp(key, "radio") == 0) {
        Serial.printf("  > freq=%.3f bw=%.1f sf=%d cr=%d tx=%d\n",
            _prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr, _prefs.tx_power_dbm);
      } else if (strcmp(key, "pubkey") == 0) {
        char hex[PUB_KEY_SIZE * 2 + 1];
        mesh::Utils::toHex(hex, self_id.pub_key, PUB_KEY_SIZE);
        Serial.printf("  > %s\n", hex);
      } else if (strcmp(key, "firmware") == 0) {
        Serial.printf("  > %s\n", FIRMWARE_VERSION);
      } else if (strcmp(key, "channels") == 0) {
        bool found = false;
        for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
          ChannelDetails ch;
          if (getChannel(i, ch) && ch.name[0] != '\0') {
            Serial.printf("  [%d] %s\n", i, ch.name);
            found = true;
          } else {
            break;
          }
        }
        if (!found) Serial.println("  (no channels)");
      } else if (strcmp(key, "presets") == 0) {
        Serial.println("  Available radio presets:");
        for (int i = 0; i < (int)NUM_RADIO_PRESETS; i++) {
          Serial.printf("    %2d  %-30s  %.3f MHz  BW%.1f  SF%d  CR%d  TX%d\n",
              i, RADIO_PRESETS[i].name, RADIO_PRESETS[i].freq,
              RADIO_PRESETS[i].bw, RADIO_PRESETS[i].sf,
              RADIO_PRESETS[i].cr, RADIO_PRESETS[i].tx_power);
        }
#ifdef HAS_4G_MODEM
      } else if (strcmp(key, "modem") == 0) {
        Serial.printf("  > %s\n", ModemManager::loadEnabledConfig() ? "on" : "off");
      } else if (strcmp(key, "apn") == 0) {
        Serial.printf("  > %s\n", modemManager.getAPN());
      } else if (strcmp(key, "imei") == 0) {
        Serial.printf("  > %s\n", modemManager.getIMEI());
#endif
      } else if (strcmp(key, "all") == 0) {
        Serial.println("  === Meck Device Settings ===");
        Serial.printf("  name:       %s\n", _prefs.node_name);
        Serial.printf("  freq:       %.3f\n", _prefs.freq);
        Serial.printf("  bw:         %.1f\n", _prefs.bw);
        Serial.printf("  sf:         %d\n", _prefs.sf);
        Serial.printf("  cr:         %d\n", _prefs.cr);
        Serial.printf("  tx:         %d\n", _prefs.tx_power_dbm);
        Serial.printf("  utc:        %d\n", _prefs.utc_offset_hours);
        Serial.printf("  notify:     %s\n", _prefs.kb_flash_notify ? "on" : "off");
        Serial.printf("  path.hash:  %d (%d-byte)\n", _prefs.path_hash_mode, _prefs.path_hash_mode + 1);
        Serial.printf("  gps:        %s (interval: %ds)\n",
            _prefs.gps_enabled ? "on" : "off", _prefs.gps_interval);
        Serial.printf("  pin:        %06d\n", _prefs.ble_pin);
        Serial.printf("  rxdelay:    %.1f\n", _prefs.rx_delay_base);
        Serial.printf("  af:         %.1f\n", _prefs.airtime_factor);
        Serial.printf("  multi.acks: %d\n", _prefs.multi_acks);
        Serial.printf("  int.thresh: %d\n", _prefs.interference_threshold);
        Serial.printf("  tx.fail:    %d\n", _prefs.tx_fail_reset_threshold);
        Serial.printf("  rx.fail:    %d\n", _prefs.rx_fail_reboot_threshold);
        {
          uint32_t eff_baud = _prefs.gps_baudrate ? _prefs.gps_baudrate : GPS_BAUDRATE;
          Serial.printf("  gps.baud:   %lu\n", (unsigned long)eff_baud);
        }
#ifdef HAS_4G_MODEM
        Serial.printf("  modem:      %s\n", ModemManager::loadEnabledConfig() ? "on" : "off");
        Serial.printf("  apn:        %s\n", modemManager.getAPN());
        Serial.printf("  imei:       %s\n", modemManager.getIMEI());
#endif
        // Detect current preset
        bool presetFound = false;
        for (int i = 0; i < (int)NUM_RADIO_PRESETS; i++) {
          if (_prefs.freq == RADIO_PRESETS[i].freq && _prefs.bw == RADIO_PRESETS[i].bw &&
              _prefs.sf == RADIO_PRESETS[i].sf && _prefs.cr == RADIO_PRESETS[i].cr) {
            Serial.printf("  preset:     %s\n", RADIO_PRESETS[i].name);
            presetFound = true;
            break;
          }
        }
        if (!presetFound) Serial.println("  preset:     (custom)");
        Serial.printf("  firmware:   %s\n", FIRMWARE_VERSION);
        char hex[PUB_KEY_SIZE * 2 + 1];
        mesh::Utils::toHex(hex, self_id.pub_key, PUB_KEY_SIZE);
        Serial.printf("  pubkey:     %s\n", hex);
        {
          uint32_t clk = getRTCClock()->getCurrentTime();
          if (clk > 1704067200UL) {
            Serial.printf("  clock:      %lu (valid)\n", (unsigned long)clk);
          } else {
            Serial.printf("  clock:      not set\n");
          }
        }
        // List channels
        Serial.println("  channels:");
        bool chFound = false;
        for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
          ChannelDetails ch;
          if (getChannel(i, ch) && ch.name[0] != '\0') {
            Serial.printf("    [%d] %s\n", i, ch.name);
            chFound = true;
          } else {
            break;
          }
        }
        if (!chFound) Serial.println("    (none)");
      } else {
        Serial.printf("  Error: unknown key '%s' (try 'help')\n", key);
      }

    // =====================================================================
    // SET commands — write settings
    // =====================================================================
    } else if (memcmp(cli_command, "set ", 4) == 0) {
      const char* config = &cli_command[4];

      if (memcmp(config, "name ", 5) == 0) {
        const char* val = &config[5];
        // Validate name (same rules as CommonCLI)
        bool valid = true;
        const char* p = val;
        while (*p) {
          if (*p == '[' || *p == ']' || *p == '/' || *p == '\\' ||
              *p == ':' || *p == ',' || *p == '?' || *p == '*') {
            valid = false;
            break;
          }
          p++;
        }
        if (valid && strlen(val) > 0) {
          strncpy(_prefs.node_name, val, sizeof(_prefs.node_name) - 1);
          _prefs.node_name[sizeof(_prefs.node_name) - 1] = '\0';
          savePrefs();
          Serial.printf("  > name = %s\n", _prefs.node_name);
        } else {
          Serial.println("  Error: invalid name (no []/:,?* chars)");
        }

      } else if (memcmp(config, "freq ", 5) == 0) {
        float f = atof(&config[5]);
        if (f >= 400.0f && f <= 928.0f) {
          _prefs.freq = f;
          savePrefs();
          radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
          Serial.printf("  > freq = %.3f (applied)\n", _prefs.freq);
        } else {
          Serial.println("  Error: freq out of range (400-928)");
        }

      } else if (memcmp(config, "bw ", 3) == 0) {
        float bw = atof(&config[3]);
        if (bw >= 7.8f && bw <= 500.0f) {
          _prefs.bw = bw;
          savePrefs();
          radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
          Serial.printf("  > bw = %.1f (applied)\n", _prefs.bw);
        } else {
          Serial.println("  Error: bw out of range (7.8-500)");
        }

      } else if (memcmp(config, "sf ", 3) == 0) {
        int sf = atoi(&config[3]);
        if (sf >= 5 && sf <= 12) {
          _prefs.sf = (uint8_t)sf;
          savePrefs();
          radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
          Serial.printf("  > sf = %d (applied)\n", _prefs.sf);
        } else {
          Serial.println("  Error: sf out of range (5-12)");
        }

      } else if (memcmp(config, "cr ", 3) == 0) {
        int cr = atoi(&config[3]);
        if (cr >= 5 && cr <= 8) {
          _prefs.cr = (uint8_t)cr;
          savePrefs();
          radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
          Serial.printf("  > cr = %d (applied)\n", _prefs.cr);
        } else {
          Serial.println("  Error: cr out of range (5-8)");
        }

      } else if (memcmp(config, "tx ", 3) == 0) {
        int tx = atoi(&config[3]);
        if (tx >= 1 && tx <= MAX_LORA_TX_POWER) {
          _prefs.tx_power_dbm = (uint8_t)tx;
          savePrefs();
          radio_set_tx_power(_prefs.tx_power_dbm);
          Serial.printf("  > tx = %d (applied)\n", _prefs.tx_power_dbm);
        } else {
          Serial.printf("  Error: tx out of range (1-%d)\n", MAX_LORA_TX_POWER);
        }

      } else if (memcmp(config, "utc ", 4) == 0) {
        int utc = atoi(&config[4]);
        if (utc >= -12 && utc <= 14) {
          _prefs.utc_offset_hours = (int8_t)utc;
          savePrefs();
          Serial.printf("  > utc = %d\n", _prefs.utc_offset_hours);
        } else {
          Serial.println("  Error: utc out of range (-12 to 14)");
        }

      } else if (memcmp(config, "notify ", 7) == 0) {
        if (strcmp(&config[7], "on") == 0) {
          _prefs.kb_flash_notify = 1;
        } else if (strcmp(&config[7], "off") == 0) {
          _prefs.kb_flash_notify = 0;
        } else {
          Serial.println("  Error: use 'on' or 'off'");
          cli_command[0] = 0;
          return;
        }
        savePrefs();
        Serial.printf("  > notify = %s\n", _prefs.kb_flash_notify ? "on" : "off");

      } else if (memcmp(config, "path.hash.mode ", 15) == 0) {
        int mode = atoi(&config[15]);
        if (mode >= 0 && mode <= 2) {
          _prefs.path_hash_mode = (uint8_t)mode;
          savePrefs();
          Serial.printf("  > path.hash.mode = %d (%d-byte path hashes)\n", mode, mode + 1);
        } else {
          Serial.println("  Error: mode must be 0, 1, or 2 (1-byte, 2-byte, 3-byte)");
        }

      } else if (memcmp(config, "pin ", 4) == 0) {
        _prefs.ble_pin = atoi(&config[4]);
        savePrefs();
        Serial.printf("  > pin is now %06d\n", _prefs.ble_pin);

      } else if (memcmp(config, "radio ", 6) == 0) {
        // Composite: "set radio <freq> <bw> <sf> <cr>"
        char tmp[64];
        strncpy(tmp, &config[6], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        const char* parts[4];
        int num = mesh::Utils::parseTextParts(tmp, parts, 4);
        if (num == 4) {
          float freq = strtof(parts[0], nullptr);
          float bw   = strtof(parts[1], nullptr);
          int sf     = atoi(parts[2]);
          int cr     = atoi(parts[3]);
          if (freq >= 400.0f && freq <= 928.0f && bw >= 7.8f && bw <= 500.0f
              && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8) {
            _prefs.freq = freq;
            _prefs.bw = bw;
            _prefs.sf = (uint8_t)sf;
            _prefs.cr = (uint8_t)cr;
            savePrefs();
            radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
            radio_set_tx_power(_prefs.tx_power_dbm);
            Serial.printf("  > radio = %.3f/%.1f/SF%d/CR%d TX:%d (applied)\n",
                _prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr, _prefs.tx_power_dbm);
          } else {
            Serial.println("  Error: invalid radio params");
          }
        } else {
          Serial.println("  Usage: set radio <freq> <bw> <sf> <cr>");
        }

      } else if (memcmp(config, "preset ", 7) == 0) {
        const char* name = &config[7];
        // Try exact match first (case-insensitive)
        bool found = false;
        for (int i = 0; i < (int)NUM_RADIO_PRESETS; i++) {
          if (strcasecmp(RADIO_PRESETS[i].name, name) == 0) {
            _prefs.freq = RADIO_PRESETS[i].freq;
            _prefs.bw = RADIO_PRESETS[i].bw;
            _prefs.sf = RADIO_PRESETS[i].sf;
            _prefs.cr = RADIO_PRESETS[i].cr;
            _prefs.tx_power_dbm = RADIO_PRESETS[i].tx_power;
            savePrefs();
            radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
            radio_set_tx_power(_prefs.tx_power_dbm);
            Serial.printf("  > Applied preset '%s' (%.3f/%.1f/SF%d/CR%d TX:%d)\n",
                RADIO_PRESETS[i].name, _prefs.freq, _prefs.bw,
                _prefs.sf, _prefs.cr, _prefs.tx_power_dbm);
            found = true;
            break;
          }
        }
        // Try by index number if name didn't match
        if (!found) {
          char* endp;
          long idx = strtol(name, &endp, 10);
          if (endp != name && *endp == '\0' && idx >= 0 && idx < (int)NUM_RADIO_PRESETS) {
            _prefs.freq = RADIO_PRESETS[idx].freq;
            _prefs.bw = RADIO_PRESETS[idx].bw;
            _prefs.sf = RADIO_PRESETS[idx].sf;
            _prefs.cr = RADIO_PRESETS[idx].cr;
            _prefs.tx_power_dbm = RADIO_PRESETS[idx].tx_power;
            savePrefs();
            radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
            radio_set_tx_power(_prefs.tx_power_dbm);
            Serial.printf("  > Applied preset '%s' (%.3f/%.1f/SF%d/CR%d TX:%d)\n",
                RADIO_PRESETS[idx].name, _prefs.freq, _prefs.bw,
                _prefs.sf, _prefs.cr, _prefs.tx_power_dbm);
            found = true;
          }
        }
        if (!found) {
          Serial.printf("  Error: unknown preset '%s' (try 'get presets')\n", name);
        }

      } else if (memcmp(config, "channel.add ", 12) == 0) {
        const char* name = &config[12];
        if (strlen(name) == 0) {
          Serial.println("  Error: channel name required");
          cli_command[0] = 0;
          return;
        }
        // Build channel name with # prefix if not present
        char chanName[32];
        if (name[0] == '#') {
          strncpy(chanName, name, sizeof(chanName));
        } else {
          chanName[0] = '#';
          strncpy(&chanName[1], name, sizeof(chanName) - 1);
        }
        chanName[31] = '\0';

        // Generate 128-bit PSK from SHA-256 of channel name
        ChannelDetails newCh;
        memset(&newCh, 0, sizeof(newCh));
        strncpy(newCh.name, chanName, sizeof(newCh.name));
        newCh.name[31] = '\0';

        uint8_t hash[32];
        mesh::Utils::sha256(hash, 32, (const uint8_t*)chanName, strlen(chanName));
        memcpy(newCh.channel.secret, hash, 16);

        // Find next empty slot
        bool added = false;
        for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
          ChannelDetails existing;
          if (!getChannel(i, existing) || existing.name[0] == '\0') {
            if (setChannel(i, newCh)) {
              saveChannels();
              Serial.printf("  > Added channel '%s' at slot %d\n", chanName, i);
              added = true;
            }
            break;
          }
        }
        if (!added) Serial.println("  Error: no empty channel slots");

      } else if (memcmp(config, "channel.del ", 12) == 0) {
        int idx = atoi(&config[12]);
        if (idx <= 0) {
          Serial.println("  Error: cannot delete channel 0 (public)");
        } else if (idx >= MAX_GROUP_CHANNELS) {
          Serial.printf("  Error: index out of range (1-%d)\n", MAX_GROUP_CHANNELS - 1);
        } else {
          // Verify channel exists
          ChannelDetails ch;
          if (!getChannel(idx, ch) || ch.name[0] == '\0') {
            Serial.printf("  Error: no channel at index %d\n", idx);
          } else {
            // Compact: shift channels down
            int total = 0;
            for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
              ChannelDetails tmp;
              if (getChannel(i, tmp) && tmp.name[0] != '\0') {
                total = i + 1;
              } else {
                break;
              }
            }
            for (int i = idx; i < total - 1; i++) {
              ChannelDetails next;
              if (getChannel(i + 1, next)) {
                setChannel(i, next);
              }
            }
            ChannelDetails empty;
            memset(&empty, 0, sizeof(empty));
            setChannel(total - 1, empty);
            saveChannels();
            Serial.printf("  > Deleted channel %d ('%s'), compacted %d channels\n",
                idx, ch.name, total);
          }
        }

#ifdef HAS_4G_MODEM
      } else if (memcmp(config, "apn ", 4) == 0) {
        const char* apn = &config[4];
        if (strlen(apn) > 0) {
          modemManager.setAPN(apn);
          Serial.printf("  > apn = %s\n", apn);
        } else {
          ModemManager::saveAPNConfig("");
          Serial.println("  > apn cleared (will auto-detect on next boot)");
        }

      } else if (strcmp(config, "modem on") == 0) {
        ModemManager::saveEnabledConfig(true);
        modemManager.begin();
        Serial.println("  > modem enabled");

      } else if (strcmp(config, "modem off") == 0) {
        ModemManager::saveEnabledConfig(false);
        modemManager.shutdown();
        Serial.println("  > modem disabled");
#endif

      // --- Mesh tuning parameters ---
      } else if (memcmp(config, "rxdelay ", 8) == 0) {
        float val = atof(&config[8]);
        if (val >= 0.0f && val <= 20.0f) {
          _prefs.rx_delay_base = val;
          savePrefs();
          Serial.printf("  > rxdelay = %.1f\n", _prefs.rx_delay_base);
        } else {
          Serial.println("  Error: rxdelay out of range (0-20)");
        }

      } else if (memcmp(config, "af ", 3) == 0) {
        float val = atof(&config[3]);
        if (val >= 0.0f && val <= 9.0f) {
          _prefs.airtime_factor = val;
          savePrefs();
          Serial.printf("  > af = %.1f\n", _prefs.airtime_factor);
        } else {
          Serial.println("  Error: af out of range (0-9)");
        }

      } else if (memcmp(config, "multi.acks ", 11) == 0) {
        int val = atoi(&config[11]);
        if (val == 0 || val == 1) {
          _prefs.multi_acks = (uint8_t)val;
          savePrefs();
          Serial.printf("  > multi.acks = %d\n", _prefs.multi_acks);
        } else {
          Serial.println("  Error: use 0 or 1");
        }

      // Interference threshold — not recommended unless the device is in a high
      // RF interference environment (low noise floor with significant fluctuations).
      // Enabling adds ~4s receive delay per packet for channel activity scanning.
      } else if (memcmp(config, "int.thresh ", 11) == 0) {
        int val = atoi(&config[11]);
        if (val == 0) {
          _prefs.interference_threshold = 0;
          savePrefs();
          Serial.println("  > int.thresh = 0 (disabled)");
        } else if (val >= 14 && val <= 255) {
          _prefs.interference_threshold = (uint8_t)val;
          savePrefs();
          Serial.printf("  > int.thresh = %d (enabled — adds ~4s rx delay)\n",
              _prefs.interference_threshold);
          Serial.println("  Note: only recommended for high RF interference environments");
        } else {
          Serial.println("  Error: use 0 (disabled) or 14+ (typical: 14)");
        }

      } else if (memcmp(config, "tx.fail.threshold ", 18) == 0) {
        int val = atoi(&config[18]);
        if (val < 0) val = 0;
        if (val > 10) val = 10;
        _prefs.tx_fail_reset_threshold = (uint8_t)val;
        savePrefs();
        if (val == 0) {
          Serial.println("  > tx fail reset disabled");
        } else {
          Serial.printf("  > tx fail reset after %d failures\n", val);
        }

      } else if (memcmp(config, "rx.fail.threshold ", 18) == 0) {
        int val = atoi(&config[18]);
        if (val < 0) val = 0;
        if (val > 10) val = 10;
        _prefs.rx_fail_reboot_threshold = (uint8_t)val;
        savePrefs();
        if (val == 0) {
          Serial.println("  > rx fail reboot disabled");
        } else {
          Serial.printf("  > reboot after %d rx recovery failures\n", val);
        }

      } else if (memcmp(config, "gps.baud ", 9) == 0) {
        uint32_t val = (uint32_t)atol(&config[9]);
        if (val == 0 || val == 4800 || val == 9600 || val == 19200 ||
            val == 38400 || val == 57600 || val == 115200) {
          _prefs.gps_baudrate = val;
          savePrefs();
          uint32_t effective = val ? val : GPS_BAUDRATE;
          Serial.printf("  > gps.baud = %lu (effective: %lu, reboot to apply)\n",
              (unsigned long)val, (unsigned long)effective);
        } else {
          Serial.println("  Error: use 0 (default), 4800, 9600, 19200, 38400, 57600, or 115200");
        }

      // Backlight control (T5S3 E-Paper Pro only)
      } else if (memcmp(config, "backlight ", 10) == 0) {
#if defined(LilyGo_T5S3_EPaper_Pro)
        const char* val = &config[10];
        if (strcmp(val, "on") == 0) {
          board.setBacklight(true);
          Serial.println("  > backlight ON");
        } else if (strcmp(val, "off") == 0) {
          board.setBacklight(false);
          Serial.println("  > backlight OFF");
        } else {
          int brightness = atoi(val);
          if (brightness >= 0 && brightness <= 255) {
            board.setBacklightBrightness((uint8_t)brightness);
            board.setBacklight(brightness > 0);
            Serial.printf("  > backlight brightness = %d\n", brightness);
          } else {
            Serial.println("  Error: use 'on', 'off', or 0-255");
          }
        }
#else
        Serial.println("  Error: backlight not available on this device");
#endif

      } else {
        Serial.printf("  Error: unknown setting '%s' (try 'help')\n", config);
      }

    // =====================================================================
    // CLOCK commands (standalone — matches repeater admin convention)
    // =====================================================================
    } else if (memcmp(cli_command, "clock sync ", 11) == 0) {
      uint32_t epoch = (uint32_t)strtoul(&cli_command[11], nullptr, 10);
      if (epoch > 1704067200UL && epoch < 2082758400UL) {
        getRTCClock()->setCurrentTime(epoch);
        Serial.printf("  > clock synced to %lu\n", (unsigned long)epoch);
      } else {
        Serial.println("  Error: invalid epoch (must be 2024-2036 range)");
        Serial.println("  Hint: on macOS/Linux run: date +%s");
      }
    } else if (strcmp(cli_command, "clock sync") == 0) {
      // Bare "clock sync" without a value — show usage
      Serial.println("  Usage: clock sync <unix_epoch>");
      Serial.println("  Hint:  clock sync $(date +%s)");
    } else if (strcmp(cli_command, "clock") == 0) {
      uint32_t t = getRTCClock()->getCurrentTime();
      if (t > 1704067200UL) {
        // Break epoch into human-readable UTC
        uint32_t ep = t;
        int s = ep % 60; ep /= 60;
        int mi = ep % 60; ep /= 60;
        int h  = ep % 24; ep /= 24;
        int yr = 1970;
        while (true) { int d = ((yr%4==0&&yr%100!=0)||yr%400==0)?366:365; if(ep<(uint32_t)d) break; ep-=d; yr++; }
        int mo = 1;
        while (true) {
          static const uint8_t dm[]={31,28,31,30,31,30,31,31,30,31,30,31};
          int d = (mo==2&&((yr%4==0&&yr%100!=0)||yr%400==0))?29:dm[mo-1];
          if(ep<(uint32_t)d) break; ep-=d; mo++;
        }
        int dy = ep + 1;
        Serial.printf("  > %04d-%02d-%02d %02d:%02d:%02d UTC (epoch: %lu)\n",
                      yr, mo, dy, h, mi, s, (unsigned long)t);
      } else {
        Serial.printf("  > not set (epoch: %lu)\n", (unsigned long)t);
      }

    // =====================================================================
    // HELP command
    // =====================================================================
    } else if (strcmp(cli_command, "help") == 0) {
      Serial.println("=== Meck Serial CLI ===");
      Serial.println("  get <key>                Read a setting");
      Serial.println("  set <key> <value>        Write a setting");
      Serial.println("");
      Serial.println("  Settings keys:");
      Serial.println("    name, freq, bw, sf, cr, tx, utc, notify, pin");
      Serial.println("    path.hash.mode         Path hash size (0=1B, 1=2B, 2=3B)");
      Serial.println("");
      Serial.println("  Mesh tuning:");
      Serial.println("    rxdelay <0-20>         Rx delay base (0=disabled)");
      Serial.println("    af <0-9>               Airtime factor");
      Serial.println("    multi.acks <0|1>       Redundant ACKs (default: 1)");
      Serial.println("    int.thresh <0|14+>     Interference threshold dB (0=off, 14=typical)");
      Serial.println("    tx.fail.threshold <0-10>  TX fail radio reset (0=off, default 3)");
      Serial.println("    rx.fail.threshold <0-10>  RX stuck reboot (0=off, default 3)");
      Serial.println("    gps.baud <rate>        GPS baud (0=default, reboot to apply)");
      Serial.println("");
      Serial.println("  Clock:");
      Serial.println("    clock                  Show current RTC time (UTC)");
      Serial.println("    clock sync <epoch>     Set RTC from Unix timestamp");
      Serial.println("    Hint: clock sync $(date +%s)");
      Serial.println("");
      Serial.println("  Compound commands:");
      Serial.println("    get all                Dump all settings");
      Serial.println("    get radio              Show all radio params");
      Serial.println("    get channels           List channels");
      Serial.println("    get presets            List radio presets");
      Serial.println("    get pubkey             Show public key");
      Serial.println("    get firmware           Show firmware version");
      Serial.println("    set radio <f> <bw> <sf> <cr>   Set all radio params");
      Serial.println("    set preset <name|num>  Apply radio preset");
      Serial.println("    set channel.add <name> Add hashtag channel");
      Serial.println("    set channel.del <idx>  Delete channel by index");
#ifdef HAS_4G_MODEM
      Serial.println("");
      Serial.println("  4G modem:");
      Serial.println("    get/set apn, get imei, set modem on/off");
#endif
      Serial.println("");
      Serial.println("  System:");
      Serial.println("    rebuild   Erase & rebuild filesystem");
      Serial.println("    erase     Format filesystem");
      Serial.println("    reboot    Restart device");
      Serial.println("    ls / cat / rm   File operations");
#if defined(LilyGo_T5S3_EPaper_Pro)
      Serial.println("");
      Serial.println("  Display:");
      Serial.println("    set backlight on/off/0-255  Control front-light");
#endif

    // =====================================================================
    // Existing system commands (unchanged)
    // =====================================================================
    } else if (strcmp(cli_command, "rebuild") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        _store->saveMainIdentity(self_id);
        savePrefs();
        saveContacts();
        saveChannels();
        Serial.println("  > erase and rebuild done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (strcmp(cli_command, "erase") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        Serial.println("  > erase done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (memcmp(cli_command, "ls", 2) == 0) {

      // get path from command e.g: "ls /adafruit"
      const char *path = &cli_command[3];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }
      Serial.printf("Listing files in %s\n", path);

      // log each file and directory
      File root = _store->openRead(path);
      if (is_fs2 == false) {
        if (root) {
          File file = root.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  UserData%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] UserData%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root.openNextFile();
          }
          root.close();
        }
      }

      if (is_fs2 == true || strlen(path) == 0 || strcmp(path, "/") == 0) {
        if (_store->getSecondaryFS() != nullptr) {
          File root2 = _store->openRead(_store->getSecondaryFS(), path);
          File file = root2.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  ExtraFS%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] ExtraFS%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root2.openNextFile();
          }
          root2.close();
        }
      }
    } else if (memcmp(cli_command, "cat", 3) == 0) {

      // get path from command e.g: "cat /contacts3"
      const char *path = &cli_command[4];
      
      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      } else {
        Serial.println("Invalid path provided, must start with UserData/ or ExtraFS/");
        cli_command[0] = 0;
        return;
      }

      // log file content as hex
      File file = _store->openRead(path);
      if (is_fs2 == true) {
        file = _store->openRead(_store->getSecondaryFS(), path);
      }
      if(file){

        // get file content
        int file_size = file.available();
        uint8_t buffer[file_size];
        file.read(buffer, file_size);

        // print hex
        mesh::Utils::printHex(Serial, buffer, file_size);
        Serial.print("\n");

        file.close();

      }

    } else if (memcmp(cli_command, "rm ", 3) == 0) {
      // get path from command e.g: "rm /adv_blobs"
      const char *path = &cli_command[3];
      MESH_DEBUG_PRINTLN("Removing file: %s", path);
      // ensure path is not empty, or root dir
      if(!path || strlen(path) == 0 || strcmp(path, "/") == 0){
        Serial.println("Invalid path provided");
      } else {
      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }

        // remove file
        bool removed;
        if (is_fs2) {
          MESH_DEBUG_PRINTLN("Removing file from ExtraFS: %s", path);
          removed = _store->removeFile(_store->getSecondaryFS(), path);
        } else {
          MESH_DEBUG_PRINTLN("Removing file from UserData: %s", path);
          removed = _store->removeFile(path);
        }
        if(removed){
          Serial.println("File removed");
        } else {
          Serial.println("Failed to remove file");
        }

      }

    } else if (strcmp(cli_command, "reboot") == 0) {
      board.reboot();  // doesn't return
    } else {
      Serial.println("  Error: unknown command (try 'help')");
    }

    cli_command[0] = 0;  // reset command buffer
  }
}

void MyMesh::checkSerialInterface() {
  size_t len = _serial->checkRecvFrame(cmd_frame);
  if (len > 0) {
    handleCmdFrame(len);
  } else if (_iter_started              // check if our ContactsIterator is 'running'
             && !_serial->isWriteBusy() // don't spam the Serial Interface too quickly!
  ) {
    ContactInfo contact;
    if (_iter.hasNext(this, contact)) {
      if (contact.lastmod > _iter_filter_since) { // apply the 'since' filter
        writeContactRespFrame(RESP_CODE_CONTACT, contact);
        if (contact.lastmod > _most_recent_lastmod) {
          _most_recent_lastmod = contact.lastmod; // save for the RESP_CODE_END_OF_CONTACTS frame
        }
      }
    } else { // EOF
      out_frame[0] = RESP_CODE_END_OF_CONTACTS;
      memcpy(&out_frame[1], &_most_recent_lastmod,
             4); // include the most recent lastmod, so app can update their 'since'
      _serial->writeFrame(out_frame, 5);
      _iter_started = false;
    }
  //} else if (!_serial->isWriteBusy()) {
  //  checkConnections();    // TODO - deprecate the 'Connections' stuff
  }
}

void MyMesh::loop() {
  BaseChatMesh::loop();

  // Always check USB serial for text CLI commands (independent of BLE)
  checkCLIRescueCmd();

  // Process BLE/WiFi companion app binary frames
  if (!_cli_rescue) {
    checkSerialInterface();
  }

  // is there are pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    if (!_store->isSaveInProgress()) {
      _store->beginSaveContacts(this);
    }
    dirty_contacts_expiry = 0;
  }

  // Drive chunked contact save — write a batch each loop iteration
  if (_store->isSaveInProgress()) {
    if (!_store->saveContactsChunk(20)) {  // 20 contacts per chunk (~3KB, ~30ms)
      _store->finishSaveContacts();  // Done or error — verify and commit
    }
  }

  // Discovery scan timeout
  if (_discoveryActive && millisHasNowPassed(_discoveryTimeout)) {
    _discoveryActive = false;
    Serial.printf("[Discovery] Scan complete: %d nodes found\n", _discoveredCount);
  }

#ifdef DISPLAY_CLASS
  if (_ui) _ui->setHasConnection(_serial->isConnected());
#endif
}

bool MyMesh::advert() {
  mesh::Packet* pkt;
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
    pkt = createSelfAdvert(_prefs.node_name);
  } else {
    pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
  }
  if (pkt) {
    sendZeroHop(pkt);
    return true;
  } else {
    return false;
  }
}

void MyMesh::startDiscovery(uint32_t duration_ms) {
  _discoveredCount = 0;
  _discoveryActive = true;
  _discoveryTimeout = futureMillis(duration_ms);
  _discoveryTag = getRNG()->nextInt(1, 0xFFFFFFFF);

  Serial.printf("[Discovery] Active scan started (%lu ms, tag=%08X)\n",
                duration_ms, _discoveryTag);

  // --- Send active discovery request (CTL_TYPE_NODE_DISCOVER_REQ) ---
  // Repeaters with firmware v1.11+ will respond with their pubkey + SNR
  uint8_t ctl_payload[10];
  ctl_payload[0] = CTL_TYPE_NODE_DISCOVER_REQ;  // 0x80, prefix_only=0 (full 32-byte pubkeys)
  ctl_payload[1] = (1 << ADV_TYPE_REPEATER)     // repeaters
                 | (1 << ADV_TYPE_ROOM);         // rooms (repeaters with chat)
  memcpy(&ctl_payload[2], &_discoveryTag, 4);    // random correlation tag
  uint32_t since = 0;                            // accept all firmware versions
  memcpy(&ctl_payload[6], &since, 4);

  auto pkt = createControlData(ctl_payload, sizeof(ctl_payload));
  if (pkt) {
    sendZeroHop(pkt);
    Serial.println("[Discovery] Sent CTL_TYPE_NODE_DISCOVER_REQ (zero-hop)");
  } else {
    Serial.println("[Discovery] ERROR: createControlData returned NULL (packet pool full?)");
  }
}

void MyMesh::stopDiscovery() {
  _discoveryActive = false;
}

bool MyMesh::addDiscoveredToContacts(int idx) {
  if (idx < 0 || idx >= _discoveredCount) return false;
  if (_discovered[idx].already_in_contacts) return true;  // already there

  // Retrieve cached raw advert packet and import it
  uint8_t buf[256];
  int plen = getBlobByKey(_discovered[idx].contact.id.pub_key, PUB_KEY_SIZE, buf);
  if (plen > 0) {
    bool ok = importContact(buf, (uint8_t)plen);
    if (ok) {
      _discovered[idx].already_in_contacts = true;
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      MESH_DEBUG_PRINTLN("Discovery: added contact '%s'", _discovered[idx].contact.name);
    }
    return ok;
  }
  MESH_DEBUG_PRINTLN("Discovery: no cached advert blob for contact '%s'", _discovered[idx].contact.name);
  return false;
}
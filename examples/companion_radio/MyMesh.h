#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include "AbstractUITask.h"

/*------------ Frame Protocol --------------*/
#define FIRMWARE_VER_CODE 11

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE "12 April 2026"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "Meck v1.7"
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
#include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
#include <LittleFS.h>
#elif defined(ESP32)
#include <SPIFFS.h>
#endif

#include "DataStore.h"
#include "NodePrefs.h"

#include <RTClib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <target.h>

/* ---------------------------------- CONFIGURATION ------------------------------------- */

#ifndef LORA_FREQ
#define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
#define LORA_BW 250
#endif
#ifndef LORA_SF
#define LORA_SF 10
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
#define LORA_TX_POWER 20
#endif
#ifndef MAX_LORA_TX_POWER
#define MAX_LORA_TX_POWER LORA_TX_POWER
#endif

#ifndef MAX_CONTACTS
#define MAX_CONTACTS 100
#endif

#ifndef OFFLINE_QUEUE_SIZE
#define OFFLINE_QUEUE_SIZE 16
#endif

#ifndef BLE_NAME_PREFIX
#define BLE_NAME_PREFIX "MeshCore-"
#endif

#include <helpers/BaseChatMesh.h>
#include <helpers/TransportKeyStore.h>

// Custom path lock flag — bit 7 of ContactInfo.flags
// When set, onContactPathRecv skips auto-updating this contact's out_path.
// Bits 0-6 remain available (bit 0 = favourite, bits 1-3 = telemetry perms).
#define CONTACT_FLAG_CUSTOM_PATH  0x80

/* -------------------------------------------------------------------------------------- */

#define REQ_TYPE_GET_STATUS             0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE             0x02
#define REQ_TYPE_GET_TELEMETRY_DATA     0x03

struct AdvertPath {
  uint8_t pubkey_prefix[8];
  uint8_t path_len;
  uint8_t type;             // ADV_TYPE_* (Chat/Repeater/Room/Sensor)
  char    name[32];
  uint32_t recv_timestamp;
  uint8_t path[MAX_PATH_SIZE];
};

// Discovery scan — transient buffer for on-device node discovery
#define MAX_DISCOVERED_NODES 20

struct DiscoveredNode {
  ContactInfo contact;
  uint8_t path_len;
  int8_t snr;                 // SNR × 4 from active discovery response (0 if pre-seeded)
  bool already_in_contacts;   // true if contact was auto-added or already known
};

class MyMesh : public BaseChatMesh, public DataStoreHost {
public:
  MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store, AbstractUITask* ui=NULL);

  void begin(bool has_display);
  void startInterface(BaseSerialInterface &serial);

  const char *getNodeName();
  NodePrefs *getNodePrefs();
  uint32_t getBLEPin();

  void loop();
  void handleCmdFrame(size_t len);
  bool advert();
  void enterCLIRescue();

  int  getRecentlyHeard(AdvertPath dest[], int max_num);

  // Discovery scan — on-device node discovery
  void startDiscovery(uint32_t duration_ms = 30000);
  void stopDiscovery();
  bool isDiscoveryActive() const { return _discoveryActive; }
  int  getDiscoveredCount() const { return _discoveredCount; }
  const DiscoveredNode& getDiscovered(int idx) const { return _discovered[idx]; }
  bool addDiscoveredToContacts(int idx);  // promote a discovered node into contacts

  // Last Heard — public wrappers for contact add/remove from UI
  void scheduleLazyContactSave();
  int getContactBlob(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
    return getBlobByKey(key, key_len, dest_buf);
  }
  // Force-add a contact from a raw advert blob, bypassing auto-add settings.
  // Used by Last Heard and Discovery when the user explicitly selects a node to add.
  bool forceImportContact(const uint8_t* blob, uint8_t len);
  
  // Queue a sent channel message for BLE app sync
  void queueSentChannelMessage(uint8_t channel_idx, uint32_t timestamp, const char* sender, const char* text);

  // Send a direct message from the UI (no BLE dependency)
  bool uiSendDirectMessage(uint32_t contact_idx, const char* text);

  // Send raw binary data to a contact (PAYLOAD_TYPE_RAW_CUSTOM, direct route only)
  // Used for dz0ny VE3 voice protocol: voice packets (0x56) and fetch requests (0x72)
  bool uiSendRawToContact(uint32_t contact_idx, const uint8_t* data, uint8_t len);

  // Voice-over-LoRa: callback for incoming raw voice packets (dz0ny VE3 protocol)
  // magic 0x56 = voice data packet, 0x72 = fetch request
  typedef void (*VoiceRawHandler)(uint8_t magic, const uint8_t* payload, uint8_t len);
  void setVoiceHandler(VoiceRawHandler h) { _voiceHandler = h; }

  // Voice-over-LoRa: callback for incoming VE3 envelope in a DM
  // Called with sender name and the VE3 text (e.g. "VE3:a:1:3:2")
  typedef void (*VoiceEnvelopeHandler)(const char* senderName, const char* ve3Text);
  void setVoiceEnvelopeHandler(VoiceEnvelopeHandler h) { _voiceEnvHandler = h; }

  // Defer contact saves while voice packets are being received
  // (SD writes block SPI bus shared with LoRa radio)
  void setDeferSaves(bool defer) { _deferSaves = defer; }
  bool isDeferSaves() const { return _deferSaves; }

  // Repeater admin - UI-initiated operations
  bool uiLoginToRepeater(uint32_t contact_idx, const char* password, uint32_t& est_timeout_ms);
  bool uiSendCliCommand(uint32_t contact_idx, const char* command);
  bool uiSendTelemetryRequest(uint32_t contact_idx);
  int  getAdminContactIdx() const { return _admin_contact_idx; }

  // Custom path editor — set or clear a manually configured path for a contact
  // When locked, automatic path discovery will not overwrite this contact's path.
  bool setCustomPath(int contactIdx, const uint8_t* path, uint8_t pathLen, bool lock);
  void clearCustomPath(int contactIdx);


protected:
  float getAirtimeBudgetFactor() const override;
  int getInterferenceThreshold() const override;
  uint8_t getTxFailResetThreshold() const override;
  uint8_t getRxFailRebootThreshold() const override;
  void onRxUnrecoverable() override;
  int calcRxDelay(float score, uint32_t air_time) const override;
  uint32_t getRetransmitDelay(const mesh::Packet *packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet *packet) override;
  uint8_t getExtraAckTransmitCount() const override;
  uint8_t getAutoAddMaxHops() const override;
  bool filterRecvFloodPacket(mesh::Packet* packet) override;

  uint8_t getPathHashSize() const override { return _prefs.path_hash_mode + 1; }
  void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0) override;
  void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0) override;

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
  bool isAutoAddEnabled() const override;
  bool shouldAutoAddContactType(uint8_t type) const override;
  bool shouldOverwriteWhenFull() const override;
  void onContactsFull() override;
  void onContactOverwrite(const uint8_t* pub_key) override;
  bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo &contact) override;
  ContactInfo* processAck(const uint8_t *data) override;
  void queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt, uint32_t sender_timestamp,
                    const uint8_t *extra, int extra_len, const char *text);

  void onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                     const char *text) override;
  void onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                         const char *text) override;
  void onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const uint8_t *sender_prefix, const char *text) override;
  void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                            const char *text) override;

  uint8_t onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                           uint8_t len, uint8_t *reply) override;
  void onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) override;
  void onControlDataRecv(mesh::Packet *packet) override;
  void onRawDataRecv(mesh::Packet *packet) override;
  void onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) override;

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override;

  // DataStoreHost methods
  bool onContactLoaded(const ContactInfo& contact) override { return addContact(contact); }
  bool getContactForSave(uint32_t idx, ContactInfo& contact) override { return getContactByIdx(idx, contact); }
  bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) override { return setChannel(channel_idx, ch); }
  bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) override { return getChannel(channel_idx, ch); }

  void clearPendingReqs() {
    pending_login = pending_status = pending_telemetry = pending_discovery = pending_req = 0;
  }

public:
  void savePrefs() { _store->savePrefs(_prefs, sensors.node_lat, sensors.node_lon); }
  void saveChannels() {
    _store->saveChannels(this);
  }
  void saveContacts() {
    _store->saveContacts(this);
  }

private:
  void writeOKFrame();
  void writeErrFrame(uint8_t err_code);
  void writeDisabledFrame();
  size_t writeContactRespFrame(uint8_t code, const ContactInfo &contact);
  void updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len);
  void addToOfflineQueue(const uint8_t frame[], int len);
  int getFromOfflineQueue(uint8_t frame[]);
  int getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) override { 
    return _store->getBlobByKey(key, key_len, dest_buf);
  }
  bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) override {
    return _store->putBlobByKey(key, key_len, src_buf, len);
  }

  void checkCLIRescueCmd();
  void checkSerialInterface();

  DataStore* _store;
  NodePrefs _prefs;
  VoiceRawHandler _voiceHandler = nullptr;
  VoiceEnvelopeHandler _voiceEnvHandler = nullptr;
  mutable bool _forceNextImport = false;
  bool _deferSaves = false;
  uint32_t pending_login;
  uint32_t pending_status;
  uint32_t pending_telemetry, pending_discovery;   // pending _TELEMETRY_REQ
  uint32_t pending_req;   // pending _BINARY_REQ
  BaseSerialInterface *_serial;
  AbstractUITask* _ui;

  ContactsIterator _iter;
  uint32_t _iter_filter_since;
  uint32_t _most_recent_lastmod;
  uint32_t _active_ble_pin;
  bool _iter_started;
  bool _cli_rescue;
  char cli_command[80];
  uint8_t app_target_ver;
  uint8_t *sign_data;
  uint32_t sign_data_len;
  unsigned long dirty_contacts_expiry;

  TransportKey send_scope;

  uint8_t cmd_frame[MAX_FRAME_SIZE + 1];
  uint8_t out_frame[MAX_FRAME_SIZE + 1];
  CayenneLPP telemetry;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];

    bool isChannelMsg() const;
  };
  int offline_queue_len;
  Frame offline_queue[OFFLINE_QUEUE_SIZE];

  struct AckTableEntry {
    unsigned long msg_sent;
    uint32_t ack;
    ContactInfo* contact;
  };
  #define EXPECTED_ACK_TABLE_SIZE 8
  AckTableEntry expected_ack_table[EXPECTED_ACK_TABLE_SIZE]; // circular table
  int next_ack_idx;

  #define ADVERT_PATH_TABLE_SIZE   1000
  AdvertPath* advert_paths;  // PSRAM-allocated in begin(), size = ADVERT_PATH_TABLE_SIZE

    // Sent message repeat tracking
  #define SENT_TRACK_SIZE          4
  #define SENT_FINGERPRINT_SIZE    12
  #define SENT_TRACK_EXPIRY_MS     30000  // stop tracking after 30 seconds
  struct SentMsgTrack {
    uint8_t fingerprint[SENT_FINGERPRINT_SIZE];
    uint8_t repeat_count;
    unsigned long sent_millis;
    bool active;
  };
  SentMsgTrack _sent_track[SENT_TRACK_SIZE];
  int _sent_track_idx;  // next slot in circular buffer
  int _admin_contact_idx;  // contact index for active admin session (-1 if none)

  // Discovery scan state
  DiscoveredNode _discovered[MAX_DISCOVERED_NODES];
  int _discoveredCount;
  bool _discoveryActive;
  unsigned long _discoveryTimeout;
  uint32_t _discoveryTag;      // random correlation tag for active discovery
};

extern MyMesh the_mesh;
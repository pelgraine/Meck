#pragma once

// =============================================================================
// SMSContacts - Phone-to-name lookup for SMS contacts (4G variant)
//
// Stores contacts in /sms/contacts.txt on SD card.
// Format: one contact per line as "phone=Display Name"
//
// Completely separate from mesh ContactInfo / IdentityStore.
//
// Guard: HAS_4G_MODEM
// =============================================================================

#ifdef HAS_4G_MODEM

#ifndef SMS_CONTACTS_H
#define SMS_CONTACTS_H

#include <Arduino.h>
#include <SD.h>

#define SMS_CONTACT_NAME_LEN   24
#define SMS_CONTACT_MAX        30
#define SMS_CONTACTS_FILE      "/sms/contacts.txt"

struct SMSContact {
  char phone[20];   // matches SMS_PHONE_LEN
  char name[SMS_CONTACT_NAME_LEN];
  bool valid;
};

class SMSContactStore {
public:
  void begin() {
    _count = 0;
    memset(_contacts, 0, sizeof(_contacts));
    load();
  }

  // Look up a name by phone number. Returns nullptr if not found.
  const char* lookup(const char* phone) const {
    for (int i = 0; i < _count; i++) {
      if (_contacts[i].valid && strcmp(_contacts[i].phone, phone) == 0) {
        return _contacts[i].name;
      }
    }
    return nullptr;
  }

  // Fill buf with display name if found, otherwise copy phone number.
  // Returns true if a name was found.
  bool displayName(const char* phone, char* buf, size_t bufLen) const {
    const char* name = lookup(phone);
    if (name && name[0]) {
      strncpy(buf, name, bufLen - 1);
      buf[bufLen - 1] = '\0';
      return true;
    }
    strncpy(buf, phone, bufLen - 1);
    buf[bufLen - 1] = '\0';
    return false;
  }

  // Add or update a contact. Returns true on success.
  bool set(const char* phone, const char* name) {
    // Update existing
    for (int i = 0; i < _count; i++) {
      if (_contacts[i].valid && strcmp(_contacts[i].phone, phone) == 0) {
        strncpy(_contacts[i].name, name, SMS_CONTACT_NAME_LEN - 1);
        _contacts[i].name[SMS_CONTACT_NAME_LEN - 1] = '\0';
        save();
        return true;
      }
    }
    // Add new
    if (_count >= SMS_CONTACT_MAX) return false;
    strncpy(_contacts[_count].phone, phone, sizeof(_contacts[_count].phone) - 1);
    _contacts[_count].phone[sizeof(_contacts[_count].phone) - 1] = '\0';
    strncpy(_contacts[_count].name, name, SMS_CONTACT_NAME_LEN - 1);
    _contacts[_count].name[SMS_CONTACT_NAME_LEN - 1] = '\0';
    _contacts[_count].valid = true;
    _count++;
    save();
    return true;
  }

  // Remove a contact by phone number
  bool remove(const char* phone) {
    for (int i = 0; i < _count; i++) {
      if (_contacts[i].valid && strcmp(_contacts[i].phone, phone) == 0) {
        for (int j = i; j < _count - 1; j++) {
          _contacts[j] = _contacts[j + 1];
        }
        _count--;
        memset(&_contacts[_count], 0, sizeof(SMSContact));
        save();
        return true;
      }
    }
    return false;
  }

  // Accessors for list browsing
  int count() const { return _count; }
  const SMSContact& get(int index) const { return _contacts[index]; }

  // Check if a contact exists
  bool exists(const char* phone) const { return lookup(phone) != nullptr; }

private:
  SMSContact _contacts[SMS_CONTACT_MAX];
  int _count = 0;

  void load() {
    File f = SD.open(SMS_CONTACTS_FILE, FILE_READ);
    if (!f) {
      Serial.println("[SMSContacts] No contacts file, starting fresh");
      return;
    }

    char line[64];
    while (f.available() && _count < SMS_CONTACT_MAX) {
      int pos = 0;
      while (f.available() && pos < (int)sizeof(line) - 1) {
        char c = f.read();
        if (c == '\n' || c == '\r') break;
        line[pos++] = c;
      }
      line[pos] = '\0';
      if (pos == 0) continue;
      // Consume trailing CR/LF
      while (f.available()) {
        int pk = f.peek();
        if (pk == '\n' || pk == '\r') { f.read(); continue; }
        break;
      }

      // Parse "phone=name"
      char* eq = strchr(line, '=');
      if (!eq) continue;
      *eq = '\0';
      const char* phone = line;
      const char* name = eq + 1;
      if (strlen(phone) == 0 || strlen(name) == 0) continue;

      strncpy(_contacts[_count].phone, phone, sizeof(_contacts[_count].phone) - 1);
      strncpy(_contacts[_count].name, name, SMS_CONTACT_NAME_LEN - 1);
      _contacts[_count].valid = true;
      _count++;
    }
    f.close();
    Serial.printf("[SMSContacts] Loaded %d contacts\n", _count);
  }

  void save() {
    if (!SD.exists("/sms")) SD.mkdir("/sms");
    File f = SD.open(SMS_CONTACTS_FILE, FILE_WRITE);
    if (!f) {
      Serial.println("[SMSContacts] Failed to write contacts file");
      return;
    }
    for (int i = 0; i < _count; i++) {
      if (!_contacts[i].valid) continue;
      f.print(_contacts[i].phone);
      f.print('=');
      f.println(_contacts[i].name);
    }
    f.close();
  }
};

// Global singleton
extern SMSContactStore smsContacts;

#endif // SMS_CONTACTS_H
#endif // HAS_4G_MODEM
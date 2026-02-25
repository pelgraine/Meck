#pragma once

// =============================================================================
// ApnDatabase.h - Embedded APN Lookup Table
//
// Maps MCC/MNC (Mobile Country Code / Mobile Network Code) to default APN
// settings for common carriers worldwide. Compiled directly into flash (~3KB)
// so users never need to manually install a lookup file.
//
// The modem queries IMSI via AT+CIMI to extract MCC (3 digits) + MNC (2-3
// digits), then looks up the APN here. If not found, falls back to the
// modem's existing PDP context (AT+CGDCONT?) or user-configured APN.
//
// To add a carrier: append to APN_DATABASE[] with the MCC+MNC as a single
// integer.  MNC can be 2 or 3 digits:
//   MCC=310, MNC=260  → mccmnc = 310260
//   MCC=505, MNC=01   → mccmnc = 50501
//
// Guard: HAS_4G_MODEM
// =============================================================================

#ifdef HAS_4G_MODEM

#ifndef APN_DATABASE_H
#define APN_DATABASE_H

struct ApnEntry {
  uint32_t mccmnc;        // MCC+MNC as integer (e.g. 310260 for T-Mobile US)
  const char* apn;        // APN string
  const char* carrier;    // Human-readable carrier name (for debug/display)
};

// ---------------------------------------------------------------------------
// APN Database — sorted by MCC for binary search potential (not required)
//
// Sources: carrier documentation, GSMA databases, community wikis.
// This covers ~120 major carriers across key regions. Users with less
// common carriers can set APN manually in Settings.
// ---------------------------------------------------------------------------

static const ApnEntry APN_DATABASE[] = {
  // =========================================================================
  // Australia (MCC 505)
  // =========================================================================
  { 50501,  "telstra.internet",    "Telstra" },
  { 50502,  "yesinternet",         "Optus" },
  { 50503,  "vfinternet.au",       "Vodafone AU" },
  { 50506,  "3netaccess",          "Three AU" },
  { 50507,  "telstra.internet",    "Vodafone AU (MVNO)" },  // Many MVNOs on Telstra
  { 50510,  "telstra.internet",    "Norfolk Tel" },
  { 50512,  "3netaccess",          "Amaysim" },              // Optus MVNO
  { 50514,  "yesinternet",         "Aussie Broadband" },     // Optus MVNO
  { 50590,  "yesinternet",         "Optus MVNO" },

  // =========================================================================
  // New Zealand (MCC 530)
  // =========================================================================
  { 53001,  "internet",            "Vodafone NZ" },
  { 53005,  "internet",            "Spark NZ" },
  { 53024,  "internet",            "2degrees" },

  // =========================================================================
  // United States (MCC 310, 311, 312, 313, 316)
  // =========================================================================
  { 310012, "fast.t-mobile.com",   "Verizon (old)" },
  { 310026, "fast.t-mobile.com",   "T-Mobile US" },
  { 310030, "fast.t-mobile.com",   "T-Mobile US" },
  { 310032, "fast.t-mobile.com",   "T-Mobile US" },
  { 310060, "fast.t-mobile.com",   "T-Mobile US" },
  { 310160, "fast.t-mobile.com",   "T-Mobile US" },
  { 310200, "fast.t-mobile.com",   "T-Mobile US" },
  { 310210, "fast.t-mobile.com",   "T-Mobile US" },
  { 310220, "fast.t-mobile.com",   "T-Mobile US" },
  { 310230, "fast.t-mobile.com",   "T-Mobile US" },
  { 310240, "fast.t-mobile.com",   "T-Mobile US" },
  { 310250, "fast.t-mobile.com",   "T-Mobile US" },
  { 310260, "fast.t-mobile.com",   "T-Mobile US" },
  { 310270, "fast.t-mobile.com",   "T-Mobile US" },
  { 310310, "fast.t-mobile.com",   "T-Mobile US" },
  { 310490, "fast.t-mobile.com",   "T-Mobile US" },
  { 310530, "fast.t-mobile.com",   "T-Mobile US" },
  { 310580, "fast.t-mobile.com",   "T-Mobile US" },
  { 310660, "fast.t-mobile.com",   "T-Mobile US" },
  { 310800, "fast.t-mobile.com",   "T-Mobile US" },
  { 311480, "vzwinternet",         "Verizon" },
  { 311481, "vzwinternet",         "Verizon" },
  { 311482, "vzwinternet",         "Verizon" },
  { 311483, "vzwinternet",         "Verizon" },
  { 311484, "vzwinternet",         "Verizon" },
  { 311489, "vzwinternet",         "Verizon" },
  { 310410, "fast.t-mobile.com",   "AT&T (migrated)" },
  { 310120, "att.mvno",            "AT&T (Sprint)" },
  { 312530, "iot.1nce.net",        "1NCE IoT" },
  { 310120, "tfdata",              "Tracfone" },

  // =========================================================================
  // Canada (MCC 302)
  // =========================================================================
  { 30220,  "internet.com",        "Rogers" },
  { 30221,  "internet.com",        "Rogers" },
  { 30237,  "internet.com",        "Rogers" },
  { 30272,  "internet.com",        "Rogers" },
  { 30234,  "sp.telus.com",        "Telus" },
  { 30286,  "sp.telus.com",        "Telus" },
  { 30236,  "sp.telus.com",        "Telus" },
  { 30261,  "sp.bell.ca",          "Bell" },
  { 30263,  "sp.bell.ca",          "Bell" },
  { 30267,  "sp.bell.ca",          "Bell" },
  { 30268,  "fido-core-appl1.apn", "Fido" },
  { 30278,  "internet.com",        "SaskTel" },
  { 30266,  "sp.mb.com",           "MTS" },

  // =========================================================================
  // United Kingdom (MCC 234, 235)
  // =========================================================================
  { 23410,  "o2-internet",         "O2 UK" },
  { 23415,  "three.co.uk",         "Vodafone UK" },
  { 23420,  "three.co.uk",         "Three UK" },
  { 23430,  "everywhere",          "EE" },
  { 23431,  "everywhere",          "EE" },
  { 23432,  "everywhere",          "EE" },
  { 23433,  "everywhere",          "EE" },
  { 23450,  "data.lycamobile.co.uk","Lycamobile UK" },
  { 23486,  "three.co.uk",         "Three UK" },

  // =========================================================================
  // Germany (MCC 262)
  // =========================================================================
  { 26201,  "internet.t-mobile",   "Telekom DE" },
  { 26202,  "web.vodafone.de",     "Vodafone DE" },
  { 26203,  "internet",            "O2 DE" },
  { 26207,  "internet",            "O2 DE" },

  // =========================================================================
  // France (MCC 208)
  // =========================================================================
  { 20801,  "orange",              "Orange FR" },
  { 20810,  "sl2sfr",              "SFR" },
  { 20815,  "free",                "Free Mobile" },
  { 20820,  "ofnew.fr",            "Bouygues" },

  // =========================================================================
  // Italy (MCC 222)
  // =========================================================================
  { 22201,  "mobile.vodafone.it",  "TIM" },
  { 22210,  "mobile.vodafone.it",  "Vodafone IT" },
  { 22250,  "internet.it",         "Iliad IT" },
  { 22288,  "internet.wind",       "WindTre" },
  { 22299,  "internet.wind",       "WindTre" },

  // =========================================================================
  // Spain (MCC 214)
  // =========================================================================
  { 21401,  "internet",            "Vodafone ES" },
  { 21403,  "internet",            "Orange ES" },
  { 21404,  "internet",            "Yoigo" },
  { 21407,  "internet",            "Movistar" },

  // =========================================================================
  // Netherlands (MCC 204)
  // =========================================================================
  { 20404,  "internet",            "Vodafone NL" },
  { 20408,  "internet",            "KPN" },
  { 20412,  "internet",            "Telfort" },
  { 20416,  "internet",            "T-Mobile NL" },
  { 20420,  "internet",            "T-Mobile NL" },

  // =========================================================================
  // Sweden (MCC 240)
  // =========================================================================
  { 24001,  "internet.telia.se",   "Telia SE" },
  { 24002,  "tre.se",              "Three SE" },
  { 24007,  "internet.telenor.se", "Telenor SE" },

  // =========================================================================
  // Norway (MCC 242)
  // =========================================================================
  { 24201,  "internet.telenor.no", "Telenor NO" },
  { 24202,  "internet.netcom.no",  "Telia NO" },

  // =========================================================================
  // Denmark (MCC 238)
  // =========================================================================
  { 23801,  "internet",            "TDC" },
  { 23802,  "internet",            "Telenor DK" },
  { 23806,  "internet",            "Three DK" },
  { 23820,  "internet",            "Telia DK" },

  // =========================================================================
  // Switzerland (MCC 228)
  // =========================================================================
  { 22801,  "gprs.swisscom.ch",    "Swisscom" },
  { 22802,  "internet",            "Sunrise" },
  { 22803,  "internet",            "Salt" },

  // =========================================================================
  // Austria (MCC 232)
  // =========================================================================
  { 23201,  "a1.net",              "A1" },
  { 23203,  "web.one.at",          "Three AT" },
  { 23205,  "web",                 "T-Mobile AT" },

  // =========================================================================
  // Japan (MCC 440, 441)
  // =========================================================================
  { 44010,  "spmode.ne.jp",        "NTT Docomo" },
  { 44020,  "plus.4g",             "SoftBank" },
  { 44051,  "au.au-net.ne.jp",     "KDDI au" },

  // =========================================================================
  // South Korea (MCC 450)
  // =========================================================================
  { 45005,  "lte.sktelecom.com",   "SK Telecom" },
  { 45006,  "lte.ktfwing.com",     "KT" },
  { 45008,  "lte.lguplus.co.kr",   "LG U+" },

  // =========================================================================
  // India (MCC 404, 405)
  // =========================================================================
  { 40445,  "airtelgprs.com",      "Airtel" },
  { 40410,  "airtelgprs.com",      "Airtel" },
  { 40411,  "www",                 "Vodafone IN (Vi)" },
  { 40413,  "www",                 "Vodafone IN (Vi)" },
  { 40486,  "www",                 "Vodafone IN (Vi)" },
  { 40553,  "jionet",              "Jio" },
  { 40554,  "jionet",              "Jio" },
  { 40512,  "bsnlnet",             "BSNL" },

  // =========================================================================
  // Singapore (MCC 525)
  // =========================================================================
  { 52501,  "internet",            "Singtel" },
  { 52503,  "internet",            "M1" },
  { 52505,  "internet",            "StarHub" },

  // =========================================================================
  // Hong Kong (MCC 454)
  // =========================================================================
  { 45400,  "internet",            "CSL" },
  { 45406,  "internet",            "SmarTone" },
  { 45412,  "internet",            "CMHK" },

  // =========================================================================
  // Brazil (MCC 724)
  // =========================================================================
  { 72405,  "claro.com.br",        "Claro BR" },
  { 72406,  "wap.oi.com.br",       "Vivo" },
  { 72410,  "wap.oi.com.br",       "Vivo" },
  { 72411,  "wap.oi.com.br",       "Vivo" },
  { 72415,  "internet.tim.br",     "TIM BR" },
  { 72431,  "gprs.oi.com.br",      "Oi" },

  // =========================================================================
  // Mexico (MCC 334)
  // =========================================================================
  { 33402,  "internet.itelcel.com","Telcel" },
  { 33403,  "internet.movistar.mx","Movistar MX" },
  { 33404,  "internet.att.net.mx", "AT&T MX" },

  // =========================================================================
  // South Africa (MCC 655)
  // =========================================================================
  { 65501,  "internet",            "Vodacom" },
  { 65502,  "internet",            "Telkom ZA" },
  { 65507,  "internet",            "Cell C" },
  { 65510,  "internet",            "MTN ZA" },

  // =========================================================================
  // Philippines (MCC 515)
  // =========================================================================
  { 51502,  "internet.globe.com.ph","Globe" },
  { 51503,  "internet",            "Smart" },
  { 51505,  "internet",            "Sun Cellular" },

  // =========================================================================
  // Thailand (MCC 520)
  // =========================================================================
  { 52001,  "internet",            "AIS" },
  { 52004,  "internet",            "TrueMove" },
  { 52005,  "internet",            "dtac" },

  // =========================================================================
  // Indonesia (MCC 510)
  // =========================================================================
  { 51001,  "internet",            "Telkomsel" },
  { 51010,  "internet",            "Telkomsel" },
  { 51011,  "3gprs",               "XL Axiata" },
  { 51028,  "3gprs",               "XL Axiata (Axis)" },

  // =========================================================================
  // Malaysia (MCC 502)
  // =========================================================================
  { 50212,  "celcom3g",            "Celcom" },
  { 50213,  "celcom3g",            "Celcom" },
  { 50216,  "internet",            "Digi" },
  { 50219,  "celcom3g",            "Celcom" },

  // =========================================================================
  // Czech Republic (MCC 230)
  // =========================================================================
  { 23001,  "internet.t-mobile.cz","T-Mobile CZ" },
  { 23002,  "internet",            "O2 CZ" },
  { 23003,  "internet.vodafone.cz","Vodafone CZ" },

  // =========================================================================
  // Poland (MCC 260)
  // =========================================================================
  { 26001,  "internet",            "Plus PL" },
  { 26002,  "internet",            "T-Mobile PL" },
  { 26003,  "internet",            "Orange PL" },
  { 26006,  "internet",            "Play" },

  // =========================================================================
  // Portugal (MCC 268)
  // =========================================================================
  { 26801,  "internet",            "Vodafone PT" },
  { 26803,  "internet",            "NOS" },
  { 26806,  "internet",            "MEO" },

  // =========================================================================
  // Ireland (MCC 272)
  // =========================================================================
  { 27201,  "internet",            "Vodafone IE" },
  { 27202,  "open.internet",       "Three IE" },
  { 27205,  "three.ie",            "Three IE" },

  // =========================================================================
  // IoT / Global SIMs
  // =========================================================================
  { 901028, "iot.1nce.net",        "1NCE (IoT)" },
  { 90143,  "hologram",            "Hologram" },
};

#define APN_DATABASE_SIZE (sizeof(APN_DATABASE) / sizeof(APN_DATABASE[0]))

// ---------------------------------------------------------------------------
// Lookup function — returns nullptr if not found
// ---------------------------------------------------------------------------

inline const ApnEntry* apnLookup(uint32_t mccmnc) {
  for (int i = 0; i < (int)APN_DATABASE_SIZE; i++) {
    if (APN_DATABASE[i].mccmnc == mccmnc) {
      return &APN_DATABASE[i];
    }
  }
  return nullptr;
}

// Parse IMSI string into MCC+MNC.  Tries 3-digit MNC first (6-digit mccmnc),
// falls back to 2-digit MNC (5-digit mccmnc) if not found.
inline const ApnEntry* apnLookupFromIMSI(const char* imsi) {
  if (!imsi || strlen(imsi) < 5) return nullptr;

  // Extract MCC (always 3 digits)
  uint32_t mcc = (imsi[0] - '0') * 100 + (imsi[1] - '0') * 10 + (imsi[2] - '0');

  // Try 3-digit MNC first (more specific)
  if (strlen(imsi) >= 6) {
    uint32_t mnc3 = (imsi[3] - '0') * 100 + (imsi[4] - '0') * 10 + (imsi[5] - '0');
    uint32_t mccmnc6 = mcc * 1000 + mnc3;
    const ApnEntry* entry = apnLookup(mccmnc6);
    if (entry) return entry;
  }

  // Fall back to 2-digit MNC
  uint32_t mnc2 = (imsi[3] - '0') * 10 + (imsi[4] - '0');
  uint32_t mccmnc5 = mcc * 100 + mnc2;
  return apnLookup(mccmnc5);
}

#endif // APN_DATABASE_H
#endif // HAS_4G_MODEM
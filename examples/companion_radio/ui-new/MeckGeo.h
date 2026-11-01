#pragma once
// Locations inside message text.
//
// There is no MeshCore message type for a location, so clients put it in the
// text. Read here:
//   [LOC]<lat>,<lon>            a position share (MeshCore-Solo)
//   [WAY]<lat>,<lon> <label>    a waypoint with a name (MeshCore-Solo)
//   geo:<lat>,<lon>             RFC 5870 URI (opens in phone map apps)
//   <lat>,<lon>                 plain decimal degrees anywhere in the text,
//                               with at least 4 decimals each, so ordinary
//                               numbers ("1.5, 2.5") are not taken for one
// Sent by Meck: "[LOC]<lat>,<lon>" with 6 decimals (about 0.1 m), which
// MeshCore-Solo shows on its map and every other client shows as text.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MECK_LOC_TAG "[LOC]"
#define MECK_WAY_TAG "[WAY]"
#define MECK_GEO_URI "geo:"

struct MeckGeoPoint {
  double lat = 0, lon = 0;
  bool tagged = false;      // [LOC], [WAY] or geo:, i.e. meant as a location
  bool waypoint = false;    // [WAY]
  char label[28] = "";      // [WAY] name
};

// One decimal number at p (optional sign); decimals = digits after the point
static inline bool meckGeoNum(const char*& p, double& v, int& decimals) {
  const char* s = p;
  if (*s == '-' || *s == '+') s++;
  if (!isdigit((unsigned char)*s)) return false;
  while (isdigit((unsigned char)*s)) s++;
  decimals = 0;
  if (*s == '.') {
    s++;
    while (isdigit((unsigned char)*s)) { s++; decimals++; }
  }
  v = strtod(p, nullptr);
  p = s;
  return true;
}

// "<lat>,<lon>" (spaces allowed around the comma) at p
static inline bool meckGeoPair(const char*& p, double& lat, double& lon, int minDecimals) {
  const char* s = p;
  int d1, d2;
  if (!meckGeoNum(s, lat, d1)) return false;
  while (*s == ' ') s++;
  if (*s != ',') return false;
  s++;
  while (*s == ' ') s++;
  if (!meckGeoNum(s, lon, d2)) return false;
  if (d1 < minDecimals || d2 < minDecimals) return false;
  if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
  if (lat == 0 && lon == 0) return false;
  p = s;
  return true;
}

// Finds the first location in text; false when there is none
static inline bool meckGeoFind(const char* text, MeckGeoPoint& g) {
  if (!text) return false;
  g = MeckGeoPoint();
  const struct { const char* tag; bool way; } tags[] = {
    { MECK_LOC_TAG, false }, { MECK_WAY_TAG, true }, { MECK_GEO_URI, false },
  };
  for (auto& t : tags) {
    const char* hit = strstr(text, t.tag);
    if (!hit) continue;
    const char* p = hit + strlen(t.tag);
    if (meckGeoPair(p, g.lat, g.lon, 0)) {
      g.tagged = true;
      g.waypoint = t.way;
      if (t.way) {
        while (*p == ' ') p++;
        snprintf(g.label, sizeof(g.label), "%s", p);
        size_t n = strlen(g.label);
        while (n > 0 && isspace((unsigned char)g.label[n - 1])) g.label[--n] = 0;
      }
      return true;
    }
  }
  // Plain coordinates: a number that does not continue a word or a number
  for (const char* p = text; *p; p++) {
    bool start = isdigit((unsigned char)*p) || ((*p == '-' || *p == '+') && isdigit((unsigned char)p[1]));
    if (!start) continue;
    if (p > text && (isalnum((unsigned char)p[-1]) || p[-1] == '.')) continue;
    const char* q = p;
    if (meckGeoPair(q, g.lat, g.lon, 4)) return true;
  }
  return false;
}

// True when the whole text is one tagged location (the chat then shows it
// as a location line instead of the raw tag)
static inline bool meckGeoIsShare(const char* text, MeckGeoPoint& g) {
  if (!text) return false;
  while (*text == ' ') text++;
  if (strncmp(text, MECK_LOC_TAG, 5) && strncmp(text, MECK_WAY_TAG, 5) && strncmp(text, MECK_GEO_URI, 4)) return false;
  return meckGeoFind(text, g) && g.tagged;
}

// The text Meck sends for a position
static inline int meckGeoFormatShare(char* out, size_t n, double lat, double lon) {
  return snprintf(out, n, MECK_LOC_TAG "%.6f,%.6f", lat, lon);
}

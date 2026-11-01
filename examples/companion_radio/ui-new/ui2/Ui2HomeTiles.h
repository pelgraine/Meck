#pragma once
// =============================================================================
// UI v2 -- home tile pages
//
// Two pages of 2 x 4 tiles: page 1 the everyday functions (Settings stays
// bottom right), page 2 media and tools. Home (HA), Weather, Agenda and Mail
// are designed but not built yet, so they are not in the layout; their
// tiles stay defined (drawn disabled, a toast on Enter) for when a feature
// arrives. Shared by HomeScreen (drawing, selection) and main.cpp
// (launching, touch hit test).
// =============================================================================

#include "Ui2Assets.h"

enum Ui2Tile : uint8_t {
  UI2_TILE_MESSAGES, UI2_TILE_CONTACTS, UI2_TILE_PHONE, UI2_TILE_MAP,
  UI2_TILE_HOME_HA, UI2_TILE_WEATHER, UI2_TILE_AGENDA, UI2_TILE_SETTINGS,
  UI2_TILE_READER, UI2_TILE_AUDIO, UI2_TILE_BROWSER, UI2_TILE_NOTES,
  UI2_TILE_ALARM, UI2_TILE_MAIL, UI2_TILE_GAMES, UI2_TILE_VOICE,
  UI2_TILE_DISCOVER, UI2_TILE_TRACE, UI2_TILE_LAST_HEARD, UI2_TILE_ADVERT,
  UI2_TILE_SOS,
  UI2_TILE_NONE
};

static const uint8_t UI2_HOME_TILE_PAGES = 3;
static const uint8_t UI2_HOME_TILES_PER_PAGE = 8;

static const Ui2Tile UI2_HOME_LAYOUT[UI2_HOME_TILE_PAGES][UI2_HOME_TILES_PER_PAGE] = {
  { UI2_TILE_MESSAGES,   UI2_TILE_CONTACTS, UI2_TILE_PHONE,  UI2_TILE_MAP,
    UI2_TILE_LAST_HEARD, UI2_TILE_DISCOVER, UI2_TILE_ALARM,  UI2_TILE_SETTINGS },
  { UI2_TILE_READER,     UI2_TILE_AUDIO,    UI2_TILE_BROWSER, UI2_TILE_NOTES,
    UI2_TILE_VOICE,      UI2_TILE_GAMES,    UI2_TILE_TRACE,   UI2_TILE_ADVERT },
  { UI2_TILE_SOS,        UI2_TILE_NONE,     UI2_TILE_NONE,    UI2_TILE_NONE,
    UI2_TILE_NONE,       UI2_TILE_NONE,     UI2_TILE_NONE,    UI2_TILE_NONE },
};

struct Ui2TileInfo {
  Ui2Icon icon;
  const char* label;
  const char* label_fr;
};

static inline const Ui2TileInfo& ui2TileInfo(Ui2Tile t) {
  static const Ui2TileInfo info[UI2_TILE_NONE + 1] = {
    { UI2_IC_MSG,      "Messages",    "Messages" },
    { UI2_IC_CONTACT,  "Contacts",    "Contacts" },
    { UI2_IC_PHONE,    "Phone \xC2\xB7 SMS", "T\xC3\xA9l\xC3\xA9phone" },
    { UI2_IC_MAP,      "Map",         "Carte" },
    { UI2_IC_HOME,     "Home",        "Maison" },
    { UI2_IC_PARTLY,   "Weather",     "M\xC3\xA9t\xC3\xA9o" },
    { UI2_IC_CALENDAR, "Agenda",      "Agenda" },
    { UI2_IC_SETTINGS, "Settings",    "R\xC3\xA9glages" },
    { UI2_IC_BOOK,     "Reader",      "Lecteur" },
    { UI2_IC_AUDIO,    "Audio",       "Audio" },
    { UI2_IC_GLOBE,    "Browser",     "Navigateur" },
    { UI2_IC_NOTES,    "Notes",       "Notes" },
    { UI2_IC_ALARM,    "Alarm",       "Alarme" },
    { UI2_IC_MAIL,     "Mail",        "Courriel" },
    { UI2_IC_GAMES,    "Games",       "Jeux" },
    { UI2_IC_MIC,      "Voice notes", "Notes vocales" },
    { UI2_IC_SEARCH,   "Discover",    "Recherche" },
    { UI2_IC_TRACE,    "Trace",       "Trace" },
    { UI2_IC_RADIO,    "Last heard",  "Entendus" },
    { UI2_IC_SEND,     "Send advert", "Annonce" },
    { UI2_IC_WARN,     "SOS",         "SOS" },
    { UI2_IC_COUNT,    "",            "" },
  };
  return info[t <= UI2_TILE_NONE ? t : UI2_TILE_NONE];
}

// Future features: drawn disabled, Enter or a tap only shows a toast
static inline bool ui2TileIsPlaceholder(Ui2Tile t) {
  return t == UI2_TILE_HOME_HA || t == UI2_TILE_WEATHER || t == UI2_TILE_AGENDA || t == UI2_TILE_MAIL;
}

// Whether this build has the feature behind a tile
static inline bool ui2TileAvailable(Ui2Tile t) {
  switch (t) {
    case UI2_TILE_NONE: return false;
    case UI2_TILE_PHONE:
#if defined(HAS_4G_MODEM) && !defined(MECK_40MHZ_TEST)
      return true;
#else
      return false;
#endif
    case UI2_TILE_MAP:
#if HAS_GPS && !defined(MECK_40MHZ_TEST)
      return true;
#else
      return false;
#endif
    case UI2_TILE_AUDIO:
#if (!defined(HAS_4G_MODEM) || defined(MECK_AUDIO_VARIANT)) && !defined(MECK_40MHZ_TEST)
      return true;
#else
      return false;
#endif
    case UI2_TILE_ALARM:
    case UI2_TILE_VOICE:
#if defined(MECK_AUDIO_VARIANT) && !defined(MECK_40MHZ_TEST)
      return true;
#else
      return false;
#endif
    case UI2_TILE_BROWSER:
#if defined(MECK_WEB_READER) && !defined(MECK_40MHZ_TEST)
      return true;
#else
      return false;
#endif
    default:
      return !ui2TileIsPlaceholder(t);
  }
}

// Number of tiles on a page (trailing UI2_TILE_NONE slots excluded)
static inline uint8_t ui2HomePageTileCount(uint8_t page) {
  uint8_t n = 0;
  if (page >= UI2_HOME_TILE_PAGES) return 0;
  for (uint8_t i = 0; i < UI2_HOME_TILES_PER_PAGE; i++) if (UI2_HOME_LAYOUT[page][i] != UI2_TILE_NONE) n = i + 1;
  return n;
}

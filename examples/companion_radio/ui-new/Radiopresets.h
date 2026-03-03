#pragma once

// ---------------------------------------------------------------------------
// Radio presets — shared between SettingsScreen (UI) and MyMesh (Serial CLI)
// ---------------------------------------------------------------------------

struct RadioPreset {
  const char* name;
  float freq;
  float bw;
  uint8_t sf;
  uint8_t cr;
  uint8_t tx_power;
};

static const RadioPreset RADIO_PRESETS[] = {
  { "Australia",              915.800f, 250.0f,  10, 5, 22 },
  { "Australia (Narrow)",     916.575f,  62.5f,   7, 8, 22 },
  { "Australia: SA, WA",      923.125f,  62.5f,   8, 8, 22 },
  { "Australia: QLD",         923.125f,  62.5f,   8, 5, 22 },
  { "EU/UK (Narrow)",         869.618f,  62.5f,   8, 8, 14 },
  { "EU/UK (Long Range)",     869.525f, 250.0f,  11, 5, 14 },
  { "EU/UK (Medium Range)",   869.525f, 250.0f,  10, 5, 14 },
  { "Czech Republic (Narrow)",869.432f,  62.5f,   7, 5, 14 },
  { "EU 433 (Long Range)",    433.650f, 250.0f,  11, 5, 14 },
  { "New Zealand",            917.375f, 250.0f,  11, 5, 22 },
  { "New Zealand (Narrow)",   917.375f,  62.5f,   7, 5, 22 },
  { "Portugal 433",           433.375f,  62.5f,   9, 6, 14 },
  { "Portugal 868",           869.618f,  62.5f,   7, 6, 14 },
  { "Switzerland",            869.618f,  62.5f,   8, 8, 14 },
  { "USA/Canada (Recommended)",910.525f, 62.5f,   7, 5, 22 },
  { "Vietnam",                920.250f, 250.0f,  11, 5, 22 },
};
#define NUM_RADIO_PRESETS (sizeof(RADIO_PRESETS) / sizeof(RADIO_PRESETS[0]))
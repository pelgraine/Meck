#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];
  char scope_name[31];  // Region scope name (e.g. "au-nsw"), empty = use device default
  uint8_t scope_key[16]; // Transport key derived from scope_name at set/load time (all zero = none)

  ChannelDetails() { memset(name, 0, sizeof(name)); memset(scope_name, 0, sizeof(scope_name)); memset(scope_key, 0, sizeof(scope_key)); }
};
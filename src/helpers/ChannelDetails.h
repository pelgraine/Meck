#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];
  char scope_name[31];  // Region scope name (e.g. "au-nsw"), empty = use device default

  ChannelDetails() { memset(name, 0, sizeof(name)); memset(scope_name, 0, sizeof(scope_name)); }
};
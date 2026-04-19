#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];
  char scope_name[31];  // Region scope name (e.g. "au-nsw"), empty = use device default
};
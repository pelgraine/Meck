#pragma once

#include <Utils.h>
#include "LoRagotchiChannelStatus.h"
#include <cstring>

// Companion-local policy; the shared MeshCore channel/storage APIs stay unchanged.
namespace LoRagotchiChannel {

static constexpr char NAME[] = "#loragotchi-dx";

template <typename Mesh>
Result ensureJoined(Mesh& host) {
  ChannelDetails wanted{};
  memcpy(wanted.name, NAME, sizeof(NAME));
  // Standard MeshCore hashtag key: first 16 bytes of SHA256, including '#'.
  // The zero-filled upper half makes setChannel() derive the 128-bit key hash.
  mesh::Utils::sha256(wanted.channel.secret, 16,
                     reinterpret_cast<const uint8_t*>(NAME), sizeof(NAME) - 1);

  int vacant = -1;
  bool nameConflict = false;
  const uint8_t emptySecret[sizeof(wanted.channel.secret)] = {};
  ChannelDetails current;
  for (int i = 0; host.getChannel(i, current); ++i) {
    // Membership is determined by the complete key, not a one-byte radio hash
    // or a display label. An existing alias keeps its slot/name/key.
    if (current.name[0] &&
        memcmp(current.channel.secret, wanted.channel.secret, sizeof(emptySecret)) == 0) {
      return Result::AlreadyJoined;
    }
    if (memcmp(current.name, NAME, sizeof(NAME)) == 0) nameConflict = true;
    // Preserve unnamed slots with nonzero keys as well as every named channel.
    if (vacant < 0 && !current.name[0] &&
        memcmp(current.channel.secret, emptySecret, sizeof(emptySecret)) == 0) {
      vacant = i;
    }
  }
  // Never overwrite a same-named private channel or an occupied slot.
  if (vacant < 0) return Result::Full;
  if (nameConflict) return Result::NameConflict;
  // addChannel() tracks defaults, not slots restored by loadChannels().
  // Using it here could overwrite a saved channel. Use the actual vacant index.
  if (!host.setChannel(vacant, wanted)) return Result::SetFailed;
  return Result::Added;
}

}  // namespace LoRagotchiChannel

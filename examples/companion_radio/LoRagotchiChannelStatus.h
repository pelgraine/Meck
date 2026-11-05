#pragma once

namespace LoRagotchiChannel {
enum class Result { AlreadyJoined, Added, Full, NameConflict, SetFailed };
// Written once by MyMesh::begin(), before the UI starts. No channel mutation
// occurs when a notice is rendered or dismissed.
extern Result startupResult;
}

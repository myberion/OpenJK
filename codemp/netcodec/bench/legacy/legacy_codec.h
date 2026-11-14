// The legacy (1.01 / OpenJK) snapshot coding, for the benchmark only: OpenJK's own codemp/qcommon/msg.cpp and
// huffman.cpp, driven as the engine drives them (server: SV_SendClientSnapshot, SV_WriteSnapshotToClient,
// SV_EmitPacketEntities; client: CL_ParseServerMessage, CL_ParseSnapshot, CL_ParsePacketEntities).
// No engine type appears here: states are byte images of entityState_t and playerState_t, as in netcodec.
#pragma once

#include <cstddef>
#include <cstdint>

namespace legacy {

constexpr size_t kEntityBytes = 532;
constexpr size_t kPlayerBytes = 1552;
constexpr int kMaxEntities = 1024;

struct SnapshotView {
	const uint8_t *ps = nullptr;
	const uint8_t *vps = nullptr;              // nullptr: no vehicle player state (zeros if the ps has a vehicle)
	const uint8_t *const *entities = nullptr;  // entity images in number order, the number field set
	int numEntities = 0;
};

struct Command {
	int32_t seq = 0;
	const char *text = nullptr;
};

struct MessageView {
	int32_t ack = 0;
	const Command *commands = nullptr;
	int numCommands = 0;
	bool hasSnapshot = false;
	int32_t serverTime = 0;
	int delta = 0;
	int flags = 0;
	const uint8_t *areas = nullptr;
	int numAreas = 0;
	SnapshotView snap;
};

// Snapshot written by the decoder: buffers owned by the caller.
struct SnapshotOut {
	uint8_t *ps = nullptr;             // kPlayerBytes
	uint8_t *vps = nullptr;            // kPlayerBytes
	uint8_t *entities = nullptr;       // kMaxEntities * kEntityBytes
	int numEntities = 0;
};

// Encodes one server message (ack, commands, snapshot, svc_EOF, Huffman as the engine does); returns its size in
// bytes. base: the snapshot the client acknowledged (delta from it), or nullptr. baselines: kMaxEntities images.
int Encode(const MessageView &msg, const SnapshotView *base, const uint8_t *const *baselines, uint8_t *out, int capacity);

// Decodes one message as the client does; false on a malformed message. base: the client's copy of the base.
bool Decode(const uint8_t *data, int size, const SnapshotView *base, const uint8_t *const *baselines, SnapshotOut &out);

// Client packets, before the netchan obfuscation: written as CL_WritePacket and CL_Netchan_Transmit (its clc_EOF) do,
// read as SV_ExecuteClientMessage, SV_ClientCommand and SV_UserMove do. User commands are byte images of usercmd_t.
constexpr size_t kUserCmdBytes = 28;
constexpr int kMaxUserCmds = 32;              // MAX_PACKET_USERCMDS

enum class Move { kNone, kDelta, kNoDelta };  // no user command, clc_move, clc_moveNoDelta

struct ClientPacketView {
	int32_t serverId = 0;
	int32_t messageAck = 0;
	int32_t reliableAck = 0;
	const Command *commands = nullptr;        // the client commands not yet acknowledged
	int numCommands = 0;
	Move move = Move::kNone;
	const uint8_t *userCmds = nullptr;        // numUserCmds images, each delta-coded against the previous one
	int numUserCmds = 0;
	int32_t key = 0;                          // the user-command key
};

// A decoded client packet; the command texts point into text. Bit counts of the sections, for where the bytes go.
struct ClientPacketOut {
	static constexpr int kMaxCommands = 256;
	int32_t serverId = 0;
	int32_t messageAck = 0;
	int32_t reliableAck = 0;
	Command commands[kMaxCommands];
	int numCommands = 0;
	char text[65536];
	Move move = Move::kNone;
	uint8_t userCmds[kMaxUserCmds * kUserCmdBytes];
	int numUserCmds = 0;
	int headerBits = 0;                       // the three acknowledges
	int commandBits = 0;                      // the client commands
	int moveBits = 0;                         // the move byte, the count and the user commands, or the final clc_EOF
};

// Encodes one client packet; returns its size in bytes (0 on overflow).
int EncodeClientPacket(const ClientPacketView &p, uint8_t *out, int capacity);

// Decodes one client packet; false where the server would stop early or on a malformed packet.
bool DecodeClientPacket(const uint8_t *data, int size, int32_t key, ClientPacketOut &out);

} // namespace legacy

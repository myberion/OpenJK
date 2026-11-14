// netcodec: the engine's interface to the message codec (server to client), and to the client packets (client to
// server, at the end).
//
// Compiled by the engine's own translation units, so this header is C++11: plain structures, pointers and sizes. The
// library converts them to the views of codec.h and reads and writes the engine's arrays in place:
// - entity states are entityState_t images (532 bytes) holding their number at offset 0, in the engine's rings
//   (svs.snapshotEntities on the server, cl.parseEntities on the client);
// - player states are playerState_t images (1,552 bytes);
// - entity baselines are found by number at number * stride (sv.svEntities[n].baseline, cl.entityBaselines[n]); a
//   baseline is present when its number field holds its number, so number 0 never has one;
// - the server commands are the engine's ring of NUL-terminated texts (client_t::reliableCommands,
//   clientConnection_t::serverCommands), which must hold at least kMinCommands commands.
//
// Both sides keep one Frame per message slot (kPacketBackup of them, slot seq % kPacketBackup) next to the engine's
// own frames; a frame describes a message that held a snapshot. A message is coded against a base (the snapshot of an
// earlier message) and against that base's own base when both sides still hold it: the base's own base is used only
// if it is fewer than kPacketBackup messages before the message being coded; on the server its entities must also
// still be in the shared ring, otherwise the message is coded without a base (the client cannot know).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace netcodec {   // NOLINT(modernize-concat-nested-namespaces): this header is C++11
namespace engine {

const uint32_t kPacketBackup = 128;         // PACKET_BACKUP: message slots kept by both sides
const uint32_t kMinCommands = 256;          // commands both sides keep at least (MAX_RELIABLE_COMMANDS)
const uint32_t kCommandWindow = 127;        // a server command is coded against an earlier one at most this far back
const uint32_t kMaxSnapshotEntities = 256;  // MAX_SNAPSHOT_ENTITIES
const uint32_t kMaxEntities = 1024;         // MAX_GENTITIES: entity numbers and baselines
const uint32_t kAckWindow = 256;            // client packets: the acknowledged message travels as its low 8 bits
const uint32_t kMaxUserCmds = 32;           // MAX_PACKET_USERCMDS
const size_t kUserCmdBytes = 28;            // sizeof(usercmd_t)
const uint32_t kMaxCommandLength = 1023;    // a client command's text: MAX_STRING_CHARS - 1 (longer: not coded)

struct PacketBase;     // client packets, at the end
struct ClientPacket;

// Entity states one after the other, used as a ring: the entity i of a snapshot whose first entity is `first` (an
// absolute index, as the engine counts) is state (first + i) % count.
struct EntityRing {
	const void *states;
	uint32_t count;
};

// What both sides keep about a message with a snapshot, for the messages coded against it. Filled by the codec.
struct Record {
	int64_t ack;
	int64_t cmdSeq;      // the client holds every server command up to this one
	int64_t time;        // the snapshot's server time
	int32_t olderSeq;    // the message this one was coded against, -1 if none
	int32_t serverId;    // the message's serverId
};

// One message slot: the message seq with a snapshot, its player states and its entities in the ring, its Record.
struct Frame {
	int32_t seq;          // -1: the slot holds no usable snapshot
	const void *ps;       // playerState_t
	const void *vps;      // the vehicle's playerState_t, nullptr when ps.m_iVehicleNum is 0
	int64_t firstEntity;  // absolute index in the ring of the snapshot's first entity (the others follow it), or, with
	                      // an index, the nextEntity of the time the snapshot was made (the server holds the snapshot's
	                      // states while nextEntity is less than the ring's count past it)
	uint32_t numEntities;
	const uint32_t *index; // nullptr, or numEntities state numbers of the ring: entity i is state index[i] (the server
	                       // keeps one copy of an entity's state while it does not change, shared by the snapshots that
	                       // hold it: the same state number in a snapshot and in its base is an unchanged entity)
	Record record;
};

// The engine's command ring: command seq is the text at texts + (seq % count) * stride (NUL-terminated within
// stride bytes); the ring holds the commands last - count + 1 to last (and none below 1).
struct CommandRing {
	const char *texts;
	uint32_t stride;
	uint32_t count;       // at least kMinCommands
	int32_t last;
};

// Entity baselines by number: the baseline of number n at bytes + n * stride, for n below kMaxEntities.
struct Baselines {
	const void *bytes;
	size_t stride;
};

// One server message, in the engine's order: commands, gamestate, setgame, snapshot, download, map change.
struct ServerMessage {
	int32_t seq;              // netchan sequence of this message
	int32_t ack;              // the last client command the server received
	int32_t serverId;         // sv.serverId (the client's own serverId is coded against it, step 8)
	int32_t firstCommand;     // the commands firstCommand to commands.last are sent (none if above it)

	bool gameState;           // gamestate: config strings, the baselines of the encoder call, client number, checksum
	int32_t relSeq;           // the reliable sequence sent with the gamestate
	const char *const *configStrings;   // by index; null or empty: not sent
	uint32_t numConfigStrings;
	int32_t clientNum;
	int32_t checksum;

	const char *setGame;      // nullptr: none

	bool snapshot;            // the snapshot: its player states and entities are a Frame, coded against the
	uint32_t delta;           // message seq - delta (0: no base; the codec may code it without a base)
	int32_t time;
	uint32_t flags;
	int32_t inputMargin;      // how early the client's command of the snapshot's tick reached the server, in ms
	                          // (negative: after its tick); sent as -128 to 127
	const uint8_t *areas;
	uint32_t numAreas;
	Frame frame;              // seq, record: unused

	bool download;
	uint32_t block;
	int32_t fileSize;         // block 0 only
	const uint8_t *data;
	uint32_t dataSize;

	bool mapChange;
};

class ServerCodec {
public:
	ServerCodec();
	~ServerCodec();
	ServerCodec(const ServerCodec &) = delete;
	ServerCodec &operator=(const ServerCodec &) = delete;
	ServerCodec(ServerCodec &&) = delete;
	ServerCodec &operator=(ServerCodec &&) = delete;

	// Codes msg into out (outSize bytes, MAX_MSGLEN); returns its size, or 0 if it does not fit or is malformed.
	// frames: the client's kPacketBackup slots; ring: svs.snapshotEntities, of which nextEntity is the next index
	// the server will write (svs.nextSnapshotEntities); baselines: the current gamestate's. delta: the base used
	// (msg.delta, or 0 when the codec had to code without a base); record: to keep in this message's Frame.
	size_t Encode(const ServerMessage &msg, const Frame *frames, const EntityRing &ring, int64_t nextEntity,
	              const CommandRing &commands, const Baselines &baselines, uint8_t *out, size_t outSize, uint32_t &delta,
	              Record &record);

	// Whether the last Encode sent the message's raw form (a message coded larger than 1 KB is also coded raw and
	// the smaller form is sent); for statistics.
	bool Escaped() const;   // NOLINT(modernize-use-nodiscard): this header is C++11

	// Decodes a client packet (see ClientPacket below). latest: the last message sent to this client
	// (netchan.outgoingSequence - 1); bases: its kAckWindow PacketBases. Returns false if the packet is malformed or
	// coded against a message whose PacketBase is no longer held: drop it.
	bool DecodePacket(const uint8_t *bytes, size_t size, int32_t latest, const PacketBase *bases,
	                  ClientPacket &packet);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Where the client decodes: the snapshot's player states (the frame slot of the message), its entities in the
// ring from firstEntity on (cl.parseEntitiesNum), at most kMaxSnapshotEntities, and the baselines (also the current
// ones).
struct ClientTarget {
	void *ps;
	void *vps;
	void *ring;
	uint32_t ringCount;
	int32_t firstEntity;
	void *baselines;
	size_t baselineStride;
};

struct Command {
	int32_t seq;
	const char *text;     // NUL-terminated
	uint32_t length;
};

struct ConfigString {
	uint32_t index;
	const char *text;     // NUL-terminated
	uint32_t length;
};

// A decoded message. The pointers stay valid until the next Decode of the same ClientCodec.
struct ClientMessage {
	int32_t ack;
	int32_t serverId;
	uint32_t delta;                     // the base: message seq - delta, 0 = none
	const Command *commands;
	uint32_t numCommands;

	bool gameState;
	int32_t relSeq;
	const ConfigString *configStrings;  // by increasing index
	uint32_t numConfigStrings;
	int32_t clientNum;
	int32_t checksum;

	const char *setGame;                // nullptr: none

	bool snapshot;
	int32_t time;
	uint32_t flags;
	int32_t inputMargin;
	const uint8_t *areas;
	uint32_t numAreas;
	bool vehicle;                       // the vehicle player state was written
	uint32_t numEntities;
	Record record;                      // to keep in this message's Frame

	bool download;
	uint32_t block;
	int32_t fileSize;
	const uint8_t *data;
	uint32_t dataSize;

	bool mapChange;
};

class ClientCodec {
public:
	ClientCodec();
	~ClientCodec();
	ClientCodec(const ClientCodec &) = delete;
	ClientCodec &operator=(const ClientCodec &) = delete;
	ClientCodec(ClientCodec &&) = delete;
	ClientCodec &operator=(ClientCodec &&) = delete;

	// Decodes message seq. frames: the client's kPacketBackup slots; ring: cl.parseEntities; commands: the commands
	// the client holds. Returns false if the message is malformed or its base is not held (then drop it; a
	// gamestate's baselines may have been written already).
	bool Decode(const uint8_t *bytes, size_t size, int32_t seq, const Frame *frames, const EntityRing &ring,
	            const CommandRing &commands, const ClientTarget &target, ClientMessage &msg);

	// Codes the client packet p into out (outSize bytes); returns its size, or 0 if p is malformed or does not fit.
	// base: the PacketBase of the message p acknowledges (base->seq == p.ack), or nullptr to code p without it.
	size_t EncodePacket(const ClientPacket &p, const PacketBase *base, uint8_t *out, size_t outSize);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// ------------------------------------------------------------------------------------------------------------------
// Client packets (client to server): the acknowledges, the client commands not yet acknowledged and the user commands.
// A packet decodes alone. Its user commands are coded against the message it acknowledges, when the client says so:
// the server must then still hold that message's PacketBase, so it keeps one for each of its last kAckWindow messages
// to the client (slot seq % kAckWindow; the acknowledge is read unambiguously over the same window).

// What a packet is coded against: the values of an acknowledged message (record and player state, PacketBaseOf).
struct PacketBase {
	int32_t seq;                      // -1: the slot holds no message with a snapshot
	int32_t serverId;
	int32_t cmdSeq;
	int32_t ack;
	int32_t commandTime;
	std::array<uint16_t, 3> angles;   // the view angle indices less delta_angles
	uint8_t weapon;
	uint8_t forceSelected;
	uint8_t holdable;
};

// The PacketBase of a frame that holds a snapshot (seq, ps and record set); the same on both sides.
PacketBase PacketBaseOf(const Frame &frame);

enum class Move : uint8_t { kNone = 0, kDelta = 1, kNoDelta = 2 };   // no user command, clc_move, clc_moveNoDelta

// One client packet. Encoded: the texts and the user commands are the caller's. Decoded: they point into the
// ServerCodec until its next DecodePacket.
struct ClientPacket {
	int32_t serverId;
	int32_t ack;              // the last server message received
	int32_t reliableAck;      // the last server command received
	const Command *commands;  // consecutive sequence numbers, at most kMinCommands, texts of at most kMaxCommandLength
	uint32_t numCommands;
	Move move;
	const void *userCmds;     // usercmd_t images one after the other: 1 to kMaxUserCmds with a move, none without
	uint32_t numUserCmds;
};

} // namespace engine
} // namespace netcodec

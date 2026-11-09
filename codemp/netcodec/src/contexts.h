// netcodec internals: the contexts of the format. Every symbol is coded with the table of one context; the contexts
// are numbered here from the schema, so that the coder, the model and the trainer share one definition.
//
// Layout, in this order:
//  - per entity field (codec order) and per player-state field name: one context per kind (fctx), unused kinds have
//    an empty alphabet;
//  - index of the next changed field given the previous one: player, pilot and vehicle tables, then entities per class;
//  - entity operation given the previous one; entity type changed and new type, per class of the base;
//  - message operation given the previous one; the scalar contexts of the message (enum Scalar);
//  - bytes of server command text given the previous byte (0 at the start of a text), also used by client commands;
//  - the message's serverId unchanged from the base;
//  - client packets (client to server, plan step 8): their scalars (enum ClientScalar), the user-command time step
//    given the previous step, the view angle differences per axis and context, the byte fields given the previous
//    value (separately for the packet's first command, which has no previous command).
#pragma once

#include "schema.h"

namespace netcodec::detail::ctx {

inline constexpr uint32_t kKinds = ToU32(fctx::kCount);   // contexts per field
inline constexpr uint32_t kEntityClassCount = kEntityClasses;
inline constexpr uint32_t kEntityGapAlphabet = kNumEntityFields + 1;   // a field index, or the end of the list
inline constexpr uint32_t kOpAlphabet = 8;

// the player-state tables, in the order of the pilot flag and the vehicle state
inline constexpr std::array<std::span<const FieldDesc>, 3> kPlayerTables{ kPlayerFields, kPilotFields, kVehicleFields };

// Server commands (see PutCommand): the distance to the reference command, the difference of the token counts, the
// "same token" flag given the previous token's flag and whether the reference token is a number, the kind of a new
// token given whether the reference token is a number, number differences and numbers (their text is coded with the
// CommandText contexts).
enum class Scalar : uint8_t {
	kMDelta, kMAck, kMCmdSeq, kMRelSeq, kMNumCs, kMCsGap, kMClientNum, kMDlBlock, kMDlSize, kMDlLen, kDlByte, kHTime,
	kHFlags, kHArea, kPsPilot, kStrSetgame, kStrCs, kEntGap,
	kCmdRef, kCmdTokens, kCmdSame0, kCmdSame1, kCmdSame2, kCmdSame3, kCmdKindRefNum, kCmdKindOther, kCmdNumDelta, kCmdNum,
	kScalarCount
};
// alphabets of the scalars: 36 for a class of a value below 2^35 (zigzag differences), 65 for any 64-bit value,
// 256 for bytes; the message's first symbol is its base delta (0 to 255) plus 256 when it is escaped
inline constexpr std::array<uint16_t, static_cast<size_t>(Scalar::kScalarCount)> kScalarAlphabet{
	512, 36, 36, 36, 16, 16, 36, 20, 36, 20, 256, 36, 256, 256, 2, 256, 256, 42,
	8, 36, 2, 2, 2, 2, 2, 2, 65, 65 };

inline constexpr uint32_t kEntityField = 0;
inline constexpr uint32_t kPlayerField = kEntityField + (kNumEntityFields * kKinds);
inline constexpr uint32_t kPlayerGap = kPlayerField + (kPlayerNames * kKinds);
inline constexpr std::array<uint32_t, 3> kPlayerGapStart{
	kPlayerGap, kPlayerGap + ToU32(kPlayerFields.size()) + 1,
	kPlayerGap + ToU32(kPlayerFields.size()) + 1 + ToU32(kPilotFields.size()) + 1 };
inline constexpr uint32_t kEntityGap = kPlayerGapStart[2] + ToU32(kVehicleFields.size()) + 1;
inline constexpr uint32_t kEntityOp = kEntityGap + (kEntityClassCount * kEntityGapAlphabet);
inline constexpr uint32_t kTypeChanged = kEntityOp + 4;
inline constexpr uint32_t kTypeValue = kTypeChanged + kEntityClassCount;
inline constexpr uint32_t kOp = kTypeValue + kEntityClassCount;
inline constexpr uint32_t kScalar = kOp + kOpAlphabet;
inline constexpr uint32_t kCommandText = kScalar + static_cast<uint32_t>(Scalar::kScalarCount);
inline constexpr uint32_t kServerIdSame = kCommandText + 256;

// Client packets (see PutClientPacket): the base flag, serverId unchanged, the server command acknowledge, the client
// command count and first sequence number, the move operation, the user-command count, the first command's time,
// an escaped time step.
enum class ClientScalar : uint8_t {
	kBase, kSameServerId, kRelAck, kNumCommands, kCommandSeq, kMove, kNumUserCmds, kTime0, kStepEscape, kCount
};
// zigzag classes of values below 2^35 (36); moves (3); user-command counts 0 to 32 (33); signed classes of 32-bit
// differences (65)
inline constexpr std::array<uint16_t, static_cast<size_t>(ClientScalar::kCount)> kClientScalarAlphabet{
	2, 2, 36, 36, 36, 3, 33, 65, 36 };
inline constexpr uint32_t kStepAlphabet = 64;        // time steps 0 to 62, 63 escaped
inline constexpr uint32_t kStepContexts = 65;        // the previous step's symbol, 64 for the second command
inline constexpr uint32_t kAngleAlphabet = 33;       // signed classes of 16-bit differences
inline constexpr uint32_t kAngleContexts = 36;       // per axis: first command with and without base, second, then
                                                     // the previous difference's signed class
inline constexpr uint32_t kUserCmdFields = 9;        // buttons (two bytes), weapon, forcesel, invensel, generic_cmd,
                                                     // forwardmove, rightmove, upmove
inline constexpr uint32_t kClientScalar = kServerIdSame + 1;
inline constexpr uint32_t kClientStep = kClientScalar + static_cast<uint32_t>(ClientScalar::kCount);
inline constexpr uint32_t kClientAngle = kClientStep + kStepContexts;
inline constexpr uint32_t kClientField = kClientAngle + (3 * kAngleContexts);
inline constexpr uint32_t kCount = kClientField + (kUserCmdFields * 2 * 256);

// The contexts of one field: row[kind] is the context of that kind.
struct Row {
	uint32_t first = 0;
	[[nodiscard]] constexpr uint32_t operator[](size_t kind) const noexcept { return first + ToU32(kind); }
};

[[nodiscard]] constexpr Row EntityField(uint32_t row) noexcept { return Row{ kEntityField + (row * kKinds) }; }
[[nodiscard]] constexpr Row PlayerField(uint32_t row) noexcept { return Row{ kPlayerField + (row * kKinds) }; }
// gap contexts: g + (previous changed field + 1)
[[nodiscard]] constexpr uint32_t PlayerGap(uint32_t table) noexcept { return kPlayerGapStart[table]; }
[[nodiscard]] constexpr uint32_t EntityGap(uint32_t cls) noexcept { return kEntityGap + (cls * kEntityGapAlphabet); }
[[nodiscard]] constexpr uint32_t Of(Scalar s) noexcept { return kScalar + static_cast<uint32_t>(s); }
[[nodiscard]] constexpr uint32_t CommandText(uint32_t previousByte) noexcept { return kCommandText + previousByte; }

// alphabet of a field kind: absolute integers up to 8 bits directly, wider ones and differences by class (bit
// length, 0 to the width, plus one for a sign); float kind, integral float classes (16), angle index differences (24),
// 32-bit differences and predictions (36), second-order angles (40), boolean flip
[[nodiscard]] constexpr uint32_t KindAlphabet(size_t kind, int bits, CType ctype) noexcept {
	const auto ab = static_cast<uint32_t>(bits < 0 ? -bits : bits);
	const bool isInt = bits != 0;
	switch (kind) {
	case fctx::kA:
		if (!isInt) return 0;
		return ab <= 8 ? 1U << ab : ab + 2;
	case fctx::kD: return isInt ? ab + 2 : 0;
	case fctx::kT: return isInt ? 36 : 0;
	case fctx::kK: return isInt ? 0 : 4;
	case fctx::kIA:
	case fctx::kID: return isInt ? 0 : 16;
	case fctx::kAD: return isInt ? 0 : 24;
	case fctx::kFD:
	case fctx::kFP: return isInt ? 0 : 36;
	case fctx::kA2: return isInt ? 0 : 40;
	case fctx::kB: return ctype == CType::kBool ? 2 : 0;
	default: return 0;
	}
}

// the descriptor of player-state field name `row` (the same in every table that has it)
[[nodiscard]] constexpr const FieldDesc *PlayerName(uint32_t row) noexcept {
	for (const std::span<const FieldDesc> t : kPlayerTables)
		for (const FieldDesc &f : t)
			if (f.ctx == row) return &f;
	return nullptr;
}

// number of symbols of a context (0: never coded)
[[nodiscard]] constexpr uint32_t Alphabet(uint32_t c) noexcept {
	if (c < kPlayerField) {
		const FieldDesc &f = kEntityFields[(c - kEntityField) / kKinds];
		return KindAlphabet((c - kEntityField) % kKinds, f.bits, f.ctype);
	}
	if (c < kPlayerGap) {
		const FieldDesc *f = PlayerName((c - kPlayerField) / kKinds);
		return f != nullptr ? KindAlphabet((c - kPlayerField) % kKinds, f->bits, f->ctype) : 0;
	}
	for (uint32_t t = 0; t < 3; t++)
		if (c < (t < 2 ? kPlayerGapStart[t + 1] : kEntityGap)) return ToU32(kPlayerTables[t].size()) + 1;
	if (c < kEntityOp) return kEntityGapAlphabet;
	if (c < kTypeChanged) return 4;
	if (c < kTypeValue) return 2;
	if (c < kOp) return 256;
	if (c < kScalar) return kOpAlphabet;
	if (c < kCommandText) return kScalarAlphabet[c - kScalar];
	if (c < kServerIdSame) return 256;
	if (c < kClientScalar) return 2;
	if (c < kClientStep) return kClientScalarAlphabet[c - kClientScalar];
	if (c < kClientAngle) return kStepAlphabet;
	if (c < kClientField) return kAngleAlphabet;
	return c < kCount ? 256 : 0;
}

[[nodiscard]] constexpr uint32_t Of(ClientScalar s) noexcept { return kClientScalar + static_cast<uint32_t>(s); }
// previous: the previous step's symbol, or kStepContexts - 1 for the packet's second command
[[nodiscard]] constexpr uint32_t ClientStep(uint32_t previous) noexcept { return kClientStep + previous; }
[[nodiscard]] constexpr uint32_t ClientAngle(uint32_t axis, uint32_t k) noexcept {
	return kClientAngle + (axis * kAngleContexts) + k;
}
[[nodiscard]] constexpr uint32_t ClientField(uint32_t field, bool first, uint32_t previous) noexcept {
	return kClientField + (((field * 2) + (first ? 1U : 0U)) * 256) + previous;
}

} // namespace netcodec::detail::ctx

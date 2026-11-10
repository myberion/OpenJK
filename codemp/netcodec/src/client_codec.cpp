// netcodec: coding of the client packets (client to server, plan step 8; interface in codec.h). In order:
//  - the acknowledged message, its low 8 bits raw; whether the user commands are coded against it (base);
//  - with a base: serverId unchanged from the acknowledged message (else raw), the server command acknowledge against
//    the commands that message gave the client (its record's cmdSeq); without a base: both raw;
//  - the client commands: their count, the first sequence number against the one after the last client command the
//    acknowledged message said the server had (its record's ack; raw without a base), each text as a server command
//    text (the same contexts);
//  - the move operation; the user-command count; per command: the time (the first against the base player state's
//    commandTime as a signed class, raw without a base; the others as the step from the previous command, given the
//    previous step), the three view angles as 16-bit differences (signed class) from the previous command (the first
//    from the base's view angle index less its delta_angles, or 0) given the previous difference, then the bytes
//    buttons (low, high), weapon, forcesel, invensel, generic_cmd, forwardmove, rightmove, upmove given the previous
//    command's (the first given the base's weapon, forcePowerSelected and holdable item, or zeros).
#include "coder.h"
#include "numeric.h"

#include <bit>
#include <cstring>

namespace netcodec {

namespace detail {
namespace {

using ctx::Of;
using enum ctx::ClientScalar;

// playerState_t offsets (the engine checks them, sv_netcodec.cpp and cl_netcodec.cpp)
constexpr size_t kPsCommandTime = 0, kPsDeltaAngles = 80, kPsWeapon = 176, kPsViewAngles = 184, kPsHoldable = 248,
                 kPsForceSelected = 884;
// usercmd_t offsets
constexpr size_t kCmdTime = 0, kCmdAngles = 4, kCmdButtons = 16, kCmdBytes = 20;
constexpr uint32_t kStepEscaped = ctx::kStepAlphabet - 1;
constexpr uint32_t kAfterFirst = ctx::kStepContexts - 1;   // step context of the second command

using Bytes = std::array<uint8_t, ctx::kUserCmdFields>;

[[nodiscard]] int32_t Load32(std::span<const uint8_t> img, size_t offset) noexcept {
	int32_t v = 0;
	std::memcpy(&v, img.subspan(offset, 4).data(), 4);
	return v;
}
void Store32(std::span<uint8_t> img, size_t offset, int32_t v) noexcept { std::memcpy(img.subspan(offset, 4).data(), &v, 4); }

[[nodiscard]] Bytes FieldBytes(const UserCmd &c) noexcept {
	const auto b = static_cast<uint32_t>(Load32(c, kCmdButtons));
	return { static_cast<uint8_t>(b), static_cast<uint8_t>(b >> 8U), c[kCmdBytes], c[kCmdBytes + 1], c[kCmdBytes + 2],
	         c[kCmdBytes + 3], c[kCmdBytes + 4], c[kCmdBytes + 5], c[kCmdBytes + 6] };
}

// a 16-bit angle difference, in [-32768, 32767]
[[nodiscard]] int64_t AngleDelta(uint32_t v, uint32_t pred) noexcept {
	return static_cast<int64_t>((v - pred + 0x8000U) & 0xFFFFU) - 0x8000;
}

// signed class: 0, or twice the bit length of |d|, less 1 when d > 0; then the bits below the leading one raw.
// Returns the symbol.
template <class O> uint32_t PutSigned(O &io, uint32_t context, int64_t d) noexcept {
	const uint64_t a = d < 0 ? static_cast<uint64_t>(-d) : static_cast<uint64_t>(d);   // |d| < 2^32 here
	const auto c = static_cast<uint32_t>(std::bit_width(a));
	const uint32_t s = c == 0 ? 0 : (2 * c) - (d > 0 ? 1U : 0U);
	io.Symbol(context, s);
	if (c > 1) io.Raw(a - (uint64_t{ 1 } << (c - 1)), c - 1);
	return s;
}

[[nodiscard]] int64_t GetSigned(In &io, uint32_t context, uint32_t &s) noexcept {
	s = io.Symbol(context);
	if (s == 0) return 0;
	const uint32_t c = (s + 1) / 2;
	const uint64_t a = c > 1 ? (uint64_t{ 1 } << (c - 1)) | io.Raw(c - 1) : 1;
	return (s & 1U) != 0 ? static_cast<int64_t>(a) : -static_cast<int64_t>(a);
}

// angle context: the first command with or without a base, the second, then the previous difference's signed class
[[nodiscard]] uint32_t AngleContext(size_t i, bool hasBase, uint32_t prevClass) noexcept {
	if (i == 0) return hasBase ? 0U : 1U;
	return i == 1 ? 2U : 3 + prevClass;
}

// what a packet is coded against, from the acknowledged message
struct BaseValues {
	bool has = false;
	int64_t serverId = 0, cmdSeq = 0, ack = 0, commandTime = 0;
	std::array<uint32_t, 3> angles{};
	Bytes first{};   // the bytes the first command is coded against
};

[[nodiscard]] BaseValues Values(const ClientBase *b) noexcept {
	BaseValues v;
	if (b == nullptr) return v;
	v.has = true;
	v.serverId = b->serverId;
	v.cmdSeq = b->cmdSeq;
	v.ack = b->ack;
	v.commandTime = b->commandTime;
	v.angles = b->angles;
	v.first[2] = b->weapon;
	v.first[3] = b->forceSelected;
	v.first[4] = b->holdable;
	return v;
}

[[nodiscard]] bool Valid(const ClientPacket &p) noexcept {
	if (p.commands.size() > kMaxClientCommands) return false;
	for (size_t i = 1; i < p.commands.size(); i++)
		if (p.commands[i].seq != p.commands[i - 1].seq + 1) return false;
	if (p.move == Move::kNone) return p.userCmds.empty();
	return p.move <= Move::kNoDelta && !p.userCmds.empty() && p.userCmds.size() <= kMaxUserCmds;
}

static_assert(kAckWindow == 256, "the acknowledged message travels as its low 8 bits");

template <class O> void PutClientPacket(O &io, const ClientPacket &p, const BaseValues &b) {
	io.Raw(static_cast<uint64_t>(p.ack) & (kAckWindow - 1), 8);
	io.Symbol(Of(kBase), b.has ? 1 : 0);
	if (b.has) {
		const bool same = p.serverId == b.serverId;
		io.Symbol(Of(kSameServerId), same ? 1 : 0);
		if (!same) io.Raw(static_cast<uint32_t>(p.serverId), 32);
		io.Class(Of(kRelAck), Zz(p.reliableAck - b.cmdSeq));
	} else {
		io.Raw(static_cast<uint32_t>(p.serverId), 32);
		io.Raw(static_cast<uint32_t>(p.reliableAck), 32);
	}
	io.Class(Of(kNumCommands), p.commands.size());
	if (!p.commands.empty()) {
		if (b.has) io.Class(Of(kCommandSeq), Zz(p.commands.front().seq - (b.ack + 1)));
		else io.Raw(static_cast<uint32_t>(p.commands.front().seq), 32);
		for (const ClientCommand &c : p.commands) PutCommandText(io, c.text);
	}
	io.Symbol(Of(kMove), static_cast<uint32_t>(p.move));
	if (p.move == Move::kNone) return;
	io.Symbol(Of(kNumUserCmds), p.userCmds.size());
	std::array<uint32_t, 3> prevAngle = b.angles;
	std::array<uint32_t, 3> prevClass{};
	Bytes prevBytes = b.first;
	int64_t prevTime = 0;
	uint32_t prevStep = kAfterFirst;
	for (size_t i = 0; i < p.userCmds.size(); i++) {
		const UserCmd &c = p.userCmds[i];
		const int64_t t = Load32(c, kCmdTime);
		if (i == 0) {
			if (b.has) PutSigned(io, Of(kTime0), t - b.commandTime);
			else io.Raw(static_cast<uint32_t>(t), 32);
		} else {
			const int64_t dt = t - prevTime;
			const uint32_t s = dt >= 0 && dt < int64_t{ kStepEscaped } ? static_cast<uint32_t>(dt) : kStepEscaped;
			io.Symbol(ctx::ClientStep(prevStep), s);
			if (s == kStepEscaped) io.Class(Of(kStepEscape), Zz(dt));
			prevStep = s;
		}
		prevTime = t;
		for (uint32_t a = 0; a < 3; a++) {
			const uint32_t v = static_cast<uint32_t>(Load32(c, kCmdAngles + (size_t{ 4 } * a))) & 0xFFFFU;
			const uint32_t k = AngleContext(i, b.has, prevClass[a]);
			prevClass[a] = PutSigned(io, ctx::ClientAngle(a, k), AngleDelta(v, prevAngle[a]));
			prevAngle[a] = v;
		}
		const Bytes bytes = FieldBytes(c);
		for (uint32_t k = 0; k < ctx::kUserCmdFields; k++) io.Symbol(ctx::ClientField(k, i == 0, prevBytes[k]), bytes[k]);
		prevBytes = bytes;
	}
}

bool GetClientPacket(In &io, int64_t latest, const ClientBaseLookup &lookup, ClientPacket &p) {
	const uint64_t low = io.Raw(8);
	p.ack = latest - static_cast<int64_t>((static_cast<uint64_t>(latest) - low) & (kAckWindow - 1));
	BaseValues b;
	if (io.Symbol(Of(kBase)) != 0) {
		ClientBase cb;
		if (!lookup.Find(p.ack, cb)) return false;
		b = Values(&cb);
		p.serverId = io.Symbol(Of(kSameServerId)) != 0 ? b.serverId : S32(static_cast<uint32_t>(io.Raw(32)));
		p.reliableAck = b.cmdSeq + Unzz(io.Class(Of(kRelAck)));
	} else {
		p.serverId = S32(static_cast<uint32_t>(io.Raw(32)));
		p.reliableAck = S32(static_cast<uint32_t>(io.Raw(32)));
	}
	const uint64_t numCommands = io.Class(Of(kNumCommands));
	if (numCommands > kMaxClientCommands) return false;
	p.commands.resize(static_cast<size_t>(numCommands));   // at most kMaxClientCommands
	if (numCommands != 0) {
		int64_t seq = b.has ? b.ack + 1 + Unzz(io.Class(Of(kCommandSeq))) : S32(static_cast<uint32_t>(io.Raw(32)));
		for (ClientCommand &c : p.commands) {
			c.seq = seq++;
			c.text.clear();
			if (!GetCommandText(io, c.text)) return false;
		}
	}
	p.move = static_cast<Move>(io.Symbol(Of(kMove)));
	if (p.move == Move::kNone) {
		p.userCmds.clear();
		return true;
	}
	const uint32_t n = io.Symbol(Of(kNumUserCmds));
	if (n == 0) return false;
	p.userCmds.resize(n);
	std::array<uint32_t, 3> prevAngle = b.angles;
	std::array<uint32_t, 3> prevClass{};
	Bytes prevBytes = b.first;
	int64_t prevTime = 0;
	uint32_t prevStep = kAfterFirst;
	for (uint32_t i = 0; i < n; i++) {
		UserCmd &c = p.userCmds[i];
		c.fill(0);
		int64_t t = 0;
		if (i == 0) {
			uint32_t s = 0;
			t = b.has ? S32(b.commandTime + GetSigned(io, Of(kTime0), s)) : S32(static_cast<uint32_t>(io.Raw(32)));
		} else {
			const uint32_t s = io.Symbol(ctx::ClientStep(prevStep));
			t = S32(prevTime + (s == kStepEscaped ? Unzz(io.Class(Of(kStepEscape))) : s));
			prevStep = s;
		}
		Store32(c, kCmdTime, static_cast<int32_t>(t));
		prevTime = t;
		for (uint32_t a = 0; a < 3; a++) {
			const uint32_t k = AngleContext(i, b.has, prevClass[a]);
			const int64_t d = GetSigned(io, ctx::ClientAngle(a, k), prevClass[a]);
			const uint32_t v = static_cast<uint32_t>(static_cast<int64_t>(prevAngle[a]) + d) & 0xFFFFU;
			Store32(c, kCmdAngles + (size_t{ 4 } * a), static_cast<int32_t>(v));
			prevAngle[a] = v;
		}
		Bytes bytes{};
		for (uint32_t k = 0; k < ctx::kUserCmdFields; k++)
			bytes[k] = static_cast<uint8_t>(io.Symbol(ctx::ClientField(k, i == 0, prevBytes[k])));
		Store32(c, kCmdButtons, static_cast<int32_t>(bytes[0] | (static_cast<uint32_t>(bytes[1]) << 8U)));
		for (uint32_t k = 2; k < ctx::kUserCmdFields; k++) c[kCmdBytes + k - 2] = bytes[k];
		prevBytes = bytes;
	}
	return true;
}

} // namespace
} // namespace detail

ClientBase ClientBaseOf(const HeaderRecord &header, std::span<const uint8_t> ps) noexcept {
	using detail::Load32;
	ClientBase b{ .serverId = header.serverId, .cmdSeq = header.cmdSeq, .ack = header.ack,
	              .commandTime = Load32(ps, detail::kPsCommandTime) };
	for (size_t a = 0; a < 3; a++) {
		const auto pattern = static_cast<uint32_t>(Load32(ps, detail::kPsViewAngles + (4 * a)));
		b.angles[a] = static_cast<uint32_t>(detail::OldIndex(pattern) - Load32(ps, detail::kPsDeltaAngles + (4 * a))) & 0xFFFFU;
	}
	b.weapon = static_cast<uint8_t>(Load32(ps, detail::kPsWeapon));
	b.forceSelected = static_cast<uint8_t>(Load32(ps, detail::kPsForceSelected));
	b.holdable = static_cast<uint8_t>(Load32(ps, detail::kPsHoldable));
	return b;
}

bool ClientPacketEncoder::Encode(const ClientPacket &p, const ClientBase *base, size_t maxBytes, std::vector<uint8_t> &out) {
	if (!detail::Valid(p)) return false;
	const detail::Model &m = detail::Model::Get();
	if (ops_.empty()) ops_.resize(1024);
	if (scratch_.size() < maxBytes) scratch_.resize(maxBytes);
	const detail::BaseValues b = detail::Values(base);
	for (;;) {
		rans::Encoder e(ops_);
		detail::Out io(e, m);
		detail::PutClientPacket(io, p, b);
		if (e.Overflow()) {
			ops_.resize(ops_.size() * 2);
			continue;
		}
		const size_t n = io.Bad() ? 0 : e.Finish(std::span<uint8_t>(scratch_.data(), maxBytes));
		if (n != 0) out.assign(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(n));
		return n != 0;
	}
}

bool DecodeClientPacket(std::span<const uint8_t> bytes, int64_t latest, const ClientBaseLookup &lookup, ClientPacket &out) {
	const detail::Model &m = detail::Model::Get();
	detail::In io(bytes, m);
	return io.Ok() && detail::GetClientPacket(io, latest, lookup, out);
}

bool CountClientPacket(const ClientPacket &p, const ClientBase *base, SymbolCounts &counts) {
	if (!detail::Valid(p) || counts.symbols.size() != detail::ctx::kCount ||
	    counts.rawBits.size() != detail::ctx::kCount)
		return false;
	detail::CountOut io(counts);
	detail::PutClientPacket(io, p, detail::Values(base));
	return !io.Bad();
}

} // namespace netcodec

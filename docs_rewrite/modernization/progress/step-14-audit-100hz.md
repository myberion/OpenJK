# Step 14: the 100 Hz tick, the audits and the "20 Hz logic on the 20 Hz instants" design (11 November 2026, evening)

Working notes kept so that nothing is lost between sessions. Status of each item: **verified** (read in the code by the main session), **reported** (an audit's claim, not yet checked), **done**, **open**.

## 1. Decisions (owner, 11 November 2026, evening)

- **Tick 100 Hz** (was 125): at 125 Hz, 50 ms is 6.25 ticks, so thinks every 50 ms ran every 56 ms, NPC decisions "every 100 ms" alternated 100/150 ms, 20 Hz deadlines jittered. At 100 Hz every 20 Hz instant is a tick.
- **The game must play identically to sv_fps 20** (damage, blocks, attack bounces, timings, AI), and be as precise as possible only where that does not change outcomes. No damage regression.
- Measurements must be reliable: deterministic scripted scenarios rather than free-for-all bot games.
- Every audit claim is checked in the code before acting on it.
- Packet rate: today no impact (a client at 125 fps still sends 125 packets per second, each command runs on arrival). Part 4 (one command per tick): `MAX_PACKET_RATE` 125 → 100, `SV_PACKET_RATE` 137 → 110, `SV_PACKET_BURST` 125 → 100; `com_maxfps` stays a display setting (partial step D5). Jump heights do not depend on the step: velocity is exact and gravity is integrated as a trapezoid (`bg_slidemove.c:673-674`, verified).

## 2. Why the per-tick tuning failed (verified)

- Every saber against saber reaction (bounce, deflection, knockaway, broken parry, lock) is gated by `dmg > SABER_NONATTACK_DAMAGE`, where `dmg = ceil(fDmg * traceLength * 0.033)` and `traceLength` is the blade's move in one server frame (`w_saber.c:4163-4170`, tests from 4400 to 5103 in OpenJK's file). At 100 Hz the move is 5 times shorter, `dmg` is nearly always 1, attacks are no longer bounced: the owner's "fewer attack cancels", dual sabers and staff stronger. No coefficient can fix it.
- My `P_RecentDamage` (pain threshold over 50 ms, commit `9c72c2425`) was wrong: in OpenJK the early return of `P_DamageFeedback` skips the clearing, so damage already accumulates across frames until 10 (rate-independent). Mine re-counted the accumulated total every frame (threshold about 2 to 3 points at 100 Hz). Reverted (verified, done).
- My "pain 750 with >=" halved lava damage (lava is paced by `pain_debounce_time`, hits at T, T+700, T+750...). Reverted (done).
- Bench flaw (verified): `ClientThink` skips `ClientThink_real` for `SVF_BOT` (`g_active.c`, ClientThink); `G_RunClient` runs it once per server frame with `pers.cmd.serverTime = level.time`. So with `bot_debugClientRate` the bots' 10 ms commands collapsed into one 50 ms Pmove at 20 Hz and 10 ms Pmoves at 100 Hz: every bot comparison so far mixed the Pmove step with the server rate. To fix: bots execute every command (open).

## 3. The design being implemented: two clocks, 20 Hz logic island

- `level.time` = logic clock = last 20 Hz instant, for all game code (timers, thinks, damage, user commands, impacts). `level.tickTime` = the tick, only to move missiles, movers, items, and for bots' command times. `level.logicFrame` (macro `G_GRID20_STEP`) = this frame is a 20 Hz instant.
- On the grid (OpenJK's code unchanged): `G_RunThink`; per client in `G_RunFrame`: space suffocation, hacking, jetpack and cloak fuel, siege data, `WP_ForcePowersUpdate`, `WP_SaberPositionUpdate`, `WP_SaberStartMissileBlockCheck`; NPC branch the same; `ClientEndFrame`: powerups expiry, `P_WorldEffects`, `P_DamageFeedback`; level rules (`CheckTournament`, `CheckExitRules`, `CheckVote`, siege respawn wave and timers, queued scores); bots think every 50 ms on the grid; all missiles and items move on the grid (since 12 November: per-tick missiles changed the hits, section 9); the server's ghoul2 clock is the logic clock (section 9).
- Per tick: snapshots, user commands (Pmove with the command's time), networking (movers moved every tick until 12 November; since then on the grid, section 10).
- The same module at sv_fps 20 is the exact reference (all gates true at 20 Hz).
- Consequences: the per-tick saber tuning (coefficient 0.030, contact effect limiter, decision windows, scaled lookahead, head-on check, `WP_SaberStoreBase`) and the force timer rewrites (commit `00c003c91`) are reverted to OpenJK's code (done in the working tree: `w_saber.c` and `w_force.c` are master plus counters; `w_saber.c` keeps `fVSpeed *= 1.6f/20.0f` instead of `1.6f/sv_fps.value` (G3) and `w_force.c` the mind trick window of 200 ms instead of `g_TimeSinceLastFrame*4`, with `g_TimeSinceLastFrame`/`g_LastFrameTime` removed). Saber contact is found at the 20 Hz instants, up to 40 ms later than per-tick tracing: the price of identity.
- Known limits: timescale (`frameMsec = 1000/sv_fps*timescale`, `sv_main.cpp:1135`, verified): during the slow-motion duel end the grid runs every 50 ms of game time (OpenJK ran logic every frame); `g_slowmoDuelEnd` is 0 on the owner's servers. First frame after map load: `previousTime == time` (settle frames, `sv_init.cpp:624-632`, verified): handled by `logicFrame` forced on the first frame (open).

## 4. Audit findings to apply or check (game module)

Saber (agent 1): G1 (gates, verified), G2 clash voids the rest of the 50 ms sweep incl. the other blade (moot on the grid), G3 `1.6f/sv_fps` (done), G6 thrown saber radius damage per tick (moot: think on grid), G7 one-off saber timers (moot on the grid), G8 kicks per tick (in WP_SaberPositionUpdate: moot), G9 missile block pose (moot). Dead code noted: the old "silent stop" paths.

Weapons (agent 2), all moot on the grid or with the logic clock unless noted: thermal fuse (thinks), missile `saberBlockTime` (impact sees the logic clock), E-Web yaw per frame (think), seeker first shot, prox mine, bouncing missiles (grid), charge reads `level.time - weaponChargeTime` at `g_weapon.c:269, 736, 1043, 1433, 2095` (logic clock in ClientThink: 0..50 ms as at 20 Hz), detpack and flechette delays (thinks), DEMP2 shell (think). Launch `trTime = level.time` (`g_missile.c:90,135,317`) becomes the logic clock: the 0..50 ms head start of 20 Hz (identity). `MISSILE_PRESTEP_TIME` unused (reported). Fire rates and per-shot damage identical (PM_Weapon per command, reported).

Client and frame code (agent 3): saber lock presses once per server frame (`g_active.c:2915-2923`; logic clock makes it once per 50 ms window), respawn `+1700` tested `>` (`g_combat.c:2750`, `g_active.c:3446`; logic clock), siege respawn wave (gated), bots think every frame with 45 random draws and 104 timers in ai_main.c (grid, done), bot Pmove step (open: bots must run every command), battlesuit event per frame in lava (gated), absorb/protect sound debounce in G_Damage (logic clock). Desync: game-written ps fields read by pmove with `cmd.serverTime` are offset by ping + 0..50 ms as at 20 Hz; **`saberAttackChainCount` is capped at 16 (`bg_saber.c:3757`) but networked on 4 bits (`msg.cpp:1367`): 16 arrives as 0** (reported, to verify; a CG5 prediction miss candidate); `Q_random(&pm->cmd.serverTime)` at `bg_saber.c:42` overwrites the command time (reported).

World (agent 4): NPC thinks every 50 ms (`NPC.c:1812`) exact at 100 Hz; NPC first think `level.time + FRAMETIME + Q_irand(0,100)` (`NPC_spawn.c:1134`) off-grid (grid gate fixes); mover `reached` at the first tick past the end (`g_mover.c:494`; logic clock fixes), crusher damage per blocked tick (`g_mover.c:1053`, `:379`, `:196`; blocked() on the grid), trigger "same frame" tests `== level.time` (`g_trigger.c:339, 93, 326, 942`; logic clock fixes), `g_object.c:43,105` hitTime from previousTime (logic previousTime fixes), turrets and vehicles fine (vehicle physics run in Pmove), `INITIAL_SUFFOCATION_DELAY` one-off.

Feedback (agent 5): no attacker hit confirmation sound exists; non-client victims got effects every tick (moot on the grid); EV_SHIELD_HIT per G_Damage call (per tick only for per-tick damage sources); staff/dual duplicate EV_SABER_BLOCK (OpenJK behaviour, kept for identity); proposals B (size the first effect by projected damage) and E (shield shell per window) are optional improvements, to show the owner after identity is proven.

## 5. Engine and cgame findings (agents 6 and 7; reported unless marked)

- B1 (verified): delta base refused at `PACKET_BACKUP - 3` = 29 messages (`sv_snapshot.cpp:142-147`): 290 ms of round trip at 100 Hz; above it every snapshot is full and fragmented, the ping reads 999 (acknowledge recorded only within 32 frames, `sv_client.cpp:1270`), the server command resend waits `2 * ping` (`sv_netcodec.cpp:226`). Fix: D3 store (part 5), ping from a send-time table of 256; stopgap PACKET_BACKUP 64.
- B2 (verified): `svs.nextSnapshotEntities` near 2^31 restarts the server (`sv_main.cpp:1157-1160`): about 2.3 h on one map with 32 clients and 80 entities per snapshot at 100 Hz (11.6 h at 20 Hz). Fix: int64 counter now, or the D3 store.
- B3: cgame drops snapshots after a 320 ms client hitch (`cl_cgame.cpp:110`, 32 snapshots). B4: client `snap.ping` 999 above 256 ms round trip (`cl_parse.cpp:85-91`), at 20 Hz too.
- S2 (verified): the dedicated server already reads packets on arrival (`NET_Sleep` → `select` → `NET_Event`, `net_ip.cpp:1070-1075`): the step 14 inventory item S2 is wrong; the ping does not include a frame wait.
- W1 catch-up frames send one snapshot; W2 whole-millisecond tick timing (±1 ms = 10 % of an interval); W3 listen server ticks per client frame; W5 server command repeats (`sv_netcodec.cpp:224-226`: any new command resends all unacknowledged ones, timeout `2*ping` under 10 ms on a LAN); W6 CMD_BACKUP 64; W7 ping mean over 32 frames; X1 busy wait of up to 1 ms per frame in `Com_Frame` (`common.cpp:1553-1557`), up to 10 % of a core at 100 Hz; X3 server CPU about 30 % at 100 Hz (from the 125 Hz measure, not measured); X5 demos 5 times larger; lagometer shows 48 columns (0.48 s at 100 Hz).
- Interpolation delay of step 11 (agent 6): only the newest snapshot of a frame is sampled, so bursts are under-measured; the delay is 8 to 10 ms more than needed (a full interval on top of an offset measured at frame time); the loss margin flaps on 64 samples (0.64 s); slew +1/-2 ms per snapshot frame is 5 times stronger; proposed redesign: sample headroom `cl.snap.serverTime - cls.realtime` every client frame, 64 slots of 32 ms (about 2 s), target = second smallest slot minimum minus 2 ms, bounds 2 to 100 ms, slew in time (+1 ms per 8 ms, -1 ms per 32 ms). Bugs at 20 Hz too: ping 999 above 256 ms (client), arrival stamped with the previous frame's time, start at zero delay, `sorted[1]` with few samples.

## 6. Measurement tools (committed in `4efd458f0`; current usage in section 9)

- Central counters in `gamestats` (`g_svcmds.c` `G_PrintCentralStats`): events by type (G_AddEvent, G_TempEntity), damage and deaths by cause, shots by weapon (FireWeapon), saber attacks and bounces (moves into PM_SaberInBounce) by style (`G_StatsSaberMove` after Pmove in ClientThink). The force statistics line prints raw counts per power.
- `codemp/game/g_scenario.c` (added to `codemp/game/CMakeLists.txt`): `scenario saber <reps> <fast|medium|strong|dual|staff> <idle|attack> [dist]`, `scenario force <reps> <power> [dist]`, `scenario weapon <reps> <weapon> <alt> [dist]`, `scenario off`; two bots reset each repetition (2 s), commands every 10 ms scripted in ms, seeds reset per repetition; needs `bot_debugClientRate 1`, `sv_cheats 1`, two bots; force needs `PREMAP="g_forcePowerDisable 0"` and the force side set (open: set `forceSide` in `G_ScenarioEquip`).
- `gamestats_agg.py` reads the per-power force line; it must be extended to the new central lines.

## 7. Next steps (in order)

As written on 11 November; steps 1 to 4 are done (sections 8 to 10), the current next steps are in [README.md](README.md), "Next actions".

1. Finish the two clocks: `level.tickTime`, `previousTickTime`, `logicFrame` in `G_RunFrame` and `G_InitGame`; tickTime in `G_RunMissile` (positions, `G_MissileImpact` origin2 and velocity), `G_MoverTeam` (positions and back-up), `G_RunClient` (bots' command time), the command time clamp in ClientThink; remove the ClientThink wrapper (redundant); bouncing missiles and items move only on the grid; mover `blocked()` on the grid; G_GRID20_DEADLINE removed.
2. Bots execute every command (`ClientThink` for bots with `bot_debugClientRate`, `G_RunClient` skips them).
3. Build; scenario runs at sv_fps 20 and 100 with the same module (saber per style, idle and attack; force per power; weapons); diff the per-event counts; tolerance 0 for saber and force where seeds align.
4. Free-for-all bench at 20 and 100 Hz as a sanity check.
5. Check `saberAttackChainCount` (CG5); engine items B1, B2 (now), X1; the interpolation redesign; document everything in step-14-fixed-tick.md, CHANGES.md, PLAN, README, progress; Linux on the VPS.

## 8. State at the end of the session (11 November 2026, night)

Implemented in the working tree (builds with clang, no new warning):
- Two clocks: `level.tickTime`, `previousTickTime`, `logicFrame` (`g_local.h`, `G_InitGame`, `G_RunFrame`); `G_GRID20_STEP` is `level.logicFrame` (forced on the first frame); `G_GRID20_DEADLINE`, `SABER_SWING_MSEC`, the saber base history removed.
- Grid gates: `G_RunThink`; the per-client and NPC logic in `G_RunFrame`; `ClientEndFrame` powerups, `P_WorldEffects`, `P_DamageFeedback`; level rules, siege respawn wave and timers, queued scores; bots think when their time crosses a 50 ms boundary (`ai_main.c`); bouncing missiles and items move only on the grid; mover `blocked()` and the four crush damages of `G_MoverPush` (`g_mover.c`, rotating impact, pusher damage, corpses, bobbing) on the grid.
- Tick clock: missile position and trace (`G_RunMissile`), the impact's velocity and mark direction (`G_MissileImpact`), mover movement and back-up (`G_MoverTeam`), bots' command time in `G_RunClient`, the command time clamp in `ClientThink_real`.
- Bots execute every command when `bot_debugClientRate` is set (`ClientThink`, `G_RunClient`), and generate their commands up to the coming frame's time (`time + 1000 / sv_fps`), as players' commands arrive before the frame.
- Scenario: placement traces the world only (`MASK_SOLID`), accepts the 9-unit drop to the floor, avoids spawn points within 512 units of a mover; scripted bots drain their server commands (`trap->BotGetServerCommand`) or the server drops them for timeout; `forceSide` set for force scenarios. `gamestats_windows.sh` takes `SCENARIO="saber 96 medium idle"` (two bots).

First scenario results (same module, `saber 96 medium idle`, two servers per rate, repeatable within 6 %): **20 Hz about 1,440 saber damage, 72 wounds, 60 clashes; 100 Hz about 307 damage, 22 wounds, 48 clashes**, for the same 96 to 102 swings. Identity is not reached: a factor close to 5 points at something still per tick. Checked: the blade's old position, trail and damage traces are only touched in `WP_SaberPositionUpdate` (grid). Hypotheses to test next, in order: (1) `ClientEndFrame` refreshes `ent->s` from `ps` every tick, so grid code reading `ent->s` (animations, position) sees the state of the last tick instead of 50 ms old; (2) ghoul2 bolt caching keyed by `frameNum = level.time`: per-tick calls with the logic clock may cache a skeleton at a tick time that the grid frame then reuses; (3) the defender's auto-block state. Method: log per event (time, entity, saberMove, saberBlocked, blade points, damage) at both rates and diff, as the design review advised. Also: both rates log one `MOD_CRUSH` death (7,000 and 6,001 damage) in the scenario though no mover is near: to explain.

Not done yet: the free-for-all sanity bench; force and weapon scenarios; extending `gamestats_agg.py` to the central lines; the engine items (B1, B2, X1), the interpolation redesign, `saberAttackChainCount`; documentation of the decisions in step-14-fixed-tick.md, CHANGES.md, PLAN, README and the progress table; Linux.

Further result (same module, `saber 64 medium idle`, one server per rate): 20 Hz 995 damage, 47 wounds, 40 clashes; 40 Hz 138, 6, 61; 50 Hz 636, 22, 46; 100 Hz 290, 17, 32. Not monotonic in the number of ticks per 20 Hz instant, and at 40 Hz the defender blocks far more: a state that diverges between the 20 Hz instants rather than a per-tick amount. Two 20 Hz servers with different start seeds differ by about 3 % (1,462 against 1,417 over 96 repetitions), so the scenario is nearly but not fully deterministic. Next: a per-event trace (time, client, commandTime, saberMove, saberBlocked, weaponTime, torsoAnim, blade points, rand() calls count, damage) written at each 20 Hz instant at both rates, diffed from the first repetition; suspects: something per tick touching the defender's block state or consuming rand() between the instants (per-tick code: ClientEndFrame's ungated part, SendPendingPredictableEvents, G_SetClientSound, ICARUS, the bot frame), and the order of the bots' commands against the grid frame.

## 9. Session of 12 November 2026 (night): identity reached in the scenarios

**Result.** With the same module at sv_fps 20 and 100, every scripted scenario gives the same counts: saber (5 styles, idle and attack, 96 repetitions each), force (lightning, grip, drain, rage, protect, absorb, 48 each), weapons (12 weapons, primary and alternate, 48 each): wounds, damage, health taken by cause, deaths, blocks, touches, attacks started, bounces, shots, hits, misses, force starts, periodic events and time held. The remaining differences are explained below; none comes from the server rate.

**Cause of the factor 5 (verified, fixed).** The engine sets the server's ghoul2 clock to `sv.time` after `GVM_RunFrame` (`sv_main.cpp`, `re->G2API_SetTime`), so a frame evaluates the ghoul2 models at the previous tick's time, while the game starts their animations at `level.time` (`G2API_SetBoneAnim` takes the time it is given; `G2API_GetBoltMatrix` and the model traces read the engine's clock through `G2API_GetTime`). At 20 Hz a 20 Hz instant evaluated the animations 50 ms before it; at 100 Hz, 10 ms before: the blades ran 40 ms ahead of their sv_fps 20 path, while the player state (saberMove, timers, origin) was identical. The trace of `saber 8 medium idle` showed it: identical `ps` at every 20 Hz instant, blade points apart. Fix: the engine sets the ghoul2 clock to the logic instant of the frame just run (`sv.time / 50 * 50`), which at 20 Hz is unchanged. Engine change: the dedicated server must be rebuilt with the game.

**Missiles move on the grid (verified, design changed).** With per-tick missiles the bryar did 768 damage instead of 1,248 for the same 96 hits, the concussion rifle 4,656 instead of 3,024, the homing rocket killed 48 times instead of 0, the blaster hit 144 times instead of 96. The trace of one bryar shot: same trajectory and same model point at both rates, but the engine's ghoul2 trace (`SV_ClipMoveToEntities`) depends on the step. A step that enters a player's box without reaching the model passes; the next step starts in the box: if it leaves the box (80-unit steps at 20 Hz) the box trace reports `startsolid` with fraction 1, the game ignores the model surface (`g_missile.c`, `tr.fraction != 1.0`) and hits the box (location from the box: head, x1.3); if it ends in the box (16-unit steps at 100 Hz) the fraction is 0 and the model surface is used (arm, x0.85). Shorter steps also land more missiles inside boxes, which count as hits without a model test. Per-tick missiles were therefore not more precise, only different: all missiles now move on the 20 Hz instants with the logic clock (`G_RunMissile` returns between them), as at sv_fps 20; the tick time is no longer used in `g_missile.c`.

**Bench fixes (tools).**
- The bots' commands run in the order of their times across all bots (`BotAIStartFrame`, `bot_debugClientRate`): a 20 Hz frame ran the five commands of one bot, then the five of the other, and two bots walking into each other ended 0.9 unit apart (strong attack, diagonal directions); players' packets arrive interleaved.
- `scenario`: a first repetition, not counted, replaces the state left by the game before the scenario (animations); the statistics of the counted repetitions are printed at the end (`gamestats`, which the script's watchdog takes as the end of the run); the bots then stand still until `scenario off` (they used to return to their AI and the free play after the scenario was counted: the `MOD_CRUSH` death of section 8 happened there). `set scenario_trace <n>` prints the two bots' state at every 20 Hz instant and every health loss (tick, take, ghoul2 surface, point) for the first n repetitions, to diff two rates.
- `SendPendingPredictableEvents` no longer counts its temporary entities in the event statistics: they copy Pmove events, and their number depends on the commands per frame.

**Remaining differences (explained, not rate effects).**
- `EV_BODYFADE`, `EV_ITEM_RESPAWN`, `EV_PLAYDOORSOUND`: bodies, items and doors left by the free play before the scenario.
- `EV_DEATH1` against `EV_DEATH3`: `player_die` cycles the death animations with a static counter, carried from earlier deaths.
- Flechette primary, 1 of 48 repetitions (195 hits against 194): the shards' entity numbers differ (temporary entities allocated differently), so they are processed in another order in the frame and a knockback moves the target before the next shard; the two values swapped between two runs at the same rates.
- Grip `EV_PAIN` 4 once at 20 Hz; six further servers (three per rate) all gave 5.

**Still per tick, to decide** (decided on 12 November: movers on the instants, section 10). Mover movement (positions every tick for the players' collisions; `blocked()` and the crush damage on the grid). Not covered by the scenarios (placed away from movers); per-tick positions change the players' collisions with lifts and doors slightly. Options: keep (smoother riding, small outcome differences) or move them on the grid (exact identity).

**Free-for-all sanity bench** (16 bots, `bot_debugClientRate 1`, FAST=1, 60 minutes of game per server, 3 batches of 12 servers per rate, same module): damage per minute 100 Hz against 20 Hz +1.0 %, -8.5 %, -0.8 % per batch (batch 2 holds a server at 14,728 against a mean of 20,500); pooled -2.8 % (standard error of the difference 1.8 %), deaths -2.2 % (1.8 %), blocks -0.9 % (2.0 %): within the noise. Free play diverges chaotically and still has per-tick movers; the scenarios are the proof of identity.

**Next.** The items of section 7 step 5; the documentation of the decisions in step-14-fixed-tick.md, CHANGES.md, PLAN, README and the progress table; Linux on the VPS.

## 10. Owner's decisions of 12 November 2026 and the status of every audit item

**Decisions (owner, 12 November 2026).**
- The saber stays on the 20 Hz instants: what the 100 Hz tick gains is perception (the other players drawn about 11 ms behind the newest snapshot instead of about 53, 100 snapshots per second), not earlier hit decisions; the per-tick prototype decided saber contacts about 20 ms sooner on average but changed the balance. More reactivity would be a deliberate gameplay change (the saber's reactions remodelled in time and retuned), not planned.
- Packet rate: `MAX_PACKET_RATE` 125 to 100, `SV_PACKET_RATE` 137 to 110, `SV_PACKET_BURST` 125 to 100 only with part 4 (one command per tick, sampled every 10 ms whatever `com_maxfps`; display at `com_maxfps` with the partial step of D5). Nothing changes before.
- Movers on the 20 Hz instants (done, below).
- CG5: a 5-bit field for `saberAttackChainCount` (no gameplay change). It changes the alphabets of its contexts (`KindAlphabet` in `netcodec/src/contexts.h`: 16 to 32 symbols, 6 to 7), so the trained tables (`jka_tables.inc`) must be retrained, and the training corpus (`data/traces/train.bin`) is no longer on this machine: done with step 13 (retraining on recordings of this build, which regenerates the schema and the tables anyway; owner, 12 November 2026). Until then a chain of 16 attacks or more (fast style; staff and dual chain without the counter) is mispredicted once.
- `PM_irand_timesync` fixed (done, below).
- B1 and B2 wait for the snapshot store D3 (part 5).
- X1: the dedicated server sleeps its last millisecond (done, below; measured on the VPS on 12 November 2026).
- The interpolation redesign of agent 6 goes with part 4.
- No hit confirmation sound for the attacker (agent 5's remark): not wanted.

**Done on 12 November after the decisions (commit `1376279bd`, after `4efd458f0`).**
- Movers: `G_RunMover` returns between the 20 Hz instants (`g_mover.c`), so they move, push, crush and think as at `sv_fps` 20; the per-push gates and the tick time are gone from `g_mover.c` (OpenJK's file plus that test), `level.previousTickTime` removed. Free-for-all batch (12 servers per rate): damage -1.3 %, deaths -0.9 %, blocks -2.0 %, within the noise.
- `PM_irand_timesync` (`bg_saber.c`): seeded with `&pm->cmd.serverTime`, each draw overwrote the command's time, so the movement code after it in the step read a random time (footstep and slope timers, weapon charge times, saber lock and hand extension times, zoom...). It now draws from `pml.randSeed`, set to the command's time at the start of each step (`bg_local.h`, `PmoveSingle`): the same random numbers as before. The scenarios give the same counts before and after (the corrupted reads did not change their outcomes).
- X1 (`qcommon/common.cpp`, `net_ip.cpp`, `qcommon.h`): `NET_Sleep` takes microseconds; outside Windows a dedicated server sleeps its last millisecond in steps of 100 us instead of the busy loop of `NET_Sleep(0)` (up to a tenth of a core at 100 Hz). `select` still returns at once when a packet arrives. On Windows the busy loop stays (`select` there waits whole timer periods). `com_busyWait 1` keeps the busy wait everywhere. Measured on the VPS (GCC 12 build, an empty server, `time timeout 120 openjkded.x86_64 +set sv_fps 100 +map mp/ffa3`, the same game module): user 3.106 s and system 8.516 s before (11.6 s, 9.7 % of a core), user 0.352 s and system 0.940 s after (1.3 s, 1.1 %).
- `gamestats.sh` (Linux) takes `SCENARIO=` as the Windows script does; its header no longer mentions the removed `g_debugSaberGrid`.
- All scenarios again (10 saber, 6 force, 24 weapon cases): identical at 20 and 100 Hz (flechette primary: the known shard order), unchanged against the commit.

**Status of every audit item (sections 4 and 5).** The engine rows brought up to date on 14 November 2026, after parts 3, 4 and 5 of step 14 ([step-14-fixed-tick.md](step-14-fixed-tick.md) sections 10 to 12).

| item | status |
|---|---|
| G1 saber, force and missile blocks once per frame | done: on the 20 Hz instants |
| G2 clash voids the rest of the sweep | moot (20 Hz instants) |
| G3 `1.6f/sv_fps` | done: `1.6f/20.0f` |
| G6 thrown saber radius damage, G7 one-off saber timers, G8 kicks, G9 missile block pose | moot (20 Hz instants) |
| weapons: thermal fuse, `saberBlockTime`, E-Web yaw, seeker, prox mine, charge, detpack, flechette, DEMP2, launch `trTime` | moot (logic clock and instants); the weapon scenarios agree |
| per-tick missiles | replaced: all missiles on the 20 Hz instants (section 9) |
| saber lock presses once per frame, respawn `+1700`, absorb and protect sound debounce | moot (logic clock) |
| siege respawn wave, battlesuit event in lava, bots' AI every frame | done: on the 20 Hz instants |
| bots one movement step per server frame | done (part 3): the bots send a command per tick; the debug cvar `bot_debugClientRate`, which did it for the measurements, is removed |
| CG5 `saberAttackChainCount` 16 sent as 0 | verified; 5-bit field with step 13 (above). The other prediction misses when crouching or jumping while attacking were not measured apart. Part 10 of step 14 (done) measured the whole prediction with `predictmiss`: what is left with the saber is the force power levels the movement code reads, also with step 13 |
| `Q_random(&pm->cmd.serverTime)` | verified, fixed |
| NPC first think off the instants, mover `reached`, trigger `== level.time` tests, `g_object` hit time | moot (logic clock and instants) |
| mover crush damage per tick | done: movers on the instants |
| agent 5: per-tick effects on non-clients, `EV_SHIELD_HIT` per tick, proposals B and E | moot (no per-tick damage left) |
| agent 5: no hit confirmation sound | not wanted |
| staff and dual duplicate `EV_SABER_BLOCK` | kept (OpenJK's behaviour) |
| B1 delta base refused above 29 messages (290 ms of round trip at 100 Hz) | done in part 5b: a window of 128 messages, a base up to 125 messages back (1.25 s); checked in game with 95 % of the messages dropped on the client |
| B2 `svs.nextSnapshotEntities` wraps (about 2.3 h with 32 clients at 100 Hz) | done in part 5a (`6e3c0a645`): counted on 64 bits, the forced restart removed |
| B3 cgame drops snapshots after a 320 ms hitch | follows part 5b: `CL_GetSnapshot` keeps `PACKET_BACKUP` snapshots, 1.28 s at 128; to test with a hitch in part 11 of step 14 |
| B4 client ping 999 above 256 ms | done in part 4e (`2bef287fe`): the client's ping over its last 256 packets, in real time |
| S2 (the step document's inventory: packets drained once per frame) | wrong: the server runs each packet on arrival (`NET_Sleep` returns from `select`, `NET_Event` runs it); the step document is corrected |
| W1 catch-up frames send one snapshot | kept (part 3): the snapshot holds the newest state; one per catch-up tick would send up to 25 messages at once |
| W2 whole-millisecond frame timing | overstated (part 3, measured): the loop carries the remainder, a frame starts 0.1 ms late on Linux and 0 to 0.5 ms on Windows; the Linux clock made monotonic |
| W3 listen server ticks per client frame | kept (part 3): the dedicated server is the target |
| W5 server command repeats | done (parts 3 and 4b): counted by cause, none without loss; the timeout is twice the ping plus a packet interval |
| W6 `CMD_BACKUP` 64 | done (part 4b): 256 |
| W7 ping mean over 32 frames | done (part 3): over the last 256 messages |
| X1 busy wait | done (Linux dedicated server): an empty server at 100 Hz 9.7 % of a core before, 1.1 % after (VPS) |
| X3 server CPU at 100 Hz | open: part 7 |
| X5 demos 5 times larger | open: part 11 of step 14 |
| lagometer 48 columns | open: part 9 |
| interpolation redesign (agent 6) and its 20 Hz bugs | done in part 4e (`2bef287fe`): the drawing clock rewritten on the proposal |
| dead code reported by the agents (the saber's old "silent stop" paths, `MISSILE_PRESTEP_TIME` unused) | reported only: part 11 of step 14 (the owner decides on the list) |

**Found on the way, fixed in part 3 (commit `3f9b7f94e`):** the Linux `Sys_Milliseconds` (`shared/sys/sys_unix.cpp`) used `gettimeofday`, the wall clock: an NTP step moves the server's clock (frames run in a burst or stop). A monotonic clock (`clock_gettime(CLOCK_MONOTONIC)`) fixes it.

## 11. Session of 12 November 2026 (afternoon): Linux, and the scenario bench made deterministic

**Linux (VPS, GCC 12 build from the Debian VM).** X1 measured (section 10). The 40 scenario cases of section 9: identical at `sv_fps` 20 and 100 with the module of the morning (`1376279bd`); with the module of this section: identical at 20 and 100 Hz as well (40 of 40).

**Rare differences between two runs at the same rate.** On the VPS about one run in 40 of the staff and dual attack scenarios gave other counts than the other runs at the same rate (locally, with clang, about one in several hundred). Never a difference between the rates: every pair of runs with the same history gave the same counts at 20 and 100 Hz. The cause was the bench, not the game's timing: the free play before a scenario (30 s of bots playing) left state in the bots and in the level that the reset did not clear, and the free play itself depended on the machine's load, since `fixedtime` is set only after `devmap` and the first frames of the map followed the real time (one bot frame for several game frames under load). With the owner's game running, the differences came back (2 in 48 runs); with the seed and the fixed frame set before the map loads, never.

Found and fixed in `g_scenario.c`, each with the scenario that showed it:

| state left by the free play or the previous repetition | how it showed | fix |
|---|---|---|
| `movementDir` and `PMF_BACKWARDS_RUN` of a still bot (they change only with movement commands) | blaster alternate fire 2,427 or 2,432 damage: a pushed defender's legs turned the other way (`BG_G2PlayerAngles`), so the points the bolts hit moved by up to 1.3 units | cleared at every reset |
| entity slots freed less than a second before (`G_Spawn` skips them) | missiles with other entity numbers, run in another order (the flechette shard order of section 9) | when the counted repetitions start: events, missiles (mines included) and dropped items freed, every free slot usable at once |
| the damage `P_DamageFeedback` keeps summing until it plays a pain (under 10 points or within 500 ms of a pain; OpenJK's behaviour) | one `EV_PAIN` more | sums cleared at every reset |
| pain timers, powerups, corpses, the last surface hit | `EV_PAIN`, powerup sounds, a field of the trace | cleared or removed |
| torso and legs animations of a still bot (the choke of a grip still held at the reset, the slope stance of the legs) | grip: 171 periodic events instead of 168; every repetition differed, since each started as the previous one ended | the weapon's ready animations at every reset, the ghoul2 animations restarted |
| the spawn point of a bot killed in the previous repetition, drawn with `rand()` before the repetition's reseed | strong idle: an idle touch one instant earlier from repetition 57 on (8 repetitions after the second death) | reseeded before the bots are placed |
| a saber thrown or knocked away at the reset; the blade's first sweep starting from its position before the teleport | strong idle: 5 or 9 idle touches instead of 6, another stance | the saber back in the hand as when caught; the blades' last positions forgotten |

Measured not to be causes: uninitialized locals in the game module or the engine (both built with `-ftrivial-auto-var-init=zero` and `=pattern`: the same counts on the 40 cases), the random numbers (they diverge only as a consequence), the movers, the saber entities' ghoul2 models, a bot's death just before the scenario.

**Method.** `g_debugSeed` (game module) replaces the clock's start seed, and the scripts' `SEED=<n>` gives server n the seed `SEED+n` and sets the fixed frame before the map loads: a run replays the same game, the free play included (two runs of one seed: the same log apart from the time stamps). Seeds were scanned until one gave other counts, then compared with a normal seed with `scenario_trace` and temporary dumps of `playerState_t`, `gclient_t`, `gentity_t` and `forcedata_t` at the start of the counted repetitions.

**Results with the fixes.** 96 seeds each of grip and strong idle, 48 each of staff, dual and fast attack, grip, blaster alternate and flechette primary: one result per case. The 40 cases identical at 20 and 100 Hz (Windows, clang, free play from the clock). VPS: none of 160 runs differs (staff attack, dual attack, strong idle, grip, 40 runs each, free play under load). The counts differ from section 9's, since every repetition now starts from the weapon's ready pose without the blade's previous position: the proof is the identity of the two rates and of the runs, not the counts.

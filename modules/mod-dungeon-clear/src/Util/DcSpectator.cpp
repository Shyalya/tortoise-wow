/*
 * mod-dungeon-clear — DcSpectator.cpp
 *
 * See DcSpectator.h for the design and the Free-vs-Follow split.
 *
 * Free mode's load-bearing detail: the possess branch of Unit::SetCharmedBy
 * sets UNIT_FLAG_DISABLE_MOVE on the CHARMER (the player body). Nearly every
 * MotionMaster entry point refuses a UNIT_FLAG_DISABLE_MOVE owner — including
 * the two DungeonClear uses, MovePoint (MotionMaster.cpp:488) and
 * MoveSplinePath (MotionMaster.cpp:506) — so without clearing that flag the bot
 * body freezes solid the moment the camera detaches. We clear it immediately
 * after a successful possess; it is a one-time set (nothing re-asserts it
 * during possession), and RemoveCharmedBy clears it again on teardown, so the
 * early clear is harmless on exit.
 *
 * Follow mode's load-bearing detail: Player::SetViewpoint stores the seer in
 * PLAYER_FARSIGHT via AddGuidValue, which REFUSES to overwrite a non-empty
 * field. Possession also parks the dummy there (SetClientControl ->
 * SetViewpoint). So the two modes can never be applied on top of one another —
 * every entry point tears the other down first, and ClearViewpoint below
 * force-clears the field when the watched unit has vanished, because
 * SetViewpoint(obj, false) needs a live object to hand back.
 */

#include "Util/DcSpectator.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "Chat.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "StringFormat.h"
#include "TemporarySummon.h"

#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"

namespace
{
    enum class Mode : uint8
    {
        Free,       // possessed dummy, client-steered
        Follow      // farsight on a bot, server-steered
    };

    struct SpectatorState
    {
        Mode mode = Mode::Free;
        ObjectGuid dummy;               // Free: the possessed camera TempSummon
        ObjectGuid watched;             // Follow: the unit the camera rides
        bool gmModeApplied = false;     // we turned GM mode on; undo it on Stop
        uint32 retargetAccumMs = 0;
    };

    // A Follow camera armed before its viewer finished zoning in. Started by
    // Tick once the viewer lands on the tank's map (see RequestFollowOnArrival).
    struct PendingFollow
    {
        ObjectGuid tank;
        bool gmModeApplied = false;
        uint32 ageMs = 0;
    };

    // Give up on an unfulfilled arrival request rather than latching a stale
    // intent forever — a teleport that never lands (declined, disconnected,
    // bounced back) must not ambush the GM with a camera minutes later.
    constexpr uint32 PENDING_FOLLOW_TIMEOUT_MS = 60 * 1000;

    // Lift the free camera off its seat bot's feet so it opens at roughly head
    // height instead of inside the floor.
    constexpr float CAMERA_SEAT_LIFT_Z = 2.0f;

    // Object scale for the camera dummy. A GM is shown the trigger's VISIBLE
    // model no matter what display id we pin (see the block in Start), and
    // OBJECT_FIELD_SCALE_X is the one appearance field the core does not
    // rewrite per-viewer — so shrink the thing to a speck.
    constexpr float CAMERA_SCALE = 0.01f;

    // How often a live Follow camera re-resolves its target. Slow on purpose:
    // it only has to notice a leader re-election or a death, and every swap
    // costs a visibility rebuild around the new seer.
    constexpr uint32 RETARGET_INTERVAL_MS = 2000;

    // player guid -> camera state. Never store the dummy or the watched unit as
    // a raw pointer — resolve via ObjectAccessor at each use so a despawn race
    // can't dangle. Guarded by g_mutex: teardown hooks fire on map threads and
    // both Tick and the AI mover window read this from every map thread.
    std::mutex g_mutex;
    std::unordered_map<ObjectGuid, SpectatorState> g_spectators;
    std::unordered_map<ObjectGuid, PendingFollow> g_pending;

    // Lock-free fast path for the per-tick entry points: zero on the
    // overwhelmingly common no-spectator server, so Tick and the mover window
    // cost one relaxed atomic load per player tick. Counts pending requests
    // too — an armed request still needs Tick to run for its viewer.
    std::atomic<uint32> g_activeCount{0};

    // Snapshot a player's camera state, or false when not spectating.
    bool LookupState(Player* player, SpectatorState& out)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_spectators.find(player->GetObjectGuid());
        if (it == g_spectators.end())
            return false;
        out = it->second;
        return true;
    }

    void Announce(Player* player, std::string const& msg)
    {
        if (player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage(msg);
    }

    bool Refuse(std::string* whyNot, std::string const& reason)
    {
        if (whyNot)
            *whyNot = reason;
        return false;
    }

    // Shared entry validation for both modes. `ownFollowCamera` says the caller
    // is switching out of a Follow camera we installed: its farsight is still
    // latched at this point on purpose (Start uses it to reach the seat), so the
    // "already viewing through something" gate would refuse our own camera.
    bool CommonStartChecks(Player* player, std::string* whyNot, bool ownFollowCamera = false)
    {
        // Admin gate: some servers treat detaching the player from their body as
        // a cheat. Server-only conf flag, so a player can't override it. Only the
        // entry path is gated — Stop() is always allowed so an in-progress
        // spectate can still be ended if the flag is flipped off mid-session.
        if (!DcSettings::GetBool(player, "SpectateEnable"))
            return Refuse(whyNot, "Spectator mode is disabled on this server.");
        if (!player->IsAlive())
            return Refuse(whyNot, "You are dead.");
        // (no vehicles on 1.12 - upstream's vehicle refusal has nothing to
        // refuse here)
        if (player->IsTaxiFlying())
            return Refuse(whyNot, "Cannot spectate while on a flight path.");
        if (player->GetCharmGuid())
            return Refuse(whyNot, "Cannot spectate while controlling another unit.");
        if (player->GetViewpoint() && !ownFollowCamera)
            return Refuse(whyNot, "Cannot spectate while viewing through something else.");
        return true;
    }

    // Drop the player's farsight, live object or not. SetViewpoint(obj, false)
    // is the correct path (it also unregisters us from the target's vision
    // list), but it needs the object — when the watched bot has logged out or
    // changed map we still have to clear PLAYER_FARSIGHT ourselves, or the
    // field stays latched and the next AddGuidValue refuses.
    void ClearViewpoint(Player* player, ObjectGuid watched)
    {
        if (watched.IsEmpty())
            return;

        if (Player* target = ObjectAccessor::FindConnectedPlayer(watched))
            player->SetViewpoint(target, false);
        else
        {
            // No m_seer on this engine - Camera::ResetView is the seer
            // reset; the farsight field is still cleared by hand below.
            player->GetCamera().ResetView();
            player->SetGuidValue(PLAYER_FARSIGHT, ObjectGuid::Empty);
        }
    }

    // Is this bot still worth watching from `viewer`'s seat?
    bool IsWatchable(Player* viewer, Player* target)
    {
        return target && target != viewer && target->IsInWorld() &&
               target->FindMap() == viewer->FindMap() &&
               GET_PLAYERBOT_AI(target) && target->IsAlive();
    }
}

namespace DcSpectator
{
    bool IsActive(Player* player)
    {
        if (!player)
            return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_spectators.count(player->GetObjectGuid()) != 0;
    }

    bool IsFollowing(Player* player)
    {
        if (!player)
            return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_spectators.find(player->GetObjectGuid());
        return it != g_spectators.end() && it->second.mode == Mode::Follow;
    }

    bool HiddenByWatch(Player* player)
    {
        if (!player)
            return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        // Either half can hold the flag: a live camera carries it in its state,
        // a watch whose loading screen hasn't finished still has it parked on
        // the pending request.
        auto it = g_spectators.find(player->GetObjectGuid());
        if (it != g_spectators.end() && it->second.gmModeApplied)
            return true;
        auto pending = g_pending.find(player->GetObjectGuid());
        return pending != g_pending.end() && pending->second.gmModeApplied;
    }

    ObjectGuid WatchedGuid(Player* player)
    {
        if (!player)
            return ObjectGuid::Empty;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_spectators.find(player->GetObjectGuid());
        if (it == g_spectators.end() || it->second.mode != Mode::Follow)
            return ObjectGuid::Empty;
        return it->second.watched;
    }

    Player* ResolveWatchTarget(Player* viewer, Player* exclude)
    {
        if (!viewer)
            return nullptr;

        // In a group with the run: the elected leader tank is the run's driver,
        // and therefore the most informative seat in the house.
        if (viewer->GetGroup())
            if (Player* leader = DcLeaderSignal::FindLeaderTank(viewer))
                if (leader != exclude && IsWatchable(viewer, leader))
                    return leader;

        // Outside the party — the GM-watcher case, and the reason this function
        // exists at all. Scan the map for the bot driving a dungeon-clear run,
        // falling back to any alive tank bot. An instance holds one party, so
        // this walks a handful of players, at most once every couple of seconds.
        Map* map = viewer->FindMap();
        if (!map)
            return nullptr;

        Player* fallback = nullptr;
        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* candidate = it->getSource();
            if (candidate == exclude || !IsWatchable(viewer, candidate))
                continue;

            // The run's driver: exactly the bot whose decisions you want to see.
            if (DcLeaderSignal::IsDungeonClearLeader(candidate))
                return candidate;

            if (!fallback && PlayerbotAI::IsTank(candidate))
                fallback = candidate;
        }

        return fallback;
    }

    std::vector<ObjectGuid> WatchRoster(Player* viewer)
    {
        std::vector<ObjectGuid> roster;
        if (!viewer || !viewer->FindMap())
            return roster;

        // EVERY watchable bot on the map, not just tanks: cycling exists so a
        // watcher can sit on the healer during a wipe or a DPS during a burn
        // phase. ResolveWatchTarget's tank preference is about picking a good
        // DEFAULT seat; it must not narrow the set you can reach by hand.
        std::vector<Player*> bots;
        Map::PlayerList const& players = viewer->FindMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
            if (Player* candidate = it->getSource())
                if (IsWatchable(viewer, candidate))
                    bots.push_back(candidate);

        // Deterministic order, or next/prev would shuffle under the viewer as
        // the map's player list churns. Leader tank first (the default seat, so
        // "next" from a cold start walks the party in a sensible order), then
        // GUID — stable for as long as the run lasts.
        std::sort(bots.begin(), bots.end(), [](Player* a, Player* b)
        {
            bool const aLead = DcLeaderSignal::IsDungeonClearLeader(a);
            bool const bLead = DcLeaderSignal::IsDungeonClearLeader(b);
            if (aLead != bLead)
                return aLead;
            return a->GetObjectGuid() < b->GetObjectGuid();
        });

        roster.reserve(bots.size());
        for (Player* bot : bots)
            roster.push_back(bot->GetObjectGuid());
        return roster;
    }

    Player* FindWatchableByName(Player* viewer, std::string const& name)
    {
        if (!viewer || name.empty())
            return nullptr;

        // Exact name first — an unambiguous request always wins.
        if (Player* exact = ObjectAccessor::FindPlayerByName(name.c_str()))
            if (IsWatchable(viewer, exact))
                return exact;

        // Then a case-insensitive prefix match within this map's bots, because
        // randomised test-comp names are not the kind of thing anyone wants to
        // type in full. Ambiguous prefixes resolve to the first in roster order
        // rather than refusing: cycling is right there if it picked wrong.
        std::string needle = name;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        for (ObjectGuid const& guid : WatchRoster(viewer))
        {
            Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
            if (!bot)
                continue;
            std::string botName = bot->GetName();
            std::transform(botName.begin(), botName.end(), botName.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (botName.compare(0, needle.size(), needle) == 0)
                return bot;
        }

        return nullptr;
    }

    bool SeatFollow(Player* viewer, Player* target, std::string* whyNot)
    {
        if (!viewer)
            return Refuse(whyNot, "No player.");

        // Not following yet — this is a plain start.
        if (!IsFollowing(viewer))
            return StartFollow(viewer, target, whyNot);

        if (!target)
            target = ResolveWatchTarget(viewer);
        if (!target)
            return Refuse(whyNot, "No bot here to follow.");
        if (!IsWatchable(viewer, target))
            return Refuse(whyNot, "That target can't be followed from here.");

        ObjectGuid const current = WatchedGuid(viewer);
        if (target->GetObjectGuid() == current)
            return Refuse(whyNot, "Already watching " + std::string(target->GetName()) + ".");

        // MOVE the camera, never Stop+Start. Stop would restore a GM-mode flip
        // made by `.dc test watch` and drop the flag that records it, so
        // changing seats mid-run would silently un-hide the watcher and lose the
        // restore — quite apart from unrooting/rerooting and announcing twice.
        ClearViewpoint(viewer, current);
        viewer->SetViewpoint(target, true);
        viewer->UpdateVisibilityForPlayer();

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_spectators.find(viewer->GetObjectGuid());
            if (it == g_spectators.end())
                return Refuse(whyNot, "Camera ended mid-switch.");
            it->second.watched = target->GetObjectGuid();
            it->second.retargetAccumMs = 0;
        }

        Announce(viewer, "Now watching " + std::string(target->GetName()) + ".");
        return true;
    }

    bool CycleFollow(Player* viewer, int delta, std::string* whyNot)
    {
        if (!viewer)
            return Refuse(whyNot, "No player.");

        std::vector<ObjectGuid> const roster = WatchRoster(viewer);
        if (roster.empty())
            return Refuse(whyNot, "No bots here to watch.");

        // Not following yet: cycling is also the cheapest way to START, so take
        // the default seat rather than making the watcher issue two commands.
        ObjectGuid const current = WatchedGuid(viewer);
        if (current.IsEmpty())
            return StartFollow(viewer, nullptr, whyNot);

        std::size_t index = 0;
        bool found = false;
        for (std::size_t i = 0; i < roster.size(); ++i)
            if (roster[i] == current)
            {
                index = i;
                found = true;
                break;
            }

        // Current seat has dropped out of the roster (died, zoned): treat the
        // step as "start from the top" instead of failing the cycle.
        std::size_t const size = roster.size();
        std::size_t const next = found
            ? (index + static_cast<std::size_t>(static_cast<int>(size) + (delta % static_cast<int>(size)))) % size
            : 0;

        Player* target = ObjectAccessor::FindConnectedPlayer(roster[next]);
        if (!target)
            return Refuse(whyNot, "That bot is no longer available.");
        if (target->GetObjectGuid() == current)
            return Refuse(whyNot, "No other bot here to watch.");

        return SeatFollow(viewer, target, whyNot);
    }

    std::string RosterText(Player* viewer)
    {
        std::vector<ObjectGuid> const roster = WatchRoster(viewer);
        if (roster.empty())
            return "No bots here to watch.";

        ObjectGuid const current = WatchedGuid(viewer);
        std::string out = "Watchable bots here (`.dc spectate follow <name>`, or next/prev):";
        for (ObjectGuid const& guid : roster)
        {
            Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
            if (!bot)
                continue;
            out += "\n  ";
            out += (guid == current) ? "> " : "  ";
            out += bot->GetName();
            if (DcLeaderSignal::IsDungeonClearLeader(bot))
                out += " (leader tank)";
            else if (PlayerbotAI::IsTank(bot))
                out += " (tank)";
            out += Acore::StringFormat(" — lvl {}, {}% hp", bot->GetLevel(),
                                       bot->GetMaxHealth()
                                           ? static_cast<uint32>(bot->GetHealth() * 100 / bot->GetMaxHealth())
                                           : 0);
        }
        return out;
    }

    bool Start(Player* player, std::string* whyNot)
    {
        if (!player)
            return Refuse(whyNot, "No player.");

        // Follow -> Free is a legitimate one-step switch, and it is the ONLY
        // way a `.dc test watch` GM gets a flyable camera: their body is parked
        // at the instance entrance and the watching is done with farsight, so
        // going through Stop first would land them back in that body a dungeon
        // away from the run. Any OTHER live camera is still a refusal.
        SpectatorState prior;
        bool const hadCamera = LookupState(player, prior);
        bool const fromFollow = hadCamera && prior.mode == Mode::Follow;
        if (hadCamera && !fromFollow)
            return Refuse(whyNot, "Already spectating.");

        if (!CommonStartChecks(player, whyNot, fromFollow))
            return false;
        // Free-fly is a dungeon-clear feature: the camera flies around bots
        // running an instance. Outside a dungeon there is nothing to spectate,
        // and possessing a WORLD_TRIGGER in the open world has caused issues.
        // (Follow mode carries no such restriction — farsight is the same
        // mechanism Mind Vision uses, and works anywhere.)
        Map* map = player->FindMap();
        if (!map || !map->IsDungeon())
            return Refuse(whyNot, "Free-fly spectator mode is only available inside a dungeon.");

        // Open the camera ON THE PARTY, not on the body. `.dc test watch` drops
        // the GM's body at the instance ENTRANCE by design and does the actual
        // watching with farsight, so a camera that spawned at the body started a
        // dungeon's length behind the run and had to fly the whole way in.
        // Prefer the bot the follow camera is already riding, then the default
        // watch seat; with neither (no bots here) fall back to the body.
        Player* seat = fromFollow ? ObjectAccessor::FindConnectedPlayer(prior.watched) : nullptr;
        if (!IsWatchable(player, seat))
            seat = ResolveWatchTarget(player);

        Position spawn = player->GetPosition();
        if (seat)
        {
            spawn = seat->GetPosition();
            spawn.m_positionZ += CAMERA_SEAT_LIFT_Z;
        }

        // Core-defined invisible utility template — no creature_template SQL
        // row needed, runtime hardening below covers the rest.
        Creature* dummy = player->SummonCreature(WORLD_TRIGGER,
            spawn.x, spawn.y, spawn.z, spawn.o, TEMPSUMMON_MANUAL_DESPAWN, 0);
        if (!dummy)
            return Refuse(whyNot, "Failed to summon the camera.");

        dummy->SetFaction(FACTION_FRIENDLY);
        // No SetImmuneToAll helper here - these two flags are what AC's
        // helper boils down to.
        dummy->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
        // AC's UNIT_FLAG_NON_ATTACKABLE (0x2) has no name here; the immune
        // flags SetImmuneToAll just raised already cover the intent.
        dummy->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        dummy->SetReactState(REACT_PASSIVE);

        // Two separate things hide the camera, because the pilot defeats the
        // first one.
        //
        // The MODEL, for everyone else: pin the invisible one instead of
        // trusting the template's roll. WORLD_TRIGGER (12999) ships TWO models —
        // 17519 and 11686 — and which one a summon gets runs through
        // ObjectMgr::ChooseDisplayId -> GetFirstInvisibleModel, which returns
        // the first model whose DBC-derived modelInfo->is_trigger is set and
        // otherwise falls back to DefaultInvisibleModel. That detection reads
        // CreatureModelData out of the client DBCs against a hardcoded 14-entry
        // table, so on data where neither model resolves as a trigger the camera
        // can end up wearing a model that RENDERS.
        //
        // The SCALE, for the GM: pinning the display id does nothing for the one
        // person guaranteed to be looking. WORLD_TRIGGER carries
        // CREATURE_FLAG_EXTRA_TRIGGER, and Unit::BuildValuesUpdate ("Use
        // modelid_a if not gm, _h if gm for CREATURE_FLAG_EXTRA_TRIGGER
        // creatures", Unit.cpp) OVERWRITES UNIT_FIELD_DISPLAYID on the wire with
        // cinfo->GetFirstVisibleModel() for any GM-account viewer in GM mode —
        // deliberately, so GMs can see triggers. Whatever we write to the field
        // never reaches them, which is why the camera has been visible to its
        // own pilot. OBJECT_FIELD_SCALE_X gets no such per-viewer rewrite, so
        // shrink the dummy instead: the GM's forced-visible model still arrives,
        // at 1% size. Never use Unit::SetVisible(false) for this: that sets
        // server-side GM visibility, i.e. hides it from players and shows it to
        // GMs, which is precisely backwards.
        // 11686 is the invisible trigger model both engines fall back to
        // (AC: CreatureModel::DefaultInvisibleModel). Scale is its own field
        // here - the GM-scale rationale above applies unchanged.
        dummy->SetDisplayId(11686);
        dummy->SetObjectScale(CAMERA_SCALE);

        // Get the camera onto the client BEFORE handing it control, because the
        // seat is far outside the parked body's sight range and a client can
        // only take control of an object it already has. Farsight is the lever
        // that moves a client's sight ORIGIN — CanSeeOrDetect takes its distance
        // check from Player::GetSightPosition(), i.e. m_seer (Object.cpp) — so
        // ride the seat bot for exactly as long as it takes to push the camera
        // out, then hand PLAYER_FARSIGHT over to the possession below (the field
        // holds one viewpoint; possession needs it free).
        //
        // Releasing that farsight here deliberately does NOT rebuild visibility
        // the way Stop's Follow branch does: a rebuild centred on the body would
        // destroy the just-sent camera client-side a line before we possess it.
        // Once possessed, Unit::IsAlwaysVisibleFor keeps a charmed unit visible
        // to its charmer at any distance, so nothing has to be re-pushed.
        if (fromFollow)
            ClearViewpoint(player, prior.watched);
        if (seat)
        {
            player->SetViewpoint(seat, true);
            player->UpdateVisibilityOf(seat, dummy); // viewPoint-first form here
            player->SetViewpoint(seat, false);
        }

        // vmangos possession primitive (the Eyes-of-the-Beast path): camera,
        // client control, mover swap, react state all live inside ModPossess.
        // It returns void - success shows as the charm guid landing.
        player->ModPossess(dummy, true);
        if (player->GetCharmGuid() != dummy->GetObjectGuid())
        {
            dummy->DespawnOrUnsummon();
            // The follow camera's farsight is already gone; don't leave the
            // watcher rooted and bookkept against a camera that never existed.
            if (fromFollow)
                Stop(player);
            return Refuse(whyNot, "Possession failed.");
        }

        // Mandatory: the possess branch just set UNIT_FLAG_DISABLE_MOVE on the
        // player body, which MotionMaster::MovePoint (MotionMaster.cpp:488) and
        // MoveSplinePath (MotionMaster.cpp:506) refuse — the bot AI could no
        // longer move the body. Clear it so the body keeps clearing.
        // (vmangos' ModPossess does not appear to raise it on the body, but
        // clearing is a free no-op and keeps the invariant explicit.)
        player->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISABLE_MOVE);

        // Follow mode roots the body so blind movement keys can't walk it off.
        // That root belongs to the camera we just replaced — drop it here,
        // because the Stop that normally does so was skipped on purpose.
        if (fromFollow)
            player->SetControlled(false, UNIT_STATE_ROOT);

        // AFTER possession the dummy is client-controlled, so SetFly routes
        // its move-mode packet to the controlling session with the proper
        // order counter. (Pre-possession the packet path differs — keep order.)
        float const speed = DcSettings::GetFloat(player, "SpectateSpeed");
        dummy->SetFly(true);
        // No MOVE_FLIGHT speed type on 1.12 - the flying camera rides the
        // run/swim speeds.
        dummy->SetSpeed(MOVE_RUN, speed, true);
        dummy->SetSpeed(MOVE_SWIM, speed, true);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            // Reuse the Follow entry rather than erase-and-insert: gmModeApplied
            // has to survive the switch or a `.dc test watch` GM is left hidden
            // for good once the free camera stops.
            SpectatorState& state = g_spectators[player->GetObjectGuid()];
            state.mode = Mode::Free;
            state.dummy = dummy->GetObjectGuid();
            state.watched = ObjectGuid::Empty;
            state.retargetAccumMs = 0;
        }
        if (!fromFollow)
            g_activeCount.fetch_add(1, std::memory_order_release);

        Announce(player, seat
            ? "Spectator camera on at " + std::string(seat->GetName()) +
                  " — your character stays under bot control. Toggle again to return."
            : "Spectator camera on — your character stays under bot control. Toggle again to return.");
        return true;
    }

    bool StartFollow(Player* player, Player* target, std::string* whyNot)
    {
        if (!player)
            return Refuse(whyNot, "No player.");

        // Read the arrival request's GM-mode flag BEFORE any Stop below: Stop
        // disarms a pending request, and losing this flag would strand the GM
        // invisible after the camera ends.
        bool gmModeApplied = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto pending = g_pending.find(player->GetObjectGuid());
            if (pending != g_pending.end())
                gmModeApplied = pending->second.gmModeApplied;
        }

        // Switching modes is a legitimate one-step operation: tear the free
        // camera down first rather than making every caller sequence it (and
        // rather than letting SetViewpoint silently refuse the already-latched
        // PLAYER_FARSIGHT field).
        if (IsActive(player))
            Stop(player);

        if (!CommonStartChecks(player, whyNot))
            return false;

        if (!target)
            target = ResolveWatchTarget(player);
        if (!target)
            return Refuse(whyNot, "No bot here to follow.");
        if (!IsWatchable(player, target))
            return Refuse(whyNot, "That target can't be followed from here.");

        // Farsight: the camera rides the target and the client renders its
        // ordinary server-driven movement. No client control is granted, which
        // is exactly what makes this smooth where possession is not.
        player->SetViewpoint(target, true);

        // The body keeps its own movement input while farsighted (Mind Vision
        // behaves the same way), so root it — otherwise a blind WASD tap walks
        // the watcher through the instance while they stare at the party.
        // SetControlled, not Unit::SetRooted: the latter is protected, and
        // reaching for it would mean a core edit for a convenience root.
        player->SetControlled(true, UNIT_STATE_ROOT);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            SpectatorState& state = g_spectators[player->GetObjectGuid()];
            state.mode = Mode::Follow;
            state.watched = target->GetObjectGuid();
            state.retargetAccumMs = 0;
            // Carry over a GM-mode flip made by the watch entry point (snapshot
            // above), so Stop puts the GM back the way it found them.
            state.gmModeApplied = gmModeApplied;

            // This request is now fulfilled — consume it.
            if (g_pending.erase(player->GetObjectGuid()))
                g_activeCount.fetch_sub(1, std::memory_order_release);
        }
        g_activeCount.fetch_add(1, std::memory_order_release);

        Announce(player, "Following " + std::string(target->GetName()) +
                 " — your view rides the bot. `.dc spectate follow` again to stop.");
        return true;
    }

    void Stop(Player* player)
    {
        if (!player)
            return;

        SpectatorState state;
        {
            std::lock_guard<std::mutex> lock(g_mutex);

            // Drop any unfulfilled arrival request too: a Stop means the human
            // changed their mind, and an armed camera must not fire later.
            if (g_pending.erase(player->GetObjectGuid()))
                g_activeCount.fetch_sub(1, std::memory_order_release);

            auto it = g_spectators.find(player->GetObjectGuid());
            if (it == g_spectators.end())
                return;
            state = it->second;
            g_spectators.erase(it);
        }
        g_activeCount.fetch_sub(1, std::memory_order_release);

        if (state.mode == Mode::Follow)
        {
            ClearViewpoint(player, state.watched);
            player->SetControlled(false, UNIT_STATE_ROOT);
            player->UpdateVisibilityForPlayer();
        }
        else if (Creature* dummy = ObjectAccessor::GetCreature(*player, state.dummy))
        {
            player->ModPossess(dummy, false);
            dummy->DespawnOrUnsummon();
        }
        else if (player->GetCharmGuid() == state.dummy)
        {
            // Stale half-state: the dummy is gone but the charm bookkeeping
            // still points at it. Uncharm() would no-op here (it needs the
            // charm to still resolve), so replay the caster side of
            // ModPossess(false) by hand to restore client control.
            player->SetMover(nullptr);
            player->SetCharm(nullptr);
            player->UpdateControl();
            player->GetCamera().ResetView();
            player->RemovePetActionBar();
        }

        // Put a GM we un-hid back the way we found them, so a watcher can't
        // wander off still invisible and untargetable without noticing.
        if (state.gmModeApplied && player->IsGameMaster())
            player->SetGameMaster(false);

        // Deliberately no motion-master surgery on the body here: DC's own AI
        // re-issues movement on its next tick. Per the stale-MoveFollow lesson,
        // only clean up what WE installed — the possession and the dummy.

        Announce(player, "Spectator camera off.");
    }

    void NotifyWatchedLoggingOut(Player* watched)
    {
        if (!watched || g_activeCount.load(std::memory_order_acquire) == 0)
            return;

        ObjectGuid const watchedGuid = watched->GetObjectGuid();

        // Collect first, act second: Stop/SetViewpoint must not run under the
        // lock (they call back into core visibility code), and the viewer set
        // is at most a handful of entries.
        std::vector<ObjectGuid> viewers;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto const& entry : g_spectators)
                if (entry.second.mode == Mode::Follow && entry.second.watched == watchedGuid)
                    viewers.push_back(entry.first);
        }

        for (ObjectGuid const& viewerGuid : viewers)
        {
            Player* viewer = ObjectAccessor::FindConnectedPlayer(viewerGuid);
            if (!viewer)
                continue;

            // Hand the camera to another live bot when there is one, so a
            // watcher doesn't get kicked out of the run just because one member
            // logged out. Otherwise end the camera cleanly.
            Player* next = ResolveWatchTarget(viewer, watched);
            if (next)
            {
                viewer->SetViewpoint(watched, false);
                viewer->SetViewpoint(next, true);
                viewer->UpdateVisibilityForPlayer();

                std::lock_guard<std::mutex> lock(g_mutex);
                auto it = g_spectators.find(viewerGuid);
                if (it != g_spectators.end())
                    it->second.watched = next->GetObjectGuid();
            }
            else
            {
                Stop(viewer);
            }
        }
    }

    bool Toggle(Player* player, std::string* whyNot)
    {
        // Follow -> Free -> off, in that order. A watch camera is how a GM gets
        // into the instance at all, so the toggle moves them INTO the free
        // camera at the seat they were watching instead of dropping them back
        // in a body parked at the entrance; press it again to stop. Ending a
        // follow camera in ONE step is still `.dc spectate follow` (or
        // `.dc test watch off`, which also un-hides the GM).
        if (IsActive(player) && !IsFollowing(player))
        {
            Stop(player);
            return true;
        }
        return Start(player, whyNot);
    }

    bool ToggleFollow(Player* player, Player* target, std::string* whyNot)
    {
        if (IsFollowing(player))
        {
            Stop(player);
            return true;
        }
        return StartFollow(player, target, whyNot);
    }

    void RequestFollowOnArrival(Player* viewer, ObjectGuid tankGuid, bool gmModeApplied)
    {
        if (!viewer)
            return;

        std::lock_guard<std::mutex> lock(g_mutex);
        auto const res = g_pending.insert({viewer->GetObjectGuid(), PendingFollow()});
        if (res.second)
            g_activeCount.fetch_add(1, std::memory_order_release);

        PendingFollow& pending = res.first->second;
        pending.tank = tankGuid;
        // A repeat request must not forget that the FIRST one hid the GM.
        pending.gmModeApplied = pending.gmModeApplied || gmModeApplied;
        pending.ageMs = 0;
    }

    void Tick(Player* player, uint32 diff)
    {
        if (!player || g_activeCount.load(std::memory_order_acquire) == 0)
            return;

        // --- pending arrival ------------------------------------------------
        ObjectGuid pendingTank;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_pending.find(player->GetObjectGuid());
            if (it != g_pending.end())
            {
                it->second.ageMs += diff;
                if (it->second.ageMs >= PENDING_FOLLOW_TIMEOUT_MS)
                {
                    g_pending.erase(it);
                    g_activeCount.fetch_sub(1, std::memory_order_release);
                }
                else
                    pendingTank = it->second.tank;
            }
        }

        if (!pendingTank.IsEmpty())
        {
            // Wait for the loading screen to end and the viewer to actually be
            // standing on the tank's map before turning the camera on.
            Player* tank = ObjectAccessor::FindConnectedPlayer(pendingTank);
            if (tank && !player->IsBeingTeleported() && player->IsInWorld() &&
                tank->IsInWorld() && tank->FindMap() == player->FindMap())
            {
                std::string whyNot;
                if (!StartFollow(player, tank, &whyNot))
                {
                    // Refused on arrival (dead, disabled, …): report once and
                    // disarm rather than retrying every tick forever.
                    Announce(player, whyNot);
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (g_pending.erase(player->GetObjectGuid()))
                        g_activeCount.fetch_sub(1, std::memory_order_release);
                }
            }
            return;
        }

        // --- live follow camera ----------------------------------------------
        SpectatorState state;
        if (!LookupState(player, state) || state.mode != Mode::Follow)
            return;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_spectators.find(player->GetObjectGuid());
            if (it == g_spectators.end())
                return;
            it->second.retargetAccumMs += diff;
            if (it->second.retargetAccumMs < RETARGET_INTERVAL_MS)
                return;
            it->second.retargetAccumMs = 0;
        }

        Player* watched = ObjectAccessor::FindConnectedPlayer(state.watched);
        if (IsWatchable(player, watched))
            return;

        // The seat went bad — the tank died, left the map, or logged out. Hand
        // the camera to whoever is still driving rather than stranding the
        // watcher on a corpse.
        Player* next = ResolveWatchTarget(player);
        if (!next || next->GetObjectGuid() == state.watched)
            return;

        ClearViewpoint(player, state.watched);
        player->SetViewpoint(next, true);
        player->UpdateVisibilityForPlayer();

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_spectators.find(player->GetObjectGuid());
            if (it != g_spectators.end())
                it->second.watched = next->GetObjectGuid();
        }

        Announce(player, "Camera handed off to " + std::string(next->GetName()) + ".");
    }

    void BeginBotAiMoverWindow(Player* player)
    {
        if (!player || g_activeCount.load(std::memory_order_acquire) == 0)
            return;

        SpectatorState state;
        if (!LookupState(player, state) || state.mode != Mode::Free)
            return;

        // Only swap when the mover really is our dummy — if some other charm
        // owns the mover, leave it alone.
        Unit* mover = player->GetMover();
        if (mover && mover->GetObjectGuid() == state.dummy)
            player->SetMover(player);   // this core's SetMover IS the raw swap - no side effects to avoid
    }

    void EndBotAiMoverWindow(Player* player)
    {
        if (!player || g_activeCount.load(std::memory_order_acquire) == 0)
            return;

        SpectatorState state;
        if (!LookupState(player, state) || state.mode != Mode::Free)
            return;     // Stop() ran mid-window and already restored control

        if (player->GetMover() != player)
            return;     // window never opened (foreign charm) — nothing to undo

        if (Creature* dummy = ObjectAccessor::GetCreature(*player, state.dummy))
            player->SetMover(dummy);
        // Dummy unresolvable while still registered: leave the mover on the
        // player; the teardown safety net will Stop() and clear the half-state.
    }
}

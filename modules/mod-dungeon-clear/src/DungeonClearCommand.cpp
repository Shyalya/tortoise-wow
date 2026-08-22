/*
 * mod-dungeon-clear — DungeonClearCommand.cpp
 *
 * Slash command `.dc on|off|skip|status|bosses`. A convenience entry point that
 * works with zero config (unlike the chat keywords, which need the
 * "dungeon clear" strategy applied — see DungeonClearModule.cpp).
 *
 * Each subcommand dispatches the matching DungeonClear action ("dc on", …) to
 * the issuing player's tank bot(s) via PlayerbotAI::DoSpecificAction. The
 * actions already self-authorize (owner must be a real player in the bot's
 * group) and self-gate (e.g. `dc on` is tank-only), so we carry the issuing
 * player as the Event owner and let the existing action logic decide.
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Group.h"
#include "InstanceSaveMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "PlayerbotAIConfig.h"

#include "DungeonClearDispatch.h"
#include "TestRun/DcTestDriver.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestGearTiers.h"
#include "TestRun/DcTestPlan.h"
#include "TestRun/DcTestPlanManager.h"
#include "TestRun/DcTestRunManager.h"
#include "Util/DcSpectator.h"
#include "Util/DcWatchHop.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearUtil.h"

// Tortoise port: the typed command-table namespace is gone with the tables
// (see the AllCommandScript below); nothing else lived in it.

namespace
{
    bool RunDcCommand(ChatHandler* handler, std::string const& action, std::string const& param = "")
    {
        Player* issuer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!issuer)
        {
            handler->SendSysMessage("This command must be used in-game.");
            return true;
        }

        if (!DungeonClearDispatch::DispatchToTankBots(issuer, action, param))
            handler->SendSysMessage("No tank bot found in your group.");

        return true;
    }

    // Format a resolved raw double per the registry type, so the printout reads
    // the way the conf line is written (true/false, ints, floats).
    std::string FormatDcValue(DcSettingDef const& d, double raw)
    {
        switch (d.type)
        {
            case DcType::Bool:
                return raw != 0.0 ? "true" : "false";
            case DcType::UInt:
            case DcType::Int:
                return Acore::StringFormat("{}", static_cast<int64>(std::lround(raw)));
            case DcType::Float:
            default:
                return Acore::StringFormat("{:.2f}", raw);
        }
    }

    // Dumps every DungeonClear tunable as the module actually reads it: the live
    // conf/default value, plus the per-run effective value when the issuer's run
    // has an addon override active. This is a pure read of sConfigMgr through the
    // DcSettings accessor, so it reflects exactly what the AI sees this tick —
    // use it to confirm whether a conf edit took effect (no `.reload config`).
    bool HandleConfig(ChatHandler* handler)
    {
        Player* issuer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!issuer)
        {
            handler->SendSysMessage("This command must be used in-game.");
            return true;
        }

        // Resolve the run owner (leader tank) so we can surface per-run overrides.
        // Empty when the issuer isn't in a DC run — then only conf/defaults show.
        Player* leader = DcLeaderSignal::FindLeaderTank(issuer);
        ObjectGuid const runOwner = leader ? leader->GetObjectGuid() : ObjectGuid::Empty;

        handler->SendSysMessage(
            "DungeonClear config (effective values; * = addon override, H = heroic default):");
        for (DcSettingDef const& d : kDcSettings)
        {
            // confVal reads with no owner, so it never picks up the heroic layer:
            // it is the "what this would be outside the run" baseline both markers
            // are shown against. effVal is what the run is actually using.
            double const confVal = DcSettings::GetEffectiveRaw(ObjectGuid::Empty, d);
            double const effVal  = DcSettings::GetEffectiveRaw(runOwner, d);
            bool const overridden =
                !runOwner.IsEmpty() && DcSettings::HasOverride(runOwner, d.key);

            std::string line;
            if (overridden)
                line = Acore::StringFormat("  * DungeonClear.{} = {} (conf {})",
                                           d.key, FormatDcValue(d, effVal),
                                           FormatDcValue(d, confVal));
            else if (effVal != confVal)
                // Not overridden but not the conf value either: the run is heroic
                // and this row carries a heroic default. Without the marker the
                // number looks like the conf line is being ignored.
                line = Acore::StringFormat("  H DungeonClear.{} = {} (normal {})",
                                           d.key, FormatDcValue(d, effVal),
                                           FormatDcValue(d, confVal));
            else
                line = Acore::StringFormat("    DungeonClear.{} = {}",
                                           d.key, FormatDcValue(d, confVal));
            handler->SendSysMessage(line);
        }
        return true;
    }

    // Spectator camera toggle. Acts on the ISSUER directly (session plumbing,
    // not bot behavior) — it must NOT go through DispatchToTankBots or the
    // action pipeline: the issuer may not even be the tank, and the camera
    // belongs to their session alone. See Util/DcSpectator.h.
    //
    // Bare `.dc spectate` is the free-flying camera; `.dc spectate follow
    // [name]` rides a bot instead. From a live follow camera (which is what
    // `.dc test watch` leaves you in) bare `.dc spectate` steps INTO the free
    // camera at the bot you were watching rather than ending the camera — press
    // it again to stop. Neither needs a group — that gate lives only
    // in the addon's party-channel transport, which is why a GM watching from
    // outside the party has to type the command.
    bool HandleSpectate(ChatHandler* handler, std::string const& param)
    {
        Player* issuer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!issuer)
        {
            handler->SendSysMessage("This command must be used in-game.");
            return true;
        }

        std::string arg = param;
        std::string sub;
        std::string name;
        {
            std::istringstream in(arg);
            in >> sub >> name;
        }
        std::transform(sub.begin(), sub.end(), sub.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        std::string whyNot;

        // Seat selection. `next`/`prev` walk every bot in the instance, not just
        // tanks — one key to start watching and to move down the party.
        if (sub == "next" || sub == "n")
        {
            if (!DcSpectator::CycleFollow(issuer, +1, &whyNot))
                handler->SendSysMessage(whyNot);
            return true;
        }
        if (sub == "prev" || sub == "previous" || sub == "p")
        {
            if (!DcSpectator::CycleFollow(issuer, -1, &whyNot))
                handler->SendSysMessage(whyNot);
            return true;
        }
        if (sub == "list" || sub == "who")
        {
            handler->SendSysMessage(DcSpectator::RosterText(issuer));
            return true;
        }

        if (sub == "follow")
        {
            // Named seat: switch to it, whether or not a camera is already up.
            // Bare `follow` stays a toggle — that is how you turn the camera off.
            if (!name.empty())
            {
                Player* target = DcSpectator::FindWatchableByName(issuer, name);
                if (!target)
                {
                    handler->PSendSysMessage(
                        "No watchable bot matching '{}' here. Try `.dc spectate list`.", name);
                    return true;
                }
                if (!DcSpectator::SeatFollow(issuer, target, &whyNot))
                    handler->SendSysMessage(whyNot);
                return true;
            }

            if (!DcSpectator::ToggleFollow(issuer, nullptr, &whyNot))
                handler->SendSysMessage(whyNot);
            return true;
        }

        if (!sub.empty() && sub != "free")
        {
            handler->SendSysMessage(
                "Usage: .dc spectate [follow [name] | next | prev | list]");
            return true;
        }

        if (!DcSpectator::Toggle(issuer, &whyNot))
            handler->SendSysMessage(whyNot);
        return true;
    }

    // --- `.dc test watch` instance-bind bookkeeping --------------------------
    //
    // Entering a run's instance binds the watcher to it (see HandleTestWatch),
    // and that bind outlives the run. Remember the ones WE made so the next
    // watch — and `watch off` — can release exactly those and never a lockout
    // the human earned themselves.
    //
    // World thread only: every writer is a chat command handler. Entries are
    // keyed by GUID and released on the next watch, so a watcher who logs out
    // mid-run leaves at most one stale row, which the next hop clears (an
    // unbind of a bind that no longer exists is a no-op).
    std::unordered_map<ObjectGuid, std::vector<DcWatchHop::Bind>> g_watchBinds;

    std::vector<DcWatchHop::Bind> HeldWatchBinds(Player* gm)
    {
        auto it = g_watchBinds.find(gm->GetObjectGuid());
        return it == g_watchBinds.end() ? std::vector<DcWatchHop::Bind>() : it->second;
    }

    void ReleaseWatchBinds(Player* gm, std::vector<DcWatchHop::Bind> const& binds)
    {
        if (binds.empty())
            return;

        auto& held = g_watchBinds[gm->GetObjectGuid()];
        for (DcWatchHop::Bind const& b : binds)
        {
            sInstanceSaveMgr->PlayerUnbindInstance(gm->GetObjectGuid(), b.mapId,
                                                   Difficulty(b.difficulty), true, gm);
            held.erase(std::remove_if(held.begin(), held.end(),
                                      [&b](DcWatchHop::Bind const& h)
                                      {
                                          return h.mapId == b.mapId &&
                                                 h.difficulty == b.difficulty &&
                                                 h.instanceId == b.instanceId;
                                      }),
                       held.end());
        }

        if (held.empty())
            g_watchBinds.erase(gm->GetObjectGuid());
    }
}

// Tortoise port: this core's ChatCommand table binds ChatHandler MEMBER
// function pointers, which a module cannot provide, so the AzerothCore
// command tables that stood here are gone. The module system's
// AllCommandScript hook runs before the table lookup and may claim a command
// outright - and since every handler below already works on plain strings,
// claiming ".dc" and tokenising the tail loses nothing the typed parser
// provided. Security and console gating match the old tables' annotations:
// everything under "dc test" was SEC_GAMEMASTER and console-allowed (except
// watch, which needs a session for its camera), the player-facing verbs were
// SEC_PLAYER and session-only.
class dungeon_clear_command_script : public AllCommandScript
{
public:
    dungeon_clear_command_script() : AllCommandScript("dungeon_clear_command_script") {}

    // The hook's contract at the call site: returning false claims the
    // command, true lets the core's own table handle it.
    bool CanExecuteCommand(ChatHandler* handler, char const* cmd, char const* argsIn) override
    {
        if (!cmd || strcmp(cmd, "dc") != 0)
            return true;

        std::string rest = argsIn ? argsIn : "";
        auto const takeWord = [&rest]() -> std::string
        {
            size_t const b = rest.find_first_not_of(' ');
            if (b == std::string::npos) { rest.clear(); return std::string(); }
            size_t const e = rest.find(' ', b);
            std::string word = rest.substr(b, e == std::string::npos ? std::string::npos : e - b);
            rest = e == std::string::npos ? std::string() : rest.substr(e + 1);
            return word;
        };

        bool const fromConsole = !handler->GetSession();
        bool const isGm = fromConsole ||
            handler->GetSession()->GetSecurity() >= SEC_GAMEMASTER;

        std::string const sub = takeWord();

        if (sub == "test")
        {
            if (!isGm)
                { handler->SendSysMessage("You are not allowed to do that."); return false; }
            std::string const t2 = takeWord();
            if (t2 == "start")       HandleTestStart(handler, rest);
            else if (t2 == "status") HandleTestStatus(handler);
            else if (t2 == "stop")   HandleTestStop(handler, rest);
            else if (t2 == "list")   HandleTestList(handler);
            else if (t2 == "gear")   HandleTestGear(handler, rest);
            else if (t2 == "watch")
            {
                if (fromConsole) handler->SendSysMessage("dc test watch needs a session.");
                else HandleTestWatch(handler, rest);
            }
            else if (t2 == "plan")
            {
                std::string const t3 = takeWord();
                if (t3 == "start")       HandleTestPlanStart(handler, rest);
                else if (t3 == "status") HandleTestPlanStatus(handler);
                else if (t3 == "stop")   HandleTestPlanStop(handler, rest);
                else handler->SendSysMessage("dc test plan: start | status | stop");
            }
            else handler->SendSysMessage("dc test: start | status | stop | list | gear | watch | plan");
            return false;
        }

        if (fromConsole)
            { handler->SendSysMessage("dc needs a session; only dc test runs from the console."); return false; }

        if (sub == "on")            HandleOn(handler);
        else if (sub == "off")      HandleOff(handler);
        else if (sub == "skip")     HandleSkip(handler);
        else if (sub == "pause")    HandlePause(handler);
        else if (sub == "pull")     HandlePull(handler, rest);
        else if (sub == "status")   HandleStatus(handler, rest);
        else if (sub == "bosses")   HandleBosses(handler, rest);
        else if (sub == "go")       HandleGo(handler, rest);
        else if (sub == "config")   HandleConfig(handler); // display-only, takes no arguments
        else if (sub == "spectate") HandleSpectate(handler, rest);
        else handler->SendSysMessage("dc: on | off | skip | pause | pull | status | bosses | go | config | spectate | test");
        return false;
    }

    static bool HandleOn(ChatHandler* handler)     { return RunDcCommand(handler, "dc on"); }
    static bool HandleOff(ChatHandler* handler)    { return RunDcCommand(handler, "dc off"); }
    static bool HandleSkip(ChatHandler* handler)   { return RunDcCommand(handler, "dc skip"); }
    static bool HandlePause(ChatHandler* handler)  { return RunDcCommand(handler, "dc pause"); }
    static bool HandlePull(ChatHandler* handler, std::string const& param) { return RunDcCommand(handler, "dc pull", param); }
    static bool HandleStatus(ChatHandler* handler, std::string const& param) { return RunDcCommand(handler, "dc status", param); }
    static bool HandleBosses(ChatHandler* handler, std::string const& param) { return RunDcCommand(handler, "dc bosses", param); }
    static bool HandleGo(ChatHandler* handler, std::string const& targetBoss) { return RunDcCommand(handler, "dc go", targetBoss); }

    // --- `.dc test` — the automated test-run harness ------------------------
    // These act on DcTestRunManager directly (never DispatchToTankBots: the
    // whole point is that the GM is NOT in the bot party).

    // The issuing GM for a start: the in-game player when there is one, else
    // the headless driver (console / dashboard path). nullptr with a pending
    // message sent when the driver is still logging in — the caller retries.
    static Player* ResolveTestIssuer(ChatHandler* handler)
    {
        if (Player* issuer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr)
            return issuer;

        std::string whyPending;
        if (DcTestDriver::EnsureOnline(&whyPending))
            return DcTestDriver::Get();
        handler->SendSysMessage(whyPending);
        return nullptr;
    }

    // `.dc test start <dungeon> [heroic] [level=N] [seed=N] [ilvl=N|none]
    // [quality=rare|epic|…]` — random comp drawn from the addclass pool;
    // dungeon is a registry token (`.dc test list`) or a mapId. ilvl/quality cap
    // the gear the bots are rolled with, defaulting to the AiPlayerbot.AutoGear*
    // conf values; `.dc test gear <dungeon>` lists the ceilings worth using for
    // a given dungeon.
    //
    // `.dc test start <dungeon> party=Tank,Heal,D1,D2,D3 [heroic]` — a hand-picked
    // party of REAL player characters instead (roles positional). level=, seed=
    // and the gear options are all meaningless there: the level comes from the
    // characters, the roster is the comp, and real characters are never re-geared.
    static bool HandleTestStart(ChatHandler* handler, std::string const& args)
    {
        Player* issuer = ResolveTestIssuer(handler);
        if (!issuer)
            return true;

        static constexpr char const* kUsage =
            "Usage: .dc test start <dungeon> [heroic] [level=N] [seed=N] [ilvl=N|none] "
            "[quality=normal|uncommon|rare|epic|legendary]\n"
            "   or: .dc test start <dungeon> party=Tank,Heal,Dps1,Dps2,Dps3 [heroic]";

        std::string token;
        std::string party;
        uint32 level = 0;
        uint32 seed = 0;  // 0 = roll a random comp; seed=N replays a specific one
        DcTestGearTiers::Spec gear;
        bool heroic = false;
        std::istringstream in{args};
        std::string word;
        while (in >> word)
        {
            if (word.rfind("level=", 0) == 0)
                level = static_cast<uint32>(std::strtoul(word.c_str() + 6, nullptr, 10));
            else if (word.rfind("seed=", 0) == 0)
                seed = static_cast<uint32>(std::strtoul(word.c_str() + 5, nullptr, 10));
            else if (word.rfind("ilvl=", 0) == 0)
            {
                bool ok = false;
                gear.ilvl = DcTestGearTiers::ParseIlvl(word.substr(5), &ok);
                if (!ok)
                {
                    handler->SendSysMessage("ilvl must be 1-400, or 'none' for no limit.");
                    return true;
                }
            }
            else if (word.rfind("quality=", 0) == 0)
            {
                gear.quality = DcTestGearTiers::ParseQuality(word.substr(8));
                if (gear.quality == 0)
                {
                    handler->SendSysMessage(
                        "quality must be normal|uncommon|rare|epic|legendary (or 1-5).");
                    return true;
                }
            }
            else if (word.rfind("party=", 0) == 0)
                party = word.substr(6);
            else if (word == "heroic")
                heroic = true;
            else if (token.empty())
                token = word;
            else
            {
                handler->SendSysMessage(kUsage);
                return true;
            }
        }
        if (token.empty())
        {
            handler->SendSysMessage(kUsage);
            return true;
        }

        std::string msg;
        if (!party.empty())
        {
            // Reject rather than silently ignore: somebody passing level= with a
            // roster believes it will be applied, and applying it would mean
            // relevelling their character.
            if (level || seed || !gear.IsDefault())
            {
                handler->SendSysMessage(
                    "level=, seed= and ilvl=/quality= do not apply to party= runs: the level comes "
                    "from the characters (they are never relevelled or re-geared) and the roster "
                    "is the comp.");
                return true;
            }
            DcTestRunManager::Instance().StartRoster(issuer, token, party, heroic, &msg);
        }
        else
            DcTestRunManager::Instance().Start(issuer, token, level, seed, heroic, gear, &msg);
        handler->SendSysMessage(msg);
        return true;
    }

    static bool HandleTestStatus(ChatHandler* handler)
    {
        handler->SendSysMessage(DcTestRunManager::Instance().StatusText());
        if (DcTestPlanManager::Instance().HasActivePlans())
            handler->SendSysMessage(DcTestPlanManager::Instance().StatusText());
        return true;
    }

    // `.dc test stop [selector]` — bare = the single active run (errors listing
    // runs when >1 active); "all"; an exact runId; or a dungeon token (all its
    // runs). See DcTestRunSelect. "all" also stops every active plan first —
    // otherwise the plan scheduler would relaunch the runs it just aborted.
    static bool HandleTestStop(ChatHandler* handler, std::string const& selector)
    {
        if (selector == "all" && DcTestPlanManager::Instance().HasActivePlans())
        {
            DcTestPlanManager::Instance().StopAll("stopped via .dc test stop all");
            handler->SendSysMessage("stopping all test plans");
        }
        std::string msg;
        DcTestRunManager::Instance().Stop(selector, &msg);
        handler->SendSysMessage(msg);
        return true;
    }

    // `.dc test watch [selector]` — put the GM's camera on a running test.
    //
    // This is the one-command answer to "let me watch a run": the GM is NOT in
    // the bot party (by design — see DcTestRunManager), so watching used to
    // mean hand-running `.appear <botname>` then `.dc spectate`, and the addon
    // button refuses outright because its transport is the party channel.
    //
    // Sequence: hide the GM (mobs must not aggro the watcher and corrupt the
    // run), bind + teleport to the run instance's ENTRANCE (the body stays out
    // of the party's way — farsight does the actual watching), and arm the
    // follow camera to start on arrival — the teleport is asynchronous, and the
    // teleport teardown hook deliberately stops any live camera on the way out,
    // so the camera cannot be started here. `.dc test watch off` ends it and
    // puts the GM's own visibility back.
    //
    // Watching a SECOND run is the same command again, with no manual cleanup
    // in between: DcWatchHop works out which binds have to be released and
    // whether the teleport must be forced far, because the bind made for the
    // previous run would otherwise decide where this teleport lands. See
    // Util/DcWatchHop.h for why each of those is load-bearing.
    //
    // `.dc test watch next` hops to the run after the one being watched and
    // wraps, so a plan's worth of concurrent runs can be toured on one command
    // without reading runIds off `.dc test status`. It refuses (and stays put)
    // when there is nowhere to go — see DcTestRunManager::NextWatchTarget.
    static bool HandleTestWatch(ChatHandler* handler, std::string const& selectorArg)
    {
        Player* gm = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!gm)
        {
            handler->SendSysMessage("This command must be used in-game.");
            return true;
        }

        std::string selector(selectorArg);
        if (selector == "off" || selector == "stop")
        {
            // Unconditional: Stop also disarms a request that is still waiting
            // out a loading screen, which IsActive can't see. It announces on
            // its own when a camera was actually running.
            bool const wasActive = DcSpectator::IsActive(gm);
            DcSpectator::Stop(gm);
            if (!wasActive)
                handler->SendSysMessage("No spectator camera running (any pending watch request is cancelled).");

            // Leave the instance system the way we found it. A watcher parked
            // inside a finished test copy holds it open and stays saved to it —
            // on a heroic map, that is the map's daily lockout spent on a run
            // that is already over. Body back out first (the recall position
            // was taken on the way in), then drop the binds we made.
            std::vector<DcWatchHop::Bind> const held = HeldWatchBinds(gm);
            if (!held.empty())
            {
                bool const insideAWatchedCopy =
                    gm->IsInWorld() &&
                    std::any_of(held.begin(), held.end(),
                                [gm](DcWatchHop::Bind const& b)
                                {
                                    return b.mapId == gm->GetMapId() &&
                                           b.instanceId == gm->GetInstanceId();
                                });
                if (insideAWatchedCopy && !gm->IsBeingTeleported())
                {
                    gm->TeleportTo(gm->GetRecallMap(), gm->GetRecallX(), gm->GetRecallY(),
                                   gm->GetRecallZ(), gm->GetRecallO());
                    handler->SendSysMessage("Returned to where you were before watching.");
                }
                ReleaseWatchBinds(gm, held);
            }
            return true;
        }

        ObjectGuid tankGuid;
        std::string msg;
        std::string dungeonToken;
        // "next" tours the live runs instead of naming one: the run after the
        // instance the watcher is standing in, wrapping. It resolves to a
        // target like any other selector, so the hop below is unchanged.
        bool const resolved =
            selector == "next"
                ? DcTestRunManager::Instance().NextWatchTarget(gm, &tankGuid, &msg, &dungeonToken)
                : DcTestRunManager::Instance().WatchTarget(selector, &tankGuid, &msg, &dungeonToken);
        if (!resolved)
        {
            handler->SendSysMessage(msg);
            return true;
        }

        Player* tank = ObjectAccessor::FindConnectedPlayer(tankGuid);
        if (!tank || !tank->IsInWorld())
        {
            handler->SendSysMessage("That run's tank isn't in the world yet — try again in a moment.");
            return true;
        }

        handler->PSendSysMessage("Watching {}", msg);

        Map* tankMap = tank->FindMap();
        Difficulty const tankDiff = tank->GetDifficulty(tankMap->IsRaid());
        InstanceSave* targetSave = sInstanceSaveMgr->GetInstanceSave(tank->GetInstanceId());

        // Key binds the way InstanceSaveMgr stores them: by the SAVE's
        // difficulty, which is already normalised for shared-difficulty maps.
        // PlayerUnbindInstance does NOT re-normalise its argument, so a release
        // keyed off the tank's raw difficulty could miss the row it must drop.
        DcWatchHop::Bind const target{
            tank->GetMapId(),
            // One difficulty on this core; the save carries none to normalise.
            static_cast<uint8>(DUNGEON_DIFFICULTY_NORMAL),
            tank->GetInstanceId()};

        // What the core thinks this watcher is saved to on the run's map right
        // now. It may name a copy we never bound them to (their own lockout),
        // and either way it is what PlayerGetDestinationInstanceId will hand
        // the teleport unless we clear it.
        uint32 boundInstanceId = 0;
        if (InstancePlayerBind* bind = sInstanceSaveMgr->PlayerGetBoundInstance(
                gm->GetObjectGuid(), target.mapId, Difficulty(target.difficulty)))
            if (bind->state)
                boundInstanceId = bind->state->GetInstanceId();

        std::vector<DcWatchHop::Bind> const held = HeldWatchBinds(gm);
        DcWatchHop::Plan const plan = DcWatchHop::Decide(
            {gm->GetMapId(), gm->GetInstanceId()}, target, boundInstanceId, held);

        // Already standing in the run's copy: nothing to teleport, just take
        // the seat.
        if (plan.alreadyThere && gm->IsInWorld())
        {
            std::string whyNot;
            if (!DcSpectator::StartFollow(gm, tank, &whyNot))
                handler->SendSysMessage(whyNot);
            return true;
        }

        // Hopping straight from one run to the next: end the old camera HERE,
        // not inside TeleportTo's teardown hook. Stop undoes a GM-mode flip we
        // made, so letting it fire mid-flight would un-hide the watcher in the
        // middle of the next run AND lose the flag that records the flip. Ask
        // first, stop second, re-apply third.
        bool gmModeApplied = DcSpectator::HiddenByWatch(gm);
        DcSpectator::Stop(gm);

        // Hide the watcher before they land. A visible, targetable GM in the
        // instance pulls aggro and invalidates the very run being observed.
        // DcSpectator::Stop restores this exactly when we were the ones to
        // change it — and only then, so a GM who was already in GM mode of
        // their own accord stays that way.
        if (!gm->IsGameMaster())
        {
            gm->SetGameMaster(true);
            if (!gmModeApplied)
                handler->SendSysMessage("GM mode enabled so the run doesn't see you (restored when you stop).");
            gmModeApplied = true;
        }

        // Release before binding, never bind over the top. PlayerBindToInstance
        // overwrites the row in place without calling RemovePlayer on the save
        // it drops (leaking the watcher onto a finished copy's player list),
        // and its `!bind.perm || permanent` ASSERT would take the server down
        // outright if a heroic lockout ever sat where a normal bind is going.
        ReleaseWatchBinds(gm, plan.release);
        if (plan.bindToTarget && targetSave)
        {
            sInstanceSaveMgr->PlayerBindToInstance(gm->GetObjectGuid(), targetSave,
                                                   !targetSave->CanReset(), gm);
            g_watchBinds[gm->GetObjectGuid()].push_back(target);
        }

        if (tankMap->IsRaid())
            gm->SetRaidDifficulty(tank->GetRaidDifficulty());
        else
            gm->SetDungeonDifficulty(tank->GetDungeonDifficulty());

        // `.recall` (and `watch off`) get the GM back out — but only snapshot
        // the way IN from the outside world. Hopping run-to-run must not
        // overwrite it with a spot inside the instance being left, or there is
        // no longer anywhere to go back to.
        bool const hoppingBetweenRuns =
            gm->IsInWorld() &&
            std::any_of(held.begin(), held.end(),
                        [gm](DcWatchHop::Bind const& b)
                        {
                            return b.mapId == gm->GetMapId() &&
                                   b.instanceId == gm->GetInstanceId();
                        });
        if (!hoppingBetweenRuns)
            gm->SaveRecallPosition();

        // Land at the instance ENTRANCE, not on the tank. Farsight renders from
        // the seer's position, so the camera looks the same either way — but
        // dropping the GM's body into the middle of a live pull puts it in
        // collision range of the party (and of anything the pull picks up), and
        // leaves it standing there once the camera stops. The entrance is the
        // quiet, already-cleared end of the instance.
        //
        // Preference order: the run's own registry row (per-WING for split maps
        // like Dire Maul, where a bare map lookup can't tell the wings apart),
        // then the map's entrance areatrigger, then the tank as a last resort.
        float tx = tank->GetPositionX();
        float ty = tank->GetPositionY();
        float tz = tank->GetPositionZ();
        float to = tank->GetOrientation();
        if (DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(dungeonToken);
            row && row->mapId == tank->GetMapId())
        {
            tx = row->x;
            ty = row->y;
            tz = row->z;
            to = row->o;
        }
        else if (AreaTriggerTeleport const* at = sObjectMgr.GetMapEntranceTrigger(tank->GetMapId()))
        {
            tx = at->destination.x;
            ty = at->destination.y;
            tz = at->destination.z;
            to = at->destination.o;
        }

        // Arm the camera AFTER the teleport call, never before: TeleportTo
        // fires PLAYERHOOK_ON_BEFORE_TELEPORT synchronously, and that hook
        // calls DcSpectator::Stop — which disarms pending requests. Arming
        // first would have the teleport immediately cancel its own camera.
        //
        // forceNewInstance is what makes run-to-run hopping work on the SAME
        // map: TeleportTo's near-teleport branch triggers on the map id alone,
        // so without it the GM would just slide around inside the copy they
        // are trying to leave.
        if (!gm->TeleportTo(tank->GetMapId(), tx, ty, tz, to, 0, nullptr,
                            plan.forceNewInstance))
        {
            if (gmModeApplied)
                gm->SetGameMaster(false);
            // Don't leave a lockout behind for a copy the GM never reached.
            if (plan.bindToTarget && targetSave)
                ReleaseWatchBinds(gm, {target});
            handler->SendSysMessage("Teleport into the run's instance was refused.");
            return true;
        }

        DcSpectator::RequestFollowOnArrival(gm, tankGuid, gmModeApplied);
        return true;
    }

    // --- `.dc test plan` — batched campaigns (N runs, capped concurrency) ----

    // Unlike `.dc test start`, a plan does NOT need its issuer up front: the
    // scheduler re-resolves one per launch and waits out an in-flight driver
    // login. That matters because the very first console/dashboard start is
    // the click that kicks that login off — requiring an issuer here rejected
    // exactly the request that caused the driver to come online.
    static bool HandleTestPlanStart(ChatHandler* handler, std::string const& args)
    {
        Player* issuer = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!issuer)
        {
            std::string why;
            DcTestDriver::Readiness const ready = DcTestDriver::Ensure(&why);
            if (ready == DcTestDriver::Readiness::Unavailable)
            {
                handler->SendSysMessage("Test plan not started: " + why);
                return true;
            }
            issuer = DcTestDriver::Get();  // nullptr while the login is in flight
        }

        DcTestPlan::ParseResult const parsed = DcTestPlan::ParseStartArgs(args);
        if (!parsed.ok)
        {
            handler->SendSysMessage(parsed.err);
            return true;
        }

        std::string msg;
        DcTestPlanManager::Instance().Start(parsed.spec, issuer, &msg);
        handler->SendSysMessage(msg);
        return true;
    }

    static bool HandleTestPlanStatus(ChatHandler* handler)
    {
        handler->SendSysMessage(DcTestPlanManager::Instance().StatusText());
        return true;
    }

    // `.dc test plan stop [planId|all]` — bare = the single active plan.
    static bool HandleTestPlanStop(ChatHandler* handler, std::string const& selector)
    {
        std::string msg;
        DcTestPlanManager::Instance().Stop(selector, &msg);
        handler->SendSysMessage(msg);
        return true;
    }

    static bool HandleTestList(ChatHandler* handler)
    {
        handler->SendSysMessage("Supported test dungeons (.dc test start <token> [heroic]):");
        for (DcTestDungeonRegistry::Row const& row : DcTestDungeonRegistry::All())
            handler->SendSysMessage(Acore::StringFormat(
                "  {:<16} {} (map {}, level {}{})", row.token, row.name, row.mapId,
                row.recommendedLevel,
                row.heroicLevel ? Acore::StringFormat(", heroic {}", row.heroicLevel)
                                : std::string()));
        return true;
    }

    // `.dc test gear <dungeon> [heroic]` — the item-level ceilings worth running
    // that dungeon at, i.e. the same list the dashboard's start form offers.
    // Named raid tiers at the level cap, three steps around the dungeon's own
    // gear below it (see DcTestGearTiers).
    static bool HandleTestGear(ChatHandler* handler, std::string const& args)
    {
        std::string token;
        bool heroic = false;
        std::istringstream in{args};
        std::string word;
        while (in >> word)
        {
            if (word == "heroic")
                heroic = true;
            else if (token.empty())
                token = word;
        }

        DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(token);
        if (!row)
        {
            handler->SendSysMessage("Usage: .dc test gear <dungeon> [heroic] — see .dc test list");
            return true;
        }
        if (heroic && row->heroicLevel == 0)
        {
            handler->SendSysMessage(Acore::StringFormat("'{}' has no heroic mode.", row->token));
            return true;
        }

        uint32 const level = heroic ? row->heroicLevel : row->recommendedLevel;
        handler->SendSysMessage(Acore::StringFormat(
            "{}{} at level {} — ilvl= choices (server default is {}):", row->name,
            heroic ? " heroic" : "", level,
            sPlayerbotAIConfig.autoGearScoreLimit > 0
                ? std::to_string(sPlayerbotAIConfig.autoGearScoreLimit)
                : std::string("unlimited")));
        for (DcTestGearTiers::Choice const& choice : DcTestGearTiers::Ladder(row->mapId, level))
            handler->SendSysMessage(Acore::StringFormat("  ilvl={:<4} {}", choice.ilvl, choice.label));
        handler->SendSysMessage("  ilvl=none  no limit");
        return true;
    }
};

void AddSC_dungeon_clear_command()
{
    new dungeon_clear_command_script();
}

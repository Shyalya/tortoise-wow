/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestRunJob.h"
#include "Ai/Dungeon/DungeonClear/Util/NavmeshSnap.h"
#include "Ai/Dungeon/DungeonClear/Util/DcEncounterMask.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <set>

#include "CharacterCache.h"
#include "Chat.h"
#include "Creature.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "InstanceSaveMgr.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "World.h"

#include "AiFactory.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "Util.h"  // urand lives here on this engine

#include "DcStrategyGate.h"
#include "Ai/Dungeon/DungeonClear/Action/DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "TestRun/DcDiagSnapshot.h"
#include "TestRun/DcTestComp.h"
#include "Config.h"
#include <cstdlib>
#include <fstream>

// Characters that took a claim, were handed to AddPlayerBot and never entered
// the world. The claim walks the player cache in guid order, so the first
// eligible character of a class is picked run after run; when that one cannot
// log in, every run needing the class fails identically. Live 2026-09-04
// 08:22-09:52: guid 886 "failed to enter world" 7 times, 7 of 18 ladder starts
// lost at spawning_bots.
//
// Was session-local on purpose so a restart re-tries it. Measured 2026-09-05:
// the same guid 886 (Sirina, the character the auction bot lists its wares
// under) failed 14 times in 14 restarts - the retry never succeeds and every
// restart costs one run slot for 180 s. So the set is now PERSISTED next to
// the roster file (dc_login_failed.txt, one guid per line, `#` comments),
// loaded on first use and appended on every new failure. Delete a line there
// after repairing the character to let the pool try it again.
static std::unordered_set<uint32> g_loginFailedGuids;

namespace
{
    std::string LoginFailedGuidsFile()
    {
        std::string const roster = sConfig.GetStringDefault("DungeonClear.RosterFile", "");
        if (roster.empty())
            return "";
        std::string::size_type const slash = roster.find_last_of("/\\");
        std::string const dir = slash == std::string::npos ? std::string(".") : roster.substr(0, slash);
        return dir + "/dc_login_failed.txt";
    }

    void LoadLoginFailedGuidsOnce()
    {
        static bool loaded = false;
        if (loaded)
            return;
        loaded = true;
        std::string const path = LoginFailedGuidsFile();
        if (path.empty())
            return;
        std::ifstream in(path);
        if (!in.is_open())
            return;
        std::string line;
        uint32 n = 0;
        while (std::getline(in, line))
        {
            std::string::size_type const hash = line.find('#');
            if (hash != std::string::npos)
                line.erase(hash);
            uint32 const guid = static_cast<uint32>(std::strtoul(line.c_str(), nullptr, 10));
            if (guid && g_loginFailedGuids.insert(guid).second)
                ++n;
        }
        if (n)
            LOG_INFO("playerbots.dungeonclear",
                     "TESTRUN login blacklist: {} character(s) from {} will not be claimed", n, path);
    }

    void RememberLoginFailedGuid(uint32 guid, std::string const& runId, std::string const& role)
    {
        std::string const path = LoginFailedGuidsFile();
        if (path.empty())
            return;
        std::ofstream out(path, std::ios::app);
        if (!out.is_open())
            return;
        out << guid << "    # " << role << " slot, never entered the world in run " << runId
            << " (" << TimeToTimestampStr(time(nullptr)) << ")\n";
    }
}

namespace
{
    // Stage timeouts: a setup stage overrunning these is itself the failure.
    // 180s, not 60: with ten groups cycling, up to fifty bot logins queue up
    // at once and they all share one character-database queue. Sixty seconds
    // was enough for a single group and turned into a stream of
    // "bots did not finish logging in" setup failures under parallel load -
    // the logins were not failing, just still in line.
    constexpr uint32 SPAWN_TIMEOUT_MS = 180 * 1000;
    // 180s, not 60. Provisioning gives every bot its level, talents, gear,
    // food and ammo; under an AddressSanitizer build the whole server runs
    // two to three times slower and six of eight groups timed out here at
    // ~70s while doing nothing wrong. The window only bounds a setup that
    // has genuinely wedged, so a generous one costs nothing.
    constexpr uint32 PROVISION_TIMEOUT_MS = 180 * 1000;
    constexpr uint32 GROUP_TIMEOUT_MS = 30 * 1000;
    // Covers BOTH teleport waves (leader, then the rest — see TickTeleporting),
    // and the stage clock does not restart between them.
    constexpr uint32 TELEPORT_TIMEOUT_MS = 45 * 1000;
    constexpr uint32 START_TIMEOUT_MS = 60 * 1000;   // same reason as above

    constexpr uint32 MONITOR_STEP_MS = 1000;

    // How much closer the party must get to its target for the sample to count
    // as ground gained. Above the idle jitter of a bot settling on a spot, so a
    // parked tank drifting a few centimetres can't hold the watchdog open
    // forever; far below any real leg of travel.
    constexpr float DC_TESTRUN_PROGRESS_EPSILON_YD = 1.0f;

    uint64 NowUnixMs()
    {
        return static_cast<uint64>(std::time(nullptr)) * 1000;
    }

    std::string MakeRunId()
    {
        static uint32 counter = 0;
        std::time_t const now = std::time(nullptr);
        std::tm tmBuf{};
        // localtime_s nimmt (tm*, time_t*), localtime_r (time_t*, tm*) - die
        // Reihenfolge ist vertauscht, ein #define-Alias waere hier falsch.
#if defined(_MSC_VER)
        localtime_s(&tmBuf, &now);
#else
        localtime_r(&now, &tmBuf);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "tr-%Y%m%d-%H%M%S", &tmBuf);
        return std::string(buf) + "-" + std::to_string(++counter);
    }

    char const* ClassToken(uint8 classId)
    {
        switch (classId)
        {
            case 1:  return "warrior";
            case 2:  return "paladin";
            case 3:  return "hunter";
            case 4:  return "rogue";
            case 5:  return "priest";
            case 6:  return "deathknight";
            case 7:  return "shaman";
            case 8:  return "mage";
            case 9:  return "warlock";
            case 11: return "druid";
        }
        return "unknown";
    }

    // Premade-spec template index for the wanted spec name — exact match
    // first, then substring fallback ("prot" catches a renamed "prot pve").
    // -1 when the class has no matching template.
    int ResolveSpecNo(uint8 classId, char const* exact, char const* fallback,
                      std::string* pickedName)
    {
        // Premade specs on this tree are TalentPath entries (id/name) under
        // classSpecs[class]; the value the factory pipeline consumes is
        // path.id + 1 (see PlayerbotFactory::SelectPremadeSpecNo).
        std::vector<TalentPath>& paths = sPlayerbotAIConfig.classSpecs[classId].talentPath;
        for (int pass = 0; pass < 2; ++pass)
            for (TalentPath& path : paths)
            {
                if (path.name.empty())
                    continue;
                bool const hit = pass == 0 ? path.name == exact
                                           : path.name.find(fallback) != std::string::npos;
                if (hit)
                {
                    if (pickedName)
                        *pickedName = path.name;
                    return path.id + 1;
                }
            }
        return -1;
    }

    // Destroy every equipped item so the factory re-gears from an empty sheet.
    // PlayerbotFactory::ClearAllItems is private to the factory, and its public
    // ClearEverything() drags in a level/talent/skill reset we do not want here,
    // so do the one thing that matters — the equipped set — directly.
    void StripEquipment(Player* bot)
    {
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
    }

    // Human-readable spec for the run record. AiFactory here only exposes the
    // talent tab index; the vanilla tab names are fixed, so map them locally.
    std::string SpecNameForRecord(Player* bot)
    {
        static char const* const kTabNames[12][3] = {
            /* 0 none    */ {"", "", ""},
            /* 1 warrior */ {"arms", "fury", "protection"},
            /* 2 paladin */ {"holy", "protection", "retribution"},
            /* 3 hunter  */ {"beast mastery", "marksmanship", "survival"},
            /* 4 rogue   */ {"assassination", "combat", "subtlety"},
            /* 5 priest  */ {"discipline", "holy", "shadow"},
            /* 6 -       */ {"", "", ""},
            /* 7 shaman  */ {"elemental", "enhancement", "restoration"},
            /* 8 mage    */ {"arcane", "fire", "frost"},
            /* 9 warlock */ {"affliction", "demonology", "destruction"},
            /*10 -       */ {"", "", ""},
            /*11 druid   */ {"balance", "feral", "restoration"},
        };
        uint8 const cls = bot->getClass();
        int const tab = AiFactory::GetPlayerSpecTab(bot);
        if (cls < 12 && tab >= 0 && tab < 3)
            return kTabNames[cls][tab];
        return "";
    }
}

char const* DcTestRunJob::StageName(Stage s)
{
    switch (s)
    {
        case Stage::SpawningBots: return "spawning_bots";
        case Stage::Provisioning: return "provisioning";
        case Stage::Grouping:     return "grouping";
        case Stage::Teleporting:  return "teleporting";
        case Stage::Starting:     return "starting";
        case Stage::Monitoring:   return "monitoring";
        case Stage::TearingDown:  return "tearing_down";
    }
    return "?";
}

void DcTestRunJob::EnterStage(Stage s)
{
    _stage.store(s);
    _stageMs = 0;
}

Player* DcTestRunJob::FindGm() const
{
    // FindConnectedPlayer, NOT FindPlayer: the GM teleporting into the
    // instance to watch sits on a loading screen for a few ticks — not in
    // world, but very much still logged in. FindPlayer() returned null there,
    // and the liveness check below read it as a logout and aborted every run
    // the moment the GM zoned in. Session-based lookup survives the loading
    // screen; a real logout still goes null as soon as the session closes.
    return ObjectAccessor::FindConnectedPlayer(_gmGuid);
}

Player* DcTestRunJob::FindTank() const
{
    return ObjectAccessor::FindPlayer(_tankGuid);
}

void DcTestRunJob::ReassertMaster()
{
    Player* const gm = FindGm();
    if (!gm)
        return;

    for (Slot const& slot : _slots)
    {
        Player* const bot = ObjectAccessor::FindPlayer(slot.guid);
        if (!bot)
            continue;
        PlayerbotAI* const botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || botAI->GetMaster() == gm)
            continue;

        // Name it once. WHICH member loses its master, and how far into the run,
        // is the missing half of the diagnosis — the rate of the message is the
        // answer to "is it lost once at setup, or repeatedly?".
        if (!_masterRepairLogged)
        {
            _masterRepairLogged = true;
            Player* const had = botAI->GetMaster();
            LOG_INFO("playerbots.dungeonclear",
                     "TESTRUN {} react-delay repair: {}'s playerbots master was {} "
                     "({}s into monitoring) — reinstating {}. Until this fires the "
                     "bot thinks on GetReactDelay()'s slow path (up to 3s per tick).",
                     RunId(), bot->GetName(), had ? had->GetName() : "cleared",
                     _monitorMs / 1000, gm->GetName());
        }
        botAI->SetMaster(gm);
        // Whatever cleared the master very likely ran ResetStrategies() with it
        // (that is the shape of every master-clearing path in stock), which puts
        // stock follow-master back. Re-strip it on the repair path exactly as
        // Grouping does, so a reinstated GM master can never start a bot jogging
        // toward the invisible driver parked outside the instance.
        // NOT here: this runs in the world tick, and ChangeStrategy rebuilds
        // the engine's trigger list under a bot that is walking it on its own
        // map thread (two SIGSEGV in NextAction::clone). Hand it to the gate,
        // which applies it in the bot's own update.
        DcStrategyGate::RequestFollowStrip(bot->GetObjectGuid());
    }
}

void DcTestRunJob::InitIdentity(Player* gm, DcTestDungeonRegistry::Row const& row, uint32 level,
                                bool heroic, uint32 seed, std::string const& planId)
{
    _dungeonToken = row.token;
    _mapId = row.mapId;
    _x = row.x;
    _y = row.y;
    _z = row.z;
    _o = row.o;
    _heroic = heroic;
    _level = level;
    _gmGuid = gm->GetObjectGuid();

    _limits.pauseGraceMs = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.PauseGraceS") * 1000;
    _limits.stallGraceMs = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.StallGraceS") * 1000;
    _limits.noProgressMs = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.NoProgressS") * 1000;
    _limits.overallTimeoutMs = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.OverallTimeoutS") * 1000;

    _record = DcTestRunRecord::Record{};
    _record.runId = MakeRunId();
    _record.planId = planId;
    _record.dungeon = row.token;
    _record.dungeonName = row.name;
    _record.wing = row.wing;
    _record.mapId = row.mapId;
    _record.level = _level;
    _record.heroic = heroic;
    _record.compSeed = seed;
    _record.startedAtMs = NowUnixMs();
    _record.pauseGraceS = _limits.pauseGraceMs / 1000;
    _record.stallGraceS = _limits.stallGraceMs / 1000;
    _record.noProgressS = _limits.noProgressMs / 1000;
    _record.overallS = _limits.overallTimeoutMs / 1000;
}

std::unique_ptr<DcTestRunJob> DcTestRunJob::Create(Player* gm, DcTestDungeonRegistry::Row const& row,
                                                   uint32 levelOverride, uint32 seed, bool heroic,
                                                   DcTestGearTiers::Spec const& gear,
                                                   std::unordered_set<ObjectGuid> const& reservedGuids,
                                                   std::string const& planId, std::string* err)
{
    // Caller (DcTestRunManager::Start) has already validated the registry row
    // (including that heroic is only requested where heroicLevel is set) and
    // that gm has a playerbot manager.
    std::unique_ptr<DcTestRunJob> job(new DcTestRunJob());

    // seed 0 = "roll one" — pick a nonzero seed so the comp varies per run yet
    // is recorded for exact replay via `.dc test start <d> seed=N`.
    if (seed == 0)
        seed = rand32() | 1u;

    uint32 const level = levelOverride ? std::min<uint32>(levelOverride, 80u)
                                       : (heroic ? row.heroicLevel : row.recommendedLevel);
    job->InitIdentity(gm, row, level, heroic, seed, planId);

    // Resolve the gear ceiling against the conf once, here: a run that took 40
    // minutes must be reproducible from its record even if somebody reloaded
    // the config while it was in the dungeon.
    job->_gear = DcTestGearTiers::Resolve(gear, sPlayerbotAIConfig.autoGearScoreLimit,
                                          sPlayerbotAIConfig.autoGearQualityLimit);
    job->_record.gearIlvl = job->_gear.ilvl;
    job->_record.gearQuality = job->_gear.quality;

    std::array<DcTestComp::Slot, DcTestComp::kPartySize> const comp = DcTestComp::BuildComp(seed);
    for (DcTestComp::Slot const& c : comp)
    {
        Slot s;
        s.classId = c.classId;
        s.specName = c.specName;
        s.fallbackSpec = c.fallbackSpec;
        s.role = c.role;
        job->_slots.push_back(s);
    }

    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(gm);
    bool const isAlliance = gm->GetTeamId() == TEAM_ALLIANCE;

    // The active rotation set, resolved once: the rotation logs its
    // currentBots in and out on its own schedule and yanked a live run's
    // tank mid-dungeon - claimed characters must come from OUTSIDE it.
    std::unordered_set<uint32> rotationGuids;
    for (uint32 lowGuid : sRandomPlayerbotMgr.GetActiveRotationBots())
        rotationGuids.insert(lowGuid);

    // Claim one offline bot-account character of `classId`, or Empty if none
    // is free. No addclass pool on this tree - the random-bot accounts are the
    // equivalent supply: every character on them is bot stock, whether or not
    // the rotation currently fields it. A claimed character gets rebuilt by
    // the factory below; the next scheduled randomize re-rolls it for rotation
    // use anyway. (Walking the player cache is fine on this control path.)
    auto claim = [&](uint8 classId) -> ObjectGuid
    {
        for (auto const& cacheEntry : sObjectMgr.GetAllPlayerCacheData())
        {
            PlayerCacheData const* data = cacheEntry.second;
            if (!data || data->uiClass != classId)
                continue;
            if (!sPlayerbotAIConfig.IsInRandomAccountList(data->uiAccount))
                continue;
            // NOT a character of the ACTIVE rotation set: the rotation logs
            // its currentBots in and out on its own schedule and yanked a
            // live run's tank mid-dungeon (result=aborted, leader tank
            // vanished). GetBots() is exactly that set; the other ~1600
            // bot-account characters are ones the rotation never touches
            // (until a future re-roll picks them - a small window relative
            // to a run's minutes, and _reservedGuids covers run-vs-run).
            if (rotationGuids.count(cacheEntry.first))
                continue;
            LoadLoginFailedGuidsOnce();
            if (g_loginFailedGuids.count(cacheEntry.first))
                continue;
            if ((Player::TeamForRace(uint8(data->uiRace)) == ALLIANCE) != isAlliance)
                continue;
            ObjectGuid const guid(HIGHGUID_PLAYER, cacheEntry.first);
            // Skip guids already claimed by this job's earlier slots and by any
            // other live run (cross-run reservation set, world-thread only).
            bool taken = false;
            for (Slot const& other : job->_slots)
                if (other.guid == guid)
                    taken = true;
            if (taken)
                continue;
            if (reservedGuids.find(guid) != reservedGuids.end())
                continue;
            // (No botLoading check — it is protected on PlayerbotHolder;
            // AddPlayerBot itself no-ops on an in-flight load, and a char
            // loading for someone else just times the spawn stage out.)
            if (ObjectAccessor::FindConnectedPlayer(guid))
                continue;
            uint32 const guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);
            if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))
                continue;
            return guid;
        }
        return ObjectGuid::Empty;
    };

    // Fill each slot with its drawn class, then start its async login. When the
    // drawn class has no free pool character, substitute another class of the
    // same role that does (and isn't already in the party) rather than fail —
    // randomisation shouldn't abort a run just because one class is un-seeded.
    // Only a role with no fillable class at all is fatal.
    std::set<uint8> usedClasses;
    for (Slot& slot : job->_slots)
    {
        ObjectGuid guid = claim(slot.classId);
        if (!guid)
        {
            for (DcTestComp::Slot const& alt : DcTestComp::RolePool(slot.role))
            {
                if (alt.classId == slot.classId || usedClasses.count(alt.classId))
                    continue;
                guid = claim(alt.classId);
                if (guid)
                {
                    LOG_INFO("playerbots.dungeonclear",
                             "TESTRUN {} substituting {} for unavailable {} ({})",
                             job->_record.runId, ClassToken(alt.classId),
                             ClassToken(slot.classId), slot.role);
                    slot.classId = alt.classId;
                    slot.specName = alt.specName;
                    slot.fallbackSpec = alt.fallbackSpec;
                    break;
                }
            }
        }
        if (!guid)
        {
            if (err)
                *err = std::string("no available ") + slot.role +
                       " class in the addclass pool — pre-seed with `.playerbots addclass`";
            return nullptr;
        }
        slot.guid = guid;
        usedClasses.insert(slot.classId);
        // A character parked inside a dungeon map by an earlier broken
        // run logs BACK IN there - and a login into an instance whose
        // WMOs are not streamed yet grounds him on the OVERWORLD height
        // (live: tanks spawning at Z=205 over the Deadmines, the bad
        // position inherited run-to-run through the character save).
        // Park the character at its homebind BEFORE the login; the same
        // CharacterDatabase queue serializes this ahead of the login
        // query holder, and the provisioning teleport places it properly
        // afterwards.
        CharacterDatabase.PExecute(
            "UPDATE characters c JOIN character_homebind h ON h.guid = c.guid "
            "SET c.map = h.map, c.position_x = h.position_x, "
            "c.position_y = h.position_y, c.position_z = h.position_z "
            "WHERE c.guid = %u AND c.online = 0",
            slot.guid.GetCounter());
        mgr->AddPlayerBot(slot.guid, gm->GetSession()->GetAccountId());
    }

    // Spell out WHO is running, not just the seed that picked them. Deriving the
    // comp from the seed means reimplementing BuildComp, and a reimplementation
    // is an assumption: an analysis on 2026-08-29 concluded druid tanks never
    // clear more than two bosses while the run -> class mapping behind it had
    // never been checked against reality. Logged here, every later breakdown by
    // class is a measurement instead.
    std::string compStr;
    for (DcTestComp::Slot const& c : comp)
    {
        if (!compStr.empty())
            compStr += ",";
        compStr += std::string(c.role) + ":" + std::string(c.specName)
                 + "(" + std::to_string(uint32(c.classId)) + ")";
    }

    LOG_INFO("playerbots.dungeonclear",
             "TESTRUN START {} dungeon={} map={} level={} heroic={} seed={} gm={} comp={}",
             job->_record.runId, job->_record.dungeon, job->_mapId, job->_level,
             heroic ? 1 : 0, seed, gm->GetName(), compStr);

    job->EnterStage(Stage::SpawningBots);
    return job;
}

std::unique_ptr<DcTestRunJob> DcTestRunJob::CreateFromRoster(Player* gm,
                                                             DcTestDungeonRegistry::Row const& row,
                                                             bool heroic,
                                                             std::vector<RosterEntry> const& roster,
                                                             std::string const& planId,
                                                             std::string* err)
{
    if (roster.size() != DcTestComp::kPartySize)
    {
        if (err)
            *err = "a roster must be exactly " + std::to_string(DcTestComp::kPartySize) + " characters";
        return nullptr;
    }

    std::unique_ptr<DcTestRunJob> job(new DcTestRunJob());
    job->_realChars = true;

    // Level is whatever the characters are. The highest of the five stands in
    // for the run until provisioning reads the live values, so the status line
    // and the dungeon's own level expectations have something sane to show; the
    // record's per-member levels are the real answer.
    uint32 level = 0;
    for (RosterEntry const& e : roster)
        level = std::max<uint32>(level, sCharacterCache->GetCharacterLevelByGuid(e.guid));

    // seed 0: a roster IS the comp, so there is nothing to replay from a seed.
    job->InitIdentity(gm, row, level, heroic, /*seed*/ 0, planId);
    job->_record.roster = true;

    for (RosterEntry const& e : roster)
    {
        Slot s;
        s.guid = e.guid;
        s.role = e.role;
        s.rosterName = e.name;
        // Snapshot the guild BEFORE the login: stock playerbots guilds a guildless
        // bot on login, and this is the only moment the original state is knowable.
        s.guildBefore = sCharacterCache->GetCharacterGuildIdByGuid(e.guid);
        // classId off the cache so the record and the live map overlay have it
        // before the character is in world; spec templates are never applied to
        // a real character, so specName/fallbackSpec stay empty.
        if (CharacterCacheEntry const* cache = sCharacterCache->GetCharacterCacheByGuid(e.guid))
            s.classId = cache->Class;
        job->_slots.push_back(std::move(s));
    }

    // MASTERLESS login. AddPlayerBot's ownership gate (PlayerbotMgr.cpp) clears
    // only for same-account / same-guild / addclass-pool / linked characters —
    // a hand-picked party is none of those, and passing the GM's account id would
    // see every slot refused with "not allowed to control bot". masterAccountId 0
    // takes the isRndbot branch, which skips the gate; it is the same call the
    // headless test driver logs itself in with. The party therefore lands in
    // sRandomPlayerbotMgr rather than the GM's PlayerbotMgr, which LogoutBots
    // already handles, and Grouping still installs the GM as playerbots master
    // so HasRealPlayerMaster (and the react-delay fast path) is unaffected.
    //
    // Landing in sRandomPlayerbotMgr does NOT enrol the character in the
    // random-bot rotation — the thing that would periodically re-Randomize (i.e.
    // regear) or relocate it. That rotation walks `currentBots`, populated purely
    // from the playerbots DB's own enrolment rows (RandomPlayerbotMgr::GetBots),
    // and IsRandomBot additionally demands the account be in
    // AiPlayerbot.RandomBotAccounts. A real player's character satisfies neither,
    // so the holder only owns its login/logout here.
    for (Slot const& slot : job->_slots)
    {
        // A character parked inside a dungeon map by an earlier broken
        // run logs BACK IN there - and a login into an instance whose
        // WMOs are not streamed yet grounds him on the OVERWORLD height
        // (live: tanks spawning at Z=205 over the Deadmines, the bad
        // position inherited run-to-run through the character save).
        // Park the character at its homebind BEFORE the login; the same
        // CharacterDatabase queue serializes this ahead of the login
        // query holder, and the provisioning teleport places it properly
        // afterwards.
        CharacterDatabase.PExecute(
            "UPDATE characters c JOIN character_homebind h ON h.guid = c.guid "
            "SET c.map = h.map, c.position_x = h.position_x, "
            "c.position_y = h.position_y, c.position_z = h.position_z "
            "WHERE c.guid = %u AND c.online = 0",
            slot.guid.GetCounter());
        sRandomPlayerbotMgr.AddPlayerBot(slot.guid, 0);
    }

    std::string names;
    for (Slot const& slot : job->_slots)
    {
        if (!names.empty())
            names += ",";
        names += slot.rosterName + "(" + slot.role + ")";
    }
    LOG_INFO("playerbots.dungeonclear",
             "TESTRUN START {} dungeon={} map={} heroic={} roster={} gm={}",
             job->_record.runId, job->_record.dungeon, job->_mapId, heroic ? 1 : 0,
             names, gm->GetName());

    job->EnterStage(Stage::SpawningBots);
    return job;
}

void DcTestRunJob::RequestAbort(std::string const& reason)
{
    std::lock_guard<std::mutex> lock(_obsMutex);
    _abortRequested = true;
    _abortReason = reason;
}

void DcTestRunJob::AbortSetup(std::string const& reason)
{
    FailSetup(reason);
}

std::string DcTestRunJob::StatusLine() const
{
    Stage const stage = _stage.load();
    std::string out = _record.runId + " " + _record.dungeon + (_heroic ? " (heroic)" : "") +
                      " [" + StageName(stage) + "] elapsed " + std::to_string(_totalMs / 1000) + "s";
    // The gear ceiling belongs in the start confirmation: it is the one run
    // parameter with no visible effect until the party is already fighting.
    if (!_realChars)
        out += ", gear " +
               (_gear.ilvl ? "ilvl<=" + std::to_string(_gear.ilvl) : std::string("unlimited")) +
               " " + DcTestGearTiers::QualityName(_gear.quality);
    if (stage == Stage::Monitoring)
    {
        out += ", bosses " + std::to_string(_record.bossesKilled) + "/" +
               std::to_string(_record.bossesTotal);
        std::lock_guard<std::mutex> lock(_obsMutex);
        if (!_lastStatusState.empty())
            out += ", state " + _lastStatusState;
    }
    return out;
}

DcTestRunLive::RunSnapshot DcTestRunJob::Snapshot() const
{
    DcTestRunLive::RunSnapshot s;
    s.runId = _record.runId;
    s.planId = _record.planId;
    s.dungeon = _record.dungeon;
    s.dungeonName = _record.dungeonName;
    s.stage = StageName(_stage.load());
    s.level = _level;
    s.heroic = _heroic;
    s.elapsedS = _totalMs / 1000;
    s.bossesKilled = _record.bossesKilled;
    s.bossesTotal = _record.bossesTotal;

    // Live positions for the dashboard map overlay. Snapshot() is called from
    // the manager's world-thread tick (WriteLiveStatus), so resolving guids to
    // players and reading their transform is safe here. The run's mapId is the
    // tank's (all members share one instance); a member still loading resolves
    // to null and is simply omitted this heartbeat.
    for (Slot const& slot : _slots)
    {
        if (!slot.guid)
            continue;
        Player* p = ObjectAccessor::FindPlayer(slot.guid);
        if (!p)
            continue;
        if (s.mapId < 0)
            s.mapId = static_cast<std::int32_t>(p->GetMapId());
        DcTestRunLive::BotPos bp;
        bp.role = slot.role;
        // The slot only knows class/spec/role; the name belongs to the char the
        // pool handed out, so it has to come off the resolved player.
        bp.name = p->GetName();
        bp.classId = slot.classId;
        bp.x = p->GetPositionX();
        bp.y = p->GetPositionY();
        bp.z = p->GetPositionZ();
        bp.alive = p->IsAlive();
        bp.hp = static_cast<std::uint8_t>(bp.alive ? p->GetHealthPct() : 0.f);
        // Gate on the mana POOL, not on getPowerType(): a druid shifted into
        // bear/cat reports RAGE/ENERGY while still holding the mana it has to
        // shift back and drink for. Keying off the active power type would blank
        // the bar for exactly the member whose mana the run is waiting on.
        // Non-mana classes have maxMana 0 and stay at the -1 "no bar" default.
        if (bp.alive && p->GetMaxPower(POWER_MANA) > 0)
            bp.mp = static_cast<std::int16_t>(p->GetPowerPct(POWER_MANA));
        bp.inCombat = p->IsInCombat();
        if (bp.inCombat)
            s.inCombat = true;
        s.bots.push_back(std::move(bp));
    }

    // Why the run is sitting still, read live off the tank rather than from the
    // status timeline: the timeline only records state CHANGES, so a run that
    // has been wedged in one state for ten minutes has nothing recent in it.
    if (Player* tank = FindTank())
        if (PlayerbotAI* tankAI = GET_PLAYERBOT_AI(tank))
        {
            AiObjectContext* ctx = tankAI->GetAiObjectContext();
            s.stall = ctx->GetValue<std::string&>(DcKey::StallReason)->Get();
            std::optional<DungeonBossInfo> const next =
                ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
            if (next.has_value())
                s.bossName = next->name;
        }

    // Not under _obsMutex: the watchdog writes _sinceProgressMs outside it too,
    // and both that write and this read happen on the world thread.
    s.sinceProgressS = _sinceProgressMs / 1000;

    // A wipe holds the run for the grace window (a battle rez can still save
    // it), during which the live card would otherwise show a party doing
    // nothing at all. Name the killer as soon as the harness sees the wipe
    // rather than only in the finished record.
    if (_wipedForMs > 0)
    {
        DcTestRun::Engagement const blame = DeathBlame();
        s.wiped = true;
        s.wipeOnBoss = blame.isBoss;
        s.wipeOpponent = blame.name;
    }

    std::lock_guard<std::mutex> lock(_obsMutex);
    s.state = _lastStatusState;
    std::vector<DcTestRunRecord::StatusEntry> const& st = _record.statusTimeline;
    std::size_t const from = st.size() > 8 ? st.size() - 8 : 0;
    for (std::size_t i = from; i < st.size(); ++i)
        s.recent.push_back({st[i].t, st[i].state, st[i].detail});
    return s;
}

std::vector<ObjectGuid> DcTestRunJob::BotGuids() const
{
    std::vector<ObjectGuid> out;
    out.reserve(_slots.size());
    for (Slot const& slot : _slots)
        if (slot.guid)
            out.push_back(slot.guid);
    return out;
}

void DcTestRunJob::Tick(uint32 diff, bool& provisionBudget)
{
    Stage const stage = _stage.load();
    if (stage == Stage::TearingDown || _done)
        return;

    _stageMs += diff;
    _totalMs += diff;

    // The GM's session anchors the bots (their logout logs the party out);
    // without it the run cannot finish cleanly whatever stage it is in. One
    // GM's logout kills only their runs — the check is per-job.
    if (!FindGm())
    {
        if (stage == Stage::Monitoring)
            Finish(DcTestRun::Verdict::FailAborted, "GM logged out mid-run");
        else
            FailSetup("GM logged out during setup");
        return;
    }

    switch (stage)
    {
        case Stage::SpawningBots:
            TickSpawning();
            break;
        case Stage::Provisioning:
            TickProvisioning(provisionBudget);
            break;
        case Stage::Grouping:
            TickGrouping();
            break;
        case Stage::Teleporting:
            TickTeleporting();
            break;
        case Stage::Starting:
            TickStarting();
            break;
        case Stage::Monitoring:
            _monitorMs += diff;
            // Every tick, NOT on the monitor's 1s step: a bot crosses a small
            // trigger box in well under a second, and a relay that samples at
            // 1Hz would silently skip the set-piece it exists to fire.
            _areaTriggers.Tick(FindTank());
            _monitorAccumMs += diff;
            if (_monitorAccumMs >= MONITOR_STEP_MS)
            {
                uint32 const dt = _monitorAccumMs;
                _monitorAccumMs = 0;
                TickMonitoring(dt);
            }
            break;
        default:
            break;
    }
}

void DcTestRunJob::TickSpawning()
{
    bool allIn = true;
    for (Slot const& slot : _slots)
    {
        Player* bot = ObjectAccessor::FindPlayer(slot.guid);
        if (!bot || !bot->IsInWorld() || !GET_PLAYERBOT_AI(bot))
        {
            allIn = false;
            break;
        }
    }

    if (allIn)
    {
        EnterStage(Stage::Provisioning);
        return;
    }

    if (_stageMs >= SPAWN_TIMEOUT_MS)
    {
        if (_realChars)
        {
            // Name the character that never arrived: for a hand-picked party the
            // usual cause is somebody logging in on it between the pre-flight
            // check and the login, which the pool path cannot experience.
            std::string missing;
            for (Slot const& slot : _slots)
            {
                Player* bot = ObjectAccessor::FindPlayer(slot.guid);
                if (bot && bot->IsInWorld() && GET_PLAYERBOT_AI(bot))
                    continue;
                if (!missing.empty())
                    missing += ", ";
                missing += slot.rosterName;
            }
            FailSetup("roster characters did not finish logging in (" + missing +
                      ") — logged in as a real player, or a login failure (see server log)");
            return;
        }
        for (Slot const& slot : _slots)
        {
            Player* bot = ObjectAccessor::FindPlayer(slot.guid);
            if (bot && bot->IsInWorld() && GET_PLAYERBOT_AI(bot))
                continue;
            if (slot.guid && g_loginFailedGuids.insert(slot.guid.GetCounter()).second)
            {
                RememberLoginFailedGuid(slot.guid.GetCounter(), _record.runId, slot.role);
                LOG_INFO("playerbots.dungeonclear",
                         "TESTRUN {} guid {} ({} slot) never entered the world — excluded from "
                         "claims (persisted in dc_login_failed.txt); check the character row "
                         "(map, position, online flag)",
                         _record.runId, slot.guid.GetCounter(), slot.role);
            }
        }
        FailSetup("bots did not finish logging in (addclass pool empty, "
                  "maxAddedBots cap, or login failure — see server log)");
    }
}

void DcTestRunJob::TickProvisioning(bool& provisionBudget)
{
    if (_stageMs >= PROVISION_TIMEOUT_MS)
    {
        FailSetup("provisioning timed out");
        return;
    }

    // Real characters are never rolled — read them out and move on.
    if (_realChars)
    {
        TickProvisioningRoster();
        return;
    }

    if (_provisionIdx >= _slots.size())
    {
        EnterStage(Stage::Grouping);
        return;
    }

    Slot& slot = _slots[_provisionIdx];
    Player* bot = ObjectAccessor::FindPlayer(slot.guid);
    PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!bot || !bot->IsInWorld() || !botAI)
        return;  // transient (mid world-add/teleport) — retry; stage timeout bounds it

    std::string pickedSpec;
    int const specNo = ResolveSpecNo(slot.classId, slot.specName, slot.fallbackSpec, &pickedSpec);
    if (specNo < 0 && (slot.role == std::string("tank") || slot.role == std::string("heal")))
    {
        // A random-rolled tank/healer spec would invalidate the whole run.
        FailSetup(std::string("no premade spec template matching '") + slot.specName +
                  "' for " + ClassToken(slot.classId) +
                  " (AiPlayerbot.PremadeSpecName.*) — cannot force the " + slot.role);
        return;
    }

    // One factory roll per world tick across ALL runs — Randomize is heavyweight
    // (full gear/spell/talent roll) and several in one tick would be a visible
    // stall. If another run already spent this tick's budget, retry next tick;
    // the stage timeout bounds the wait.
    if (!provisionBudget)
        return;
    provisionBudget = false;

    // Gear ceiling: the run's own (_gear, resolved at Create from the `ilvl=` /
    // `quality=` options or the AutoGear* conf values), applied exactly the way
    // the `autogear` chat command applies the server-wide ones.
    //
    // This tree's factory has no mixed-gearscore ceiling - itemQuality (the
    // ctor argument below) is the only cap it honors. The requested ilvl stays
    // in the run record and the gearCap label, but is NOT enforced here.
    std::string const gearCap = _gear.ilvl == 0 ? std::string("unlimited") : std::to_string(_gear.ilvl);

    // Full roll at the target level first (Randomize includes GiveLevel and
    // re-picks talents), then force the role spec and re-gear for it — the
    // same sequence the `talents spec` chat command uses.
    // Pin the slot's role BEFORE the factory rebuilds strategies:
    // IsTank/FindLeaderTank read the INSTALLED strategy set, not the talent
    // spec, and e.g. a feral druid respecs fine yet never gains the "tank"
    // strategy on its own - dc on then resolves no leader tank and the run
    // refuses to start (live: two straight setup failures, both with the
    // feral tank; warrior/paladin tanks worked because their default
    // strategy set already carries "tank"). ResetStrategies applies
    // m_forcedRole last, dungeon-finder style, for every rebuild from here.
    if (PlayerbotAI* slotAi = GetBotAI(bot))
        slotAi->SetForcedRole(slot.role == "tank" ? 1 : (slot.role == "heal" ? 2 : 3));

    // Shield this login from the random holder's add-event logout policy for
    // the run's lifetime (see RandomPlayerbotMgr::SetExternallyManaged).
    sRandomPlayerbotMgr.SetExternallyManaged(slot.guid.GetCounter(), true);

    // FORCE the run's target level, both directions, BEFORE the factory.
    // AiPlayerbot.DisableRandomLevels=1 (this server pins rotation levels)
    // makes Randomize() skip its GiveLevel entirely, so provisioning kept
    // whatever level the claimed bot happened to be - live: level 38-47
    // bots breezing through a level-21 race leg (the who-list screenshot).
    // Talents above the target are cleared on the way down; the premade
    // spec path right below rebuilds them for the new level.
    if (bot->GetLevel() != _level)
    {
        if (bot->GetLevel() > _level)
            bot->resetTalents(true);
        bot->GiveLevel(_level);
        bot->InitTalentForLevel();
        bot->SetUInt32Value(PLAYER_XP, 0);
        LOG_INFO("playerbots.dungeonclear",
                 "TESTRUN {} level-set: {} -> {}",
                 _record.runId, bot->GetName(), _level);
    }

    PlayerbotFactory factory(bot, _level, _gear.quality);

    // Strip the equipped set first. Randomize() only wipes items when
    // AiPlayerbot.EquipAndSpecPersistence is off (it defaults on), and
    // InitEquipment leaves a slot alone when no candidate passes the filters — so
    // a pool bot geared by an earlier run under a looser ceiling would keep those
    // pieces and the new limit would look ignored. Every test bot starts bare and
    // is geared from scratch, which is the `autogear`-on-a-stripped-bot behaviour
    // a run needs to be reproducible.
    StripEquipment(bot);
    factory.Randomize(false, /*syncWithMaster*/ false);
    if (specNo >= 0)
    {
        // The factory pipeline's own premade path: pin the spec value, then
        // the "auto talents" action applies the matching build (resetting
        // first when the current tree does not match).
        sRandomPlayerbotMgr.SetValue(bot, "specNo", uint32(specNo));
        GetBotAI(bot)->DoSpecificAction("auto talents");
        factory.EquipGear();
        // (no glyphs on 1.12)

        // Gear first, enchants/gems second — the order the `autogear` then
        // `maintenance` chat commands run in. Randomize() already ends with an
        // ApplyEnchantAndGemsNew() pass, but the spec re-gear above swaps those
        // enchanted/gemmed items out for freshly rolled bare ones, so without
        // this second pass every spec-forced bot (i.e. every tank and healer in
        // a test run) fights with no enchants and empty sockets. Cheap relative
        // to Randomize, and it only touches what is currently equipped.
        if (bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
            factory.EnchantEquipment();
    }
    if (bot->getClass() == CLASS_HUNTER)
        factory.InitPet();

    // AMMO LAST, FOR EVERY CLASS — for the same reason the enchant pass above runs
    // last, and it was the same oversight one line further down.
    //
    // Randomize() ends with its own InitAmmo(), which loads ammo for the ranged
    // weapon IT rolled. The spec re-gear then replaces that weapon, and a gun and a
    // bow do not take the same projectile — so a bot whose random roll gave it a bow
    // and whose prot re-gear gave it a gun ends up holding a rifle loaded with
    // arrows. Re-running InitAmmo() only for hunters left every other class stranded
    // on whatever the pre-re-gear weapon needed.
    //
    // Not a cosmetic mismatch. PLAYER_AMMO_ID is set, so every "do I have ammo" test
    // passes, and the failure only surfaces where it counts: the server rejects the
    // Shoot cast itself, silently, and the bot has no ranged opener at all. A warrior
    // has no class opener either (Heroic Throw is level 71), so the tank has nothing
    // — which for a scripted pull means standing on the stand spot for the whole leg
    // budget and then walking into the room. Live: Erinerice and Moge, both prot
    // warriors, both holding Rifle of the Stoic Guardian with Timeless Arrows loaded,
    // failed the Selin stage that way in tr-20260803-154419-13 and -17 while every
    // druid and paladin tank in the same plan pulled normally on a class opener.
    //
    // InitAmmo() self-gates to hunter/rogue/warrior and re-derives the projectile
    // class from the CURRENTLY equipped weapon, so calling it unconditionally is both
    // safe and the whole fix.
    factory.InitAmmo();

    // PROVISIONS, for the same reason ammo is re-run above: StripEquipment
    // empties the bags and the Randomize(false, ...) path never reaches
    // InitFood, so every caster entered its run with nothing to drink. A
    // level-21 healer then regenerates mana at natural rate - minutes - and
    // the party's rest gate holds the WHOLE run there (live: 248s of
    // "Waiting on X (low mana)" in front of Mr. Smite, run aborted at 4/8).
    // InitFood is a no-op when the bags already carry food/drink, and skips
    // drinks for classes without mana.
    factory.AddFood();   // public wrapper for InitFood

    // Not inline: provisioning ticks in the world thread and this bot is
    // already logged in and thinking on its map thread. ResetStrategies
    // rebuilds the trigger list, which is what tore NextAction::clone apart.
    DcStrategyGate::RequestStrategyReset(bot->GetObjectGuid());

    DcTestRunRecord::CompEntry entry;
    entry.name = bot->GetName();
    entry.className = ClassToken(slot.classId);
    entry.spec = specNo >= 0 ? pickedSpec : "(random)";
    entry.role = slot.role;
    entry.guid = slot.guid.GetRawValue();
    entry.level = bot->GetLevel();
    _record.comp.push_back(entry);

    LOG_INFO("playerbots.dungeonclear",
             "TESTRUN {} provisioned {} ({} {}, level {}, gear <= ilvl {} quality {})",
             _record.runId, bot->GetName(), entry.spec, entry.role, entry.level, gearCap,
             DcTestGearTiers::QualityName(_gear.quality));

    slot.provisioned = true;
    ++_provisionIdx;
}

// The whole of "provisioning" for a hand-picked party: describe the characters,
// change nothing about them.
//
// Deliberately absent, and none of it may come back: PlayerbotFactory::Randomize
// (re-rolls gear AND talents AND level), InitTalentsBySpecNo / InitEquipment /
// InitGlyphs / ApplyEnchantAndGemsNew (the `autogear`/`maintenance` pass), and
// InitPet/InitAmmo (a hunter's real pet is not ours to replace). A character
// marked for a run accepts dying, looting, and durability loss — it does not
// accept coming back a different character.
//
// ResetStrategies stays: it reloads strategies from config (which is how the
// dungeon-clear stack gets installed) without touching the character sheet.
//
// All five slots are read in one tick — there is no factory roll to spend the
// shared per-tick provision budget on.
// Stock playerbots joins any guildless bot to a random bot guild the moment it
// logs in: RandomPlayerbotMgr::OnBotLoginInternal calls
// PlayerbotFactory::InitGuild whenever AiPlayerbot.RandomBotGuildCount > 0, with
// no check that the character actually IS a random bot. A roster member logs in
// through that same holder (the masterless path is the only one whose ownership
// gate a hand-picked character clears), so it gets caught too — and a real
// character must come out of a test run in the guild it went in with.
//
// So put it back. The decisive signal is that the character was guildless when
// the run claimed it (captured in CreateFromRoster, moments before the login) and
// is guilded now: that change provably happened inside our window, and the only
// actor in that window is playerbots' own InitGuild.
//
// Deliberately NOT gated on PlayerbotGuildMgr::IsRealGuild. That flag is computed
// from the guild LEADER's account, and InitGuild's create path makes the drafted
// character itself the leader — so a guild freshly conjured around a real
// character is classified "real" and the gate would refuse to undo exactly the
// case that needs undoing. The classification is logged instead of obeyed.
//
// A character that already had a guild is never touched (InitGuild returns early
// on those anyway). Guild::DeleteMember handles the leader case properly: it
// promotes the next-ranked member, or disbands when this was the only one, which
// is right for a guild that exists solely because of this bug.
void DcTestRunJob::UndoUnwantedGuild(Player* bot, Slot const& slot) const
{
    if (slot.guildBefore)
        return;  // came in with a guild — not ours to touch
    uint32 const now = bot->GetGuildId();
    if (!now)
        return;  // still guildless — nothing happened

    Guild* guild = sGuildMgr.GetGuildById(now);
    if (!guild)
        return;

    bool const classedReal = PlayerbotGuildMgr::instance().IsRealGuild(now);
    std::string const guildName = guild->GetName();
    bool const wasLeader = guild->GetLeaderGuid() == bot->GetObjectGuid();

    // vmangos DelMember(guid, isDisbanding=false): no kicked/canDeleteGuild
    // knobs - it already disbands a guild whose last member leaves.
    guild->DelMember(bot->GetObjectGuid());
    LOG_INFO("playerbots.dungeonclear",
             "TESTRUN {} removed {} from guild '{}' ({}) it was auto-joined to at login — "
             "character was guildless (leader={}, playerbots classed it {}); "
             "stock RandomBotGuildCount behaviour",
             _record.runId, bot->GetName(), guildName, now, wasLeader ? "yes" : "no",
             classedReal ? "real" : "bot");
}

void DcTestRunJob::TickProvisioningRoster()
{
    for (Slot& slot : _slots)
    {
        if (slot.provisioned)
            continue;

        Player* bot = ObjectAccessor::FindPlayer(slot.guid);
        PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        if (!bot || !bot->IsInWorld() || !botAI)
            return;  // transient — retry next tick; the stage timeout bounds it

        UndoUnwantedGuild(bot, slot);
        // Same reason as in TickProvisioning: off the world thread.
        DcStrategyGate::RequestStrategyReset(bot->GetObjectGuid());

        // What the character's talents actually say, next to what the human
        // marked it as. A wrong marking is human error at roster time and the run
        // proceeds anyway (that is the stated policy), but it is the first thing
        // anyone will want to see in the post-mortem of a run where the "tank"
        // died in six seconds.
        char const* detected = PlayerbotAI::IsTank(bot, /*bySpec*/ true)   ? "tank"
                               : PlayerbotAI::IsHeal(bot, /*bySpec*/ true) ? "heal"
                                                                          : "dps";

        DcTestRunRecord::CompEntry entry;
        entry.name = bot->GetName();
        entry.className = ClassToken(bot->getClass());
        entry.spec = SpecNameForRecord(bot);
        entry.role = slot.role;
        entry.detectedRole = detected;
        entry.roleMismatch = slot.role != std::string(detected);
        entry.guid = slot.guid.GetRawValue();
        entry.level = bot->GetLevel();
        entry.fromMap = bot->GetMapId();
        entry.fromX = bot->GetPositionX();
        entry.fromY = bot->GetPositionY();
        entry.fromZ = bot->GetPositionZ();
        entry.fromO = bot->GetOrientation();
        _record.comp.push_back(entry);

        // classId was taken from the character cache at Create; trust the live
        // character over the cache now that it is resolvable.
        slot.classId = bot->getClass();
        slot.provisioned = true;

        LOG_INFO("playerbots.dungeonclear",
                 "TESTRUN {} roster member {} ({} {}, level {}){}",
                 _record.runId, entry.name, entry.spec, entry.role, entry.level,
                 entry.roleMismatch ? std::string(" — WARNING: spec reads as ") + detected : "");
    }

    // Highest level present is the run's headline level (Create only had the
    // cache's view; this is the live one).
    for (DcTestRunRecord::CompEntry const& e : _record.comp)
        _level = std::max(_level, e.level);
    _record.level = _level;

    EnterStage(Stage::Grouping);
}

void DcTestRunJob::TickGrouping()
{
    if (!_groupFormed)
    {
        Player* tank = ObjectAccessor::FindPlayer(_slots[0].guid);
        if (!tank)
        {
            if (_stageMs >= GROUP_TIMEOUT_MS)
                FailSetup("tank vanished before grouping");
            return;
        }

        for (Slot const& slot : _slots)
            if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
                if (bot->GetGroup())
                    bot->RemoveFromGroup();

        // Direct group formation (the LFGMgr pattern) — no invite/accept
        // packet round-trips, and Create() makes the tank the leader.
        Group* group = new Group();
        if (!group->Create(tank->GetObjectGuid(), tank->GetName()))
        {
            delete group;
            FailSetup("group creation failed");
            return;
        }
        // Free-for-all loot for the whole run. Group loot opens roll windows
        // on every green+ drop, and un-answered rolls keep the corpse
        // lootable for every non-voter - each such corpse costs the party a
        // camp timeout plus a loot-yield timeout (~30s standing still), and
        // an at-level clear drops greens constantly (live: race leg 1 froze
        // for minutes in a corpse-to-corpse give-up chain).
        group->SetLootMethod(FREE_FOR_ALL);
        group->SendUpdate();
        sObjectMgr.AddGroup(group);
        for (std::size_t i = 1; i < _slots.size(); ++i)
        {
            Player* bot = ObjectAccessor::FindPlayer(_slots[i].guid);
            if (!bot || !group->AddMember(bot->GetObjectGuid(), bot->GetName()))
            {
                FailSetup(std::string("could not add ") + ClassToken(_slots[i].classId) +
                          " to the group");
                return;
            }
        }
        // (no dungeon difficulties on 1.12 - the _heroic flag only stays in
        // the run record)
        _tankGuid = tank->GetObjectGuid();
        _groupFormed = true;
    }

    Player* tank = FindTank();
    Group* group = tank ? tank->GetGroup() : nullptr;
    if (group && group->GetMembersCount() == _slots.size() && group->IsLeader(_tankGuid))
    {
        // Keep the GM (a real human Player) as each bot's playerbots MASTER, and
        // strip the stock follow-master strategy instead of nulling the master.
        //
        // Why not masterless: the real-player-master gate governs the whole "a
        // human is driving me" fast path in stock playerbots (pre-PR-2592 the
        // removed HasRealPlayerMaster(); now the master satisfying
        // IsRealPlayer(master) || IsSelfBot(master)). With no real-player master,
        // GetReactDelay() returns base*10 (1000ms vs 100ms) out of combat, so the
        // party thinks on a 1s beat between packs — test runs looked far slower
        // and sloppier than a real human-led run. That gate has no
        // map/distance/visibility check (it only checks the master pointer is a
        // non-bot Player or a self-bot), so keeping the GM as master holds the
        // fast path even
        // though the GM is invisible and outside the instance — the test now
        // mirrors a real run in every master-gated respect (react delay, AoE
        // avoidance, wait-for-attack), which is the point of a regression harness.
        //
        // The only reason the old code went masterless was to stop stock
        // follow-master from dragging the party toward the GM. Follow-master is
        // just the "follow" strategy, so we remove it outright here. DC's own
        // follow-tank redirect (a DcMovementAction, not a FollowAction) drives
        // the followers, and DungeonClearMultiplier additionally zeroes
        // FollowAction for every active-run member once the run is enabled —
        // this strip covers the pre-enable / paused window. A GM master sticks:
        // FindNewMaster() only re-resolves a null or bot master.
        Player* gm = FindGm();
        for (Slot const& slot : _slots)
            if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                {
                    if (gm)
                        botAI->SetMaster(gm);
                    // Same reason as the repair path above: requested here,
                    // carried out on the bot's own thread.
                    DcStrategyGate::RequestFollowStrip(bot->GetObjectGuid());
                }

        EnterStage(Stage::Teleporting);
        return;
    }

    if (_stageMs >= GROUP_TIMEOUT_MS)
        FailSetup("group did not form");
}

void DcTestRunJob::UnbindFromMap() const
{
    // Guid-keyed and offline-safe, which is why the teardown copy can run after
    // the party has logged out.
    for (Slot const& slot : _slots)
    {
        if (!slot.guid)
            continue;
        sInstanceSaveMgr->PlayerUnbindInstance(slot.guid, _mapId, DUNGEON_DIFFICULTY_NORMAL,
                                               /*deleteFromDB*/ true);
        sInstanceSaveMgr->PlayerUnbindInstance(slot.guid, _mapId, DUNGEON_DIFFICULTY_HEROIC,
                                               /*deleteFromDB*/ true);
    }
}

bool DcTestRunJob::CheckInstanceBudget()
{
    // Player::CheckInstanceCount is exactly the gate MapMgr::PlayerCannotEnter
    // applies, and reads the same in-memory per-account table the core loads at
    // login (account_instance_times) and prunes as entries expire — so asking the
    // logged-in character is both cheaper and more faithful than re-deriving the
    // count in SQL. Instance id 0 = "a brand-new instance", which is what an
    // unbound party is about to create.
    // Fixed core constant on this engine - Player::CheckInstanceCount gates
    // on the very same value.
    uint32 const perHour = MAX_INSTANCE_PER_ACCOUNT_PER_HOUR;
    if (perHour == 0)
        return true;

    for (Slot const& slot : _slots)
    {
        Player* bot = ObjectAccessor::FindPlayer(slot.guid);
        if (!bot)
            continue;  // not resolvable yet; the stage timeout covers it

        // Mirror MapMgr::PlayerCannotEnter exactly, including its escape hatch:
        // a member still bound to an instance of this map is re-entering one it
        // has already paid for, so the count check passes on the found id. Every
        // run now unbinds before this point, so in practice the lookup misses and
        // the id is 0 ("a brand-new instance") — the hatch is kept rather than
        // hard-coding 0 so this stays a faithful mirror of the core's gate if a
        // bind ever does survive.
        uint32 idToCheck = 0;
        if (InstanceSave* save = sInstanceSaveMgr->PlayerGetInstanceSave(
                bot->GetObjectGuid(), _mapId, bot->GetDifficulty(/*isRaid*/ false)))
            idToCheck = save->GetInstanceId();

        if (bot->CheckInstanceCount(idToCheck))
            continue;

        FailSetup(Acore::StringFormat(
            "{} has entered {} instances in the last hour (AccountInstancesPerHour) — "
            "the core would refuse the teleport. Wait for a slot to free, use different "
            "characters, or raise AccountInstancesPerHour.",
            bot->GetName(), perHour));
        return false;
    }
    return true;
}

void DcTestRunJob::TickTeleporting()
{
    if (!_teleportIssued)
    {
        for (Slot const& slot : _slots)
        {
            Player* bot = ObjectAccessor::FindPlayer(slot.guid);
            if (!bot)
            {
                if (_stageMs >= TELEPORT_TIMEOUT_MS)
                    FailSetup("bot vanished before teleport");
                return;  // transient — retry next tick
            }
        }

        // Nobody may enter this dungeon FROM a dungeon. Two separate core
        // behaviours make an instance-to-instance entry unsafe, and one trip out
        // to the bind point defuses both:
        //
        //   * A same-map TeleportTo is a NEAR teleport (Player::TeleportTo keys
        //     the branch on map id alone). It moves the body but never
        //     re-resolves which COPY the body is in, so a bot left inside a
        //     stale instance of this very dungeon — which is exactly what a
        //     worldserver restart mid-plan leaves behind — would "arrive"
        //     without ever leaving that copy, dragging its cleared bosses into
        //     our verdict.
        //
        //   * `m_InstanceValid` is cleared by Group::_homebindIfInstance for any
        //     member pulled out of a group while standing in a dungeon — which
        //     TickGrouping's pre-formation sweep does to every recycled pool bot
        //     that logged in inside one. The ONLY place that ever sets it back is
        //     HandleMoveWorldportAck, and only when the DESTINATION is not
        //     instanced ("except if going to an instance inside an instance").
        //     Carry it into the run and Player::UpdateHomebindTime repops the
        //     member at the entrance graveyard exactly 60s later — the whole
        //     party ends up outside, the DC strategy gate sees a bot no longer in
        //     a dungeon, and the run dies as "Left the dungeon" barely a minute
        //     in.
        bool evicting = false;
        for (Slot const& slot : _slots)
            if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
                if (Map const* map = bot->FindMap())
                    if (map->IsDungeon())
                    {
                        evicting = true;
                        if (!bot->IsBeingTeleported())
                            bot->TeleportTo(bot->GetHomebindMapId(), bot->GetHomebindX(),
                                            bot->GetHomebindY(), bot->GetHomebindZ(),
                                            bot->GetOrientation());
                    }
        if (evicting)
        {
            if (_stageMs >= TELEPORT_TIMEOUT_MS)
                FailSetup("could not clear the party out of the dungeon it was already in");
            return;  // retry next tick
        }

        // Every member sheds every bind for this map, both difficulties, on
        // every run. A bind is what decides WHICH COPY of the map a teleport
        // lands in (InstanceSaveMgr::PlayerGetDestinationInstanceId), so a
        // leftover one drags the party into an instance somebody already
        // cleared: the verdict's GetCompletedEncounterMask baseline starts with
        // bosses dead and TickStarting refuses the run as stale.
        //
        // This used to be a roster-only sweep, with pool runs shedding only the
        // permanent heroic saves on the theory that normal 5-man saves are
        // non-permanent and reset themselves when the map empties. They do —
        // eventually, and only if the map ever empties. A worldserver restart
        // in the middle of a plan leaves the interrupted runs' rows sitting in
        // character_instance, and the next plan's recycled pool bots walk
        // straight back into those half-cleared copies. Unbinding
        // unconditionally is a few guid-keyed deletes and removes the whole
        // class of failure.
        //
        // The cost is real but small: re-entering a bound instance is free
        // against AccountInstancesPerHour, and a fresh one is not, so a bot
        // recycled through more runs than that cap in an hour will now be
        // refused by name in CheckInstanceBudget rather than silently reusing a
        // copy. The pool is far larger than any one plan, so that is a
        // theoretical cost against a defect we have actually been paying.
        UnbindFromMap();

        // Now that no member is bound, entering costs each account one of its
        // AccountInstancesPerHour slots — refuse by name here rather than let the
        // core silently abort the transfer and time this stage out.
        if (!CheckInstanceBudget())
            return;

        // LEADER FIRST, ALONE. The destination copy is resolved per member at
        // worldport-ack time from the GROUP LEADER's bind; with nobody bound
        // that lookup returns 0, which means "mint a brand-new instance". Fire
        // all five teleports at once and every member that resolves before the
        // first one has actually been added to a map mints a copy of its own —
        // we have watched ten parties scatter across twenty-four copies of the
        // same map in a nine-second window, each tank alone or nearly so, the
        // run then stalling forever on teammates it can never reach.
        //
        // Sending the tank by itself closes the window: once it is inside, its
        // bind names the copy, and every later arrival resolves to that same id
        // no matter how the acks interleave.
        Player* const leader = FindTank();
        if (!leader)
        {
            if (_stageMs >= TELEPORT_TIMEOUT_MS)
                FailSetup("tank vanished before teleport");
            return;
        }
        leader->TeleportTo(_mapId, _x, _y, _z, _o);
        _teleportIssued = true;
    }

    // Wave 1 — wait for the leader to be standing in a copy of its own.
    Player* const tank = FindTank();
    if (!tank || tank->GetMapId() != _mapId || !tank->IsInWorld() ||
        tank->IsBeingTeleported() || !tank->GetInstanceId())
    {
        if (_stageMs >= TELEPORT_TIMEOUT_MS)
            FailSetup("tank did not arrive at the dungeon entrance");
        return;
    }

    // Wave 2 — the rest of the party, now that there is a copy to join.
    if (!_followersTeleported)
    {
        _destInstanceId = tank->GetInstanceId();
        for (std::size_t i = 1; i < _slots.size(); ++i)
            if (Player* bot = ObjectAccessor::FindPlayer(_slots[i].guid))
                bot->TeleportTo(_mapId, _x, _y, _z, _o);
        _followersTeleported = true;
    }

    // Arrival is the INSTANCE id, not the map id. Two members of one party can
    // both be "on map 601" and never see each other, and the distance helpers
    // do not check the instance either — a member in the wrong copy reports a
    // plausible 30-yard distToTank and the run reads as a party that is merely
    // lagging. Refusing here turns a twenty-minute run that could never have
    // progressed into a setup failure that names the members and their copies.
    std::string stranded;
    bool allThere = true;
    for (Slot const& slot : _slots)
    {
        Player* bot = ObjectAccessor::FindPlayer(slot.guid);
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        {
            allThere = false;
            continue;
        }
        if (bot->GetMapId() != _mapId || bot->GetInstanceId() != _destInstanceId)
        {
            allThere = false;
            if (!stranded.empty())
                stranded += ", ";
            stranded += Acore::StringFormat("{} (map {} instance {})", bot->GetName(),
                                            bot->GetMapId(), bot->GetInstanceId());
        }
    }

    if (allThere)
    {
        EnterStage(Stage::Starting);
        return;
    }

    if (_stageMs >= TELEPORT_TIMEOUT_MS)
    {
        if (stranded.empty())
            FailSetup("party did not arrive at the dungeon entrance");
        else
            FailSetup(Acore::StringFormat(
                "party split across instance copies — the tank is in instance {} but {}",
                _destInstanceId, stranded));
    }
}

void DcTestRunJob::SweepPartyGeometry()
{
    // Ground truth for the instance floor: the next boss's spawn Z, read off
    // the tank's own AI value. The symmetric column snap below answers a bot
    // standing ON the phantom terrain deck (map 36 carries the Westfall
    // surface ~200y above the mine as walkable mesh) with the deck itself -
    // the boss band is what pulls such a bot back down (live
    // tr-20260822-232939-1: the whole party marched at Z 263-297 toward
    // Sneed at Z 49 until the no-progress watchdog fired).
    float bossFloorZ = 0.0f;
    bool haveBossFloor = false;
    if (Player* tank = ObjectAccessor::FindPlayer(_tankGuid))
        if (PlayerbotAI* tankAI = GET_PLAYERBOT_AI(tank))
            if (AiObjectContext* tctx = tankAI->GetAiObjectContext())
            {
                std::optional<DungeonBossInfo> const nb =
                    tctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
                if (nb.has_value())
                {
                    bossFloorZ = nb->z;
                    haveBossFloor = true;
                }
            }

    // Altitude sanity, run by the MONITOR because nothing can gate it here:
    // stock movement repeatedly grounds party members on the OVERWORLD
    // height over the Deadmines entrance (Z 130..216, real floor ~60). The
    // AI-side rescue (stranded trigger -> Recover) loses the relevance race
    // against resting/looting every tick, so the run supervisor drags any
    // member hovering >25y off the mesh straight back down.
    for (Slot const& slot : _slots)
        if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
            if (Map* botMap = bot->FindMap())
            {
                // Geo fence FIRST, and OUTSIDE the dungeon gate - a wanderer
                // is by definition NOT on the dungeon map, so a fence nested
                // under IsDungeon() never sees him (live: a dps strolled to
                // Elwynn while the fence idled). Bring him back to the tank;
                // the group bind resolves the same instance copy.
                if (!botMap->IsDungeon())
                {
                    if (Player* tank = ObjectAccessor::FindPlayer(_tankGuid))
                        if (Map* tankMap = tank->FindMap())
                            if (tankMap->IsDungeon() && bot != tank && bot->IsAlive() &&
                                !bot->IsBeingTeleported())
                            {
                                LOG_INFO("playerbots.dungeonclear",
                                         "TESTRUN {} geo fence: {} left the dungeon — teleporting back to the tank",
                                         _record.runId, bot->GetName());
                                bot->TeleportTo(tankMap->GetId(), tank->GetPositionX(),
                                                tank->GetPositionY(), tank->GetPositionZ(),
                                                bot->GetOrientation());
                            }
                    continue;
                }

                {
                    // DISTANCE FENCE, supervisor side. A member that ends up
                    // far from the tank while still ON the dungeon map is
                    // invisible to the geo fence above (that one only catches
                    // bots who left the map) and the AI-side stranded
                    // recovery loses the relevance race against the very
                    // "waiting on X (out of range)" gate the stray causes -
                    // live tr-20260824-135423-3: a dps died, was resurrected
                    // at the ENTRANCE and stood there while the party waited
                    // 300+ ticks 300yd deeper in. The supervisor cannot be
                    // starved, so it owns the last-resort rescue: sustained
                    // distance, nobody in combat, then teleport to the tank.
                    if (Player* tank = ObjectAccessor::FindPlayer(_tankGuid))
                    {
                        uint32 const slotKey = slot.guid.GetCounter();
                        uint32 const nowFar = getMSTime();
                        // NOT `far`: windef.h defines `far` and `near` as EMPTY macros, a
                        // 16-bit leftover, so `bool far = false;` becomes
                        // `bool = false;` under MSVC. Reported from a Windows
                        // build 2026-09-02: C2513 here, then C2059 at each
                        // later use.
                        bool isFar = false;
                        // A combat FLAG is not a fight. Live: three resurrected
                        // dps stood at the entrance 250yd from the party, all
                        // flagged in combat with no victim at all, so the
                        // original "nobody in combat" gate never let the fence
                        // fire and the run bled out its whole window. Real
                        // fighting (a live victim) still protects a member;
                        // beyond 150yd even that is stale, because nothing the
                        // party fights can be that far away.
                        float const fenceDist = tank->GetDistance(bot);
                        bool const reallyFighting =
                            (bot->GetVictim() != nullptr || tank->GetVictim() != nullptr) &&
                            fenceDist <= 150.0f;
                        if (bot != tank && bot->IsAlive() && tank->IsAlive() &&
                            !bot->IsBeingTeleported() && tank->FindMap() == botMap &&
                            !reallyFighting && fenceDist > 120.0f)
                        {
                            isFar = true;
                            uint32& since = _farSinceMs[slotKey];
                            if (since == 0)
                                since = nowFar ? nowFar : 1;
                            else if (getMSTimeDiff(since, nowFar) > 45000)
                            {
                                // WALK, do not teleport. Moving a straggler to
                                // the party skips ground it is supposed to
                                // cover, and the recorder then captures that
                                // jump as if it were a path. Order it to run
                                // to the tank instead; if it cannot get
                                // there, the run fails honestly.
                                LOG_INFO("playerbots.dungeonclear",
                                         "TESTRUN {} distance fence: {} is {}yd behind at "
                                         "{:.0f},{:.0f},{:.0f} (tank at {:.0f},{:.0f},{:.0f}) — sending it "
                                         "running to the tank",
                                         _record.runId, bot->GetName(),
                                         int(tank->GetDistance(bot)),
                                         bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                         tank->GetPositionX(), tank->GetPositionY(), tank->GetPositionZ());
                                bot->GetMotionMaster()->Clear();
                                bot->GetMotionMaster()->MovePoint(0, tank->GetPositionX(),
                                                                  tank->GetPositionY(),
                                                                  tank->GetPositionZ(),
                                                                  FORCED_MOVEMENT_NONE, 0.0f, 0.0f,
                                                                  /*generatePath*/ true, false);
                                since = 0;
                                continue;
                            }
                        }
                        if (!isFar)
                            _farSinceMs[slotKey] = 0;
                    }

                    float const bz = bot->GetPositionZ();
                    // Boss-band rescue first (see the header note above).
                    //
                    // 80 yards, not 25. The phantom deck sits ~200y over the
                    // mine, but the mine ITSELF is 40y tall - Cookie stands at
                    // Z 17, Rhahk'Zor at Z 54, and the custom Voss wing at Z 54
                    // hangs 35y over Gilnid's foundry floor at Z 19. At 25y this
                    // rescue therefore fired on parties that were exactly where
                    // they belonged and dropped them a storey, 444 times in one
                    // evening: a vertical teleport that skipped the climb and
                    // wrote a jump into the route recording. The gap between
                    // "wrong deck" and "other floor of the same mine" is what
                    // the threshold has to name, and 80y names it.
                    float const DECK_BAND = 80.0f;
                    if (haveBossFloor && bz > bossFloorZ + DECK_BAND && !bot->IsBeingTeleported())
                    {
                        NavmeshSnap::Result const floorHit = NavmeshSnap::SnapColumn(
                            botMap, bot->GetPositionX(), bot->GetPositionY(),
                            bossFloorZ, /*halfHeight*/ 40.0f, /*radius*/ 8.0f);
                        if (floorHit.ok && bz - floorHit.z > DECK_BAND)
                        {
                            bot->GetMotionMaster()->Clear();
                            bot->NearTeleportTo(floorHit.x, floorHit.y, floorHit.z,
                                                bot->GetOrientation(),
                                                /*casting*/ false, /*vehicle*/ false,
                                                /*withPet*/ true);
                            LOG_INFO("playerbots.dungeonclear",
                                     "TESTRUN {} altitude sanity: {} floor-banded {}y "
                                     "down off the phantom deck",
                                     _record.runId, bot->GetName(), int(bz - floorHit.z));
                            continue;
                        }
                    }
                    NavmeshSnap::Result const column = NavmeshSnap::SnapColumn(
                        botMap, bot->GetPositionX(), bot->GetPositionY(), bz);
                    if (column.ok && std::fabs(column.z - bz) > 25.0f)
                    {
                        bot->GetMotionMaster()->Clear();
                        bot->NearTeleportTo(column.x, column.y, column.z,
                                            bot->GetOrientation(),
                                            /*casting*/ false, /*vehicle*/ false,
                                            /*withPet*/ true);
                        LOG_INFO("playerbots.dungeonclear",
                                 "TESTRUN {} altitude sanity: {} column-snapped {}y onto the mesh",
                                 _record.runId, bot->GetName(), int(std::fabs(column.z - bz)));
                    }
                }
            }
}

void DcTestRunJob::TickStarting()
{
    Player* tank = FindTank();
    PlayerbotAI* tankAI = tank ? GET_PLAYERBOT_AI(tank) : nullptr;
    if (!tank || !tankAI)
    {
        if (_stageMs >= START_TIMEOUT_MS)
            FailSetup("tank vanished before start");
        return;
    }

    // Freshly-teleported bots may not have the DC strategies installed yet
    // (contexts register on world ticks) — re-assert every tick; idempotent.
    for (Slot const& slot : _slots)
        if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
            DcStrategyGate::Reconcile(bot);

    SweepPartyGeometry();


    AiObjectContext* ctx = tankAI->GetAiObjectContext();

    // A reused instance with dead bosses would flash an instant (false)
    // all-clear — refuse it rather than record a fake success.
    if (uint32 const staleMask = DcEncounterMask::Get(tank->FindMap()))
        {
            FailSetup("stale instance: encounters already completed (mask " +
                      std::to_string(staleMask) + ")");
            return;
        }

    // The boss roster needs a tick or two to populate after the teleport;
    // retry inside the stage timeout.
    std::vector<DungeonBossInfo> const bosses =
        ctx->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Get();
    if (bosses.empty())
    {
        if (_stageMs >= START_TIMEOUT_MS)
            FailSetup("no boss roster for this map");
        return;
    }

    if (!_dcOnIssued)
    {
        _record.instanceId = tank->FindMap()->GetInstanceId();
        _roster.clear();
        for (DungeonBossInfo const& b : bosses)
        {
            BossRef ref;
            ref.entry = b.entry;
            ref.encounterIndex = b.encounterIndex;
            ref.name = b.name;
            ref.isBoss = b.kind == DungeonAnchorKind::Boss;
            _roster.push_back(ref);
            // Objectives (anchor kinds) carry no name worth aggregating across a
            // plan — only real bosses go into the reported roster.
            if (ref.isBoss)
                _record.bossRoster.push_back(ref.name);
        }
        _record.bossesTotal = static_cast<uint32>(_roster.size());

        // The run must never wait for a human: kill the WaitAtBoss pre-pull
        // hold for this run whatever the conf says.
        DcSettings::SetOverride(_tankGuid, "WaitAtBoss", 0.0);
        _dcOnIssued = true;
    }

    // Retry `dc on` each tick until the enabled flag sticks (roster/context
    // timing) or the stage times out.
    bool const dcOnOk = tankAI->DoSpecificAction("dc on", Event("dc", "", FindGm()), true);
    if (!dcOnOk && (!_dcOnFalseLoggedAt || getMSTimeDiff(_dcOnFalseLoggedAt, getMSTime()) > 10000))
    {
        // "dc on did not take" left no trace 3x a night: neither DcRefuse nor the
        // not-leader branch logged, so the action never ran at all. Name the
        // state the tank is in when DoSpecificAction says no (2026-09-05).
        _dcOnFalseLoggedAt = getMSTime();
        LOG_INFO("playerbots.dungeonclear",
                 "TESTRUN {} dc on: DoSpecificAction returned false for {} (inWorld {}, map {}, teleporting {}, group {}, dcLeader {}, gm {}, stage {}s)",
                 _record.runId, tank->GetName(), tank->IsInWorld() ? 1 : 0, tank->GetMapId(),
                 tank->IsBeingTeleported() ? 1 : 0, tank->GetGroup() ? 1 : 0,
                 DcLeaderSignal::IsDungeonClearLeader(tank) ? 1 : 0, FindGm() ? 1 : 0, _stageMs / 1000);
    }
    if (DcRun::Of(ctx).enabled)
    {
        _lastMask = DcEncounterMask::Get(tank->FindMap());
        _lastAnchors =
            ctx->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get().size();
        LOG_INFO("playerbots.dungeonclear",
                 "TESTRUN {} running: instance {} with {} bosses/objectives",
                 _record.runId, _record.instanceId, _record.bossesTotal);
        // Nobody in this party has a game client, so nobody will ever send a
        // CMSG_AREATRIGGER and no `at_*` script would run for the whole clear.
        // Armed here rather than at Teleporting so it covers exactly the window
        // the party is actually walking the dungeon.
        _areaTriggers.Arm(_mapId);
        EnterStage(Stage::Monitoring);
        return;
    }

    if (_stageMs >= START_TIMEOUT_MS)
        FailSetup("dc on did not take (look for 'DC command refused' in the DC log)");
}

// File a death for every member who went from standing to a corpse since the
// last sample, stamped with what the party was fighting at the time.
//
// MUST run before TrackEngagement folds this tick's sample: `_engaged` still
// holds the picture from the last tick in which the victim was alive, which is
// exactly the mob to blame. Folding first would attribute the death to whatever
// survives the fold — nothing at all, once the survivors drop combat.
//
// Only members on the leader's map are watched. A bot who left the instance (or
// logged out) is not a casualty, and its absence must not be read as a death.
void DcTestRunJob::TrackDeaths(Player* tank)
{
    if (!tank)
        return;

    auto observe = [this](Player* member)
    {
        if (!member || !member->IsInWorld())
            return;
        bool const alive = member->IsAlive();
        auto const [it, fresh] = _aliveLast.emplace(member->GetObjectGuid(), alive);
        if (fresh)
            return;  // seeding this member — no edge to report yet

        bool const wasAlive = it->second;
        it->second = alive;
        if (wasAlive == alive || alive)
            return;

        DcTestRunRecord::DeathEntry death;
        death.t = _totalMs / 1000;
        death.name = member->GetName();
        death.opponent = _engaged.name;
        death.opponentEntry = _engaged.entry;
        death.onBoss = _engaged.isBoss;
        _record.deaths.push_back(death);
        // Deliberately overwritten even when the latch is empty: this means
        // "what the LAST death was to", not "the last death that had a killer".
        // A member who dropped combat and then fell off a ledge must not be
        // filed against the boss the party disengaged from ten minutes earlier.
        _lastDeathEngaged = _engaged;
    };

    auto onTankMap = [tank](Player* member)
    { return member && member->IsInWorld() && member->GetMapId() == tank->GetMapId(); };

    if (onTankMap(tank))
        observe(tank);
    if (Group* group = tank->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member != tank && onTankMap(member))
                observe(member);
}

// Who to blame for a run people died in. DcTestRun::BlameFor owns the rule.
DcTestRun::Engagement DcTestRunJob::DeathBlame() const
{
    return DcTestRun::BlameFor(_engaged, _lastDeathEngaged);
}

// Gather this sample's combat picture off the party and fold it into the
// engagement latch (DcTestRun::UpdateEngagement owns the rule).
//
// Only members that are alive and on the leader's map are read: a corpse holds
// neither a victim nor an attacker list, so a party being wiped simply stops
// contributing and the last engagement seen while somebody was standing is what
// the wipe verdict reports.
//
// The run's own roster is the authority on what counts as a boss — it is what
// every other line of the report names bosses by — and the creature-side flags
// are the fallback for a summoned or phase-swapped boss the roster never listed
// (Anzu, a Tuten'kash-style event spawn).
void DcTestRunJob::TrackEngagement(Player* tank)
{
    if (!tank)
        return;

    DcTestRun::EngagementSample sample;

    auto rosterName = [this](uint32 entry) -> std::string const*
    {
        for (BossRef const& ref : _roster)
            if (ref.entry == entry && ref.isBoss)
                return &ref.name;
        return nullptr;
    };

    auto consider = [&](Unit const* u)
    {
        if (!u || !u->IsAlive())
            return;
        Creature const* creature = u->ToCreature();
        if (!creature)
            return;
        std::string const* named = rosterName(creature->GetEntry());
        if (sample.bossName.empty() &&
            (named || creature->IsWorldBoss()
             /* no IsDungeonBoss flag on 1.12 - the roster names, fed from our
                baked boss list, already cover the dungeon bosses */))
        {
            sample.bossEntry = creature->GetEntry();
            sample.bossName = named ? *named : creature->GetName();
        }
        if (sample.trashName.empty())
        {
            sample.trashEntry = creature->GetEntry();
            sample.trashName = creature->GetName();
        }
    };

    auto scan = [&](Player* member)
    {
        if (!member || !member->IsAlive() || !member->IsInWorld() ||
            member->GetMapId() != tank->GetMapId())
            return;
        sample.anyAlive = true;
        if (!member->IsInCombat())
            return;
        sample.anyAliveInCombat = true;
        // The victim covers whoever the member is swinging at; the attacker set
        // covers the mob beating on a member with no target of its own (a
        // healer, or anyone the pack peeled onto).
        consider(member->GetVictim());
        for (Unit const* attacker : member->getAttackers())
            consider(attacker);
    };

    scan(tank);
    if (Group* group = tank->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member && member != tank)
                scan(member);

    _engaged = DcTestRun::UpdateEngagement(_engaged, sample);
}

// File the pull observation currently in flight, if any. `wipedHere` marks the
// pull the run died on — the single most useful row in the log, since it names
// the pack that ended the run alongside what the governor thought it was.
void DcTestRunJob::ClosePull(bool wipedHere)
{
    if (!_pullOpen)
        return;
    _pullOpen = false;
    _pullEntry.wipedHere = wipedHere;
    if (_record.pulls.size() < DcTestRunRecord::kPullLog)
        _record.pulls.push_back(_pullEntry);
    else
        ++_record.pullsElided;
}

// Pair each Dynamic-pull verdict with the number of mobs that actually turned
// up for it. See DcTestRunRecord::PullEntry for why the pairing (not either
// number alone) is what diagnoses an over-pulling tank.
//
// The observation runs on the leader's DcPullContext:
//
//   decisionSeq changed  -> a NEW pack was latched: close the old record, open
//                           one stamped with this verdict's prediction.
//   decision == None     -> the governor dropped its verdict (pack dead, target
//                           lost past the grace); once the party is also out of
//                           combat the fight is over and the record closes.
//
// The observed count is the union, over every alive on-map member, of what it is
// swinging at and what is swinging at it — the same reach TrackEngagement uses,
// widened from "name one opponent" to "count them all". Sampled at MONITOR_STEP_MS,
// so it is a floor on what was fought: a mob that joined and died inside one
// second never appears.
void DcTestRunJob::TrackPulls(Player* tank)
{
    if (!tank)
        return;
    PlayerbotAI* ai = GET_PLAYERBOT_AI(tank);
    AiObjectContext* ctx = ai ? ai->GetAiObjectContext() : nullptr;
    if (!ctx)
        return;

    DcPullContext const& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();

    if (pull.decisionSeq != _pullSeq)
    {
        ClosePull();
        _pullSeq = pull.decisionSeq;
        _pullEntry = DcTestRunRecord::PullEntry{};
        _pullEntry.t = _totalMs / 1000;
        _pullEntry.targetEntry = pull.decisionTargetEntry;
        _pullOpen = true;
    }

    if (!_pullOpen)
        return;

    // Refresh the prediction every sample while the record is open: the governor
    // re-checks a standing Leeroy on a throttle and may UPGRADE it to Advanced,
    // which re-estimates the pack. What we want on file is the estimate the
    // COMMITTED verdict was taken from, i.e. the last one before the pull ends.
    _pullEntry.predictedCount = pull.predictedCount;
    _pullEntry.predictedThirds = pull.predictedThirds;
    _pullEntry.ceilingThirds = pull.predictedCeiling;
    if (pull.decision == DcPullDecisionCode::Advanced)
        _pullEntry.advanced = true;

    std::set<ObjectGuid> engaged;
    std::uint32_t elites = 0;
    auto consider = [&](Unit const* u)
    {
        if (!u || !u->IsAlive())
            return;
        Creature const* c = u->ToCreature();
        if (!c || c->IsCritter() || c->IsTotem())
            return;
        if (engaged.insert(c->GetObjectGuid()).second && c->isElite())
            ++elites;
    };
    auto scan = [&](Player* member)
    {
        if (!member || !member->IsAlive() || !member->IsInWorld() ||
            member->GetMapId() != tank->GetMapId() || !member->IsInCombat())
            return;
        consider(member->GetVictim());
        for (Unit const* attacker : member->getAttackers())
            consider(attacker);
    };
    scan(tank);
    if (Group* group = tank->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member && member != tank)
                scan(member);

    if (engaged.size() > _pullEntry.observedMax)
    {
        _pullEntry.observedMax = static_cast<std::uint32_t>(engaged.size());
        _pullEntry.observedElites = elites;
    }

    // Verdict dropped AND the fight resolved: this pull is history. Holding the
    // record open until combat clears is what keeps a Leeroy on file — its
    // verdict drops the moment the pack dies, which is also when the fight ends.
    if (pull.decision == DcPullDecisionCode::None && engaged.empty())
        ClosePull();
}

// Is anyone in the party on the leader's map a corpse right now? Separates a
// run that ended because people died (worth the wipe post-mortem) from one that
// ended for any other reason.
bool DcTestRunJob::AnyMemberDead(Player* tank)
{
    if (!tank)
        return false;
    if (!tank->IsAlive())
        return true;
    Group* group = tank->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == tank || member->GetMapId() != tank->GetMapId())
            continue;
        if (!member->IsAlive())
            return true;
    }
    return false;
}

void DcTestRunJob::TickMonitoring(uint32 dt)
{
    // Repair party geometry BEFORE reading it: fence overworld wanderers
    // back to the tank and column-snap mid-air members, so the observation
    // below scores the repaired state.
    SweepPartyGeometry();

    DcTestRun::Observation obs;
    obs.elapsedMs = _monitorMs;
    obs.gmOnline = true;  // GM absence is handled in Tick()

    {
        std::lock_guard<std::mutex> lock(_obsMutex);
        obs.abortRequested = _abortRequested;
        obs.disableFired = _disableFired;
        obs.disableAllCleared = _disableAllCleared;
    }

    // Connected-lookup + explicit teleport tolerance: DC's own scripted
    // events teleport the party mid-run (Underbog hops, Old Hillsbrad's
    // muster) — a sample landing in that window must wait, not read the
    // leader as gone.
    Player* tank = ObjectAccessor::FindConnectedPlayer(_tankGuid);
    PlayerbotAI* tankAI = tank ? GET_PLAYERBOT_AI(tank) : nullptr;
    std::string pauseReason;
    std::string stallReason;

    if (tank && tankAI && (!tank->IsInWorld() || tank->IsBeingTeleported()))
        return;  // mid-teleport — skip this sample, timers resume next tick

    // Keep the react-delay fast path alive for the WHOLE run, not just from
    // Grouping (see ReassertMaster — the leader was observed losing its master and
    // dropping to a 1-3 second think interval for the entire clear).
    ReassertMaster();

    // A roster member vanishing from the world mid-run means its owner logged in:
    // playerbots' secure-login hook force-logs-out an active altbot when a real
    // CMSG_PLAYER_LOGIN arrives for it, so the human cleanly takes their character
    // back. Name that outcome. Without this the run limps on a member short and
    // eventually files a no-progress or wipe failure — a real regression report
    // for something that was never the AI's doing. Session-based lookup, so DC's
    // own mid-run teleports (handled above) cannot trip it.
    if (_realChars)
    {
        for (Slot const& slot : _slots)
        {
            if (!slot.guid || ObjectAccessor::FindConnectedPlayer(slot.guid))
                continue;
            Finish(DcTestRun::Verdict::FailAborted,
                   "roster member " + slot.rosterName +
                       " left the run (owner logged in, or the character was logged out)");
            return;
        }
    }

    if (!tank || !tankAI)
    {
        // A tank BETWEEN MAPS is not a lost tank. FindTank uses
        // ObjectAccessor::FindPlayer, which returns null for a Player whose
        // IsInWorld() is false - and that is precisely the state a teleport
        // passes through. So an ordinary map change read as "the leader
        // vanished" and killed the run.
        //
        // Measured 2026-09-02: 37 Dragonmaw runs died overnight on "leader tank
        // left the world" while the destruction probe (DC-BOTLIFE, fired from
        // Player::~Player for any bot still under run protection) reported
        // ZERO. Nothing was ever destroyed; every one of them was this false
        // alarm. It is the same mistake a guard of mine made the day before,
        // from the other direction - asking a registry whether a pointer is
        // good, when the honest answer during a teleport is "not right now".
        //
        // Only FindPlayerNotInWorld coming back empty means the Player is
        // really gone. A transient absence is left alone: the no-progress
        // watchdog already ends a run that stops getting anywhere, so this
        // check does not have to be the safety net as well.
        Player* const tankAnywhere = ObjectAccessor::FindPlayerNotInWorld(_tankGuid);
        if (!tankAnywhere)
        {
            obs.leaderMissing = true;
            obs.leaderGoneFromWorld = true;
        }
        else if (!GET_PLAYERBOT_AI(tankAnywhere))
        {
            // Present but without its AI - a real fault, and a different one.
            obs.leaderMissing = true;
            obs.leaderGoneFromWorld = false;
        }
        // else: in transit between maps. Say nothing and let the run continue.
    }
    else
    {
        AiObjectContext* ctx = tankAI->GetAiObjectContext();
        DcRunState const& rs = DcRun::Of(ctx);

        // The disable funnel notifies OnRunDisabled; this catches any path
        // that somehow bypassed it (belt and braces — enabled off without a
        // callback still ends the run).
        if (!obs.disableFired && !rs.enabled)
        {
            obs.disableFired = true;
            std::lock_guard<std::mutex> lock(_obsMutex);
            if (_disableReason.empty())
                _disableReason = "run disabled (no reason captured)";
        }

        // The anchor the run is currently heading for, for the closing-distance
        // signal below.
        std::optional<DungeonBossInfo> const next =
            ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();

        uint32 const mask = tank->FindMap() ? DcEncounterMask::Get(tank->FindMap()) : _lastMask;
        std::size_t const anchors =
            ctx->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get().size();

        bool progressed = false;
        if (uint32 const fresh = mask & ~_lastMask)
        {
            for (uint32 bit = 0; bit < 32; ++bit)
            {
                if (!(fresh & (1u << bit)))
                    continue;
                DcTestRunRecord::BossKill kill;
                kill.t = _totalMs / 1000;
                kill.via = "mask";
                // Prefer a real BOSS for the name. Objectives reuse a boss's
                // encounterIndex as an ordering hint (Uldaman's two altars both say
                // 7, Archaedas's bit), and taking the first match labelled a kill
                // "Altar of the Keepers" in six runs on 2026-09-03.
                for (int pass = 0; pass < 2 && kill.name.empty(); ++pass)
                    for (BossRef const& ref : _roster)
                        if (ref.encounterIndex == bit && (pass == 1 || ref.isBoss))
                        {
                            kill.entry = ref.entry;
                            kill.name = ref.name;
                            break;
                        }
                if (kill.name.empty())
                    kill.name = "encounter #" + std::to_string(bit);
                _record.bossTimeline.push_back(kill);
                progressed = true;
            }
            _lastMask = mask;
        }
        // A scripted encounter advancing (Zul'Farrak's pyramid phases, a wave
        // counter) and an event step advancing are HARD progress too. Without
        // this, a party holding the temple ramp through the waves closed no
        // distance and flipped no bit for the whole encounter, and the cap
        // ended five runs mid-event (arch16, 2026-09-05).
        {
            uint32 sig = 2166136261u;
            if (InstanceData* inst = tank->FindMap() ? tank->FindMap()->GetInstanceData() : nullptr)
                for (uint32 i = 0; i < 10; ++i)
                    sig = (sig ^ inst->GetData(i)) * 16777619u;
            if (_lastInstanceDataSig && sig != _lastInstanceDataSig)
                progressed = true;
            _lastInstanceDataSig = sig;
            uint32 const evMs = ctx->GetValue<DungeonEventProgress&>(DcKey::EventProgress)->Get().progressMs;
            if (_lastEventProgressMs && evMs != _lastEventProgressMs)
                progressed = true;
            _lastEventProgressMs = evMs;
        }
        if (anchors > _lastAnchors)
        {
            for (std::size_t i = _lastAnchors; i < anchors; ++i)
            {
                DcTestRunRecord::BossKill kill;
                kill.t = _totalMs / 1000;
                kill.name = "objective";
                kill.via = "anchor";
                _record.bossTimeline.push_back(kill);
            }
            _lastAnchors = anchors;
            progressed = true;
        }
        if (progressed)
            _record.bossesKilled =
                std::min<uint32>(static_cast<uint32>(_record.bossTimeline.size()),
                                 _record.bossesTotal);

        // A kill is not the only evidence a run is alive. Keyed on kills alone
        // the watchdog fails runs that are working perfectly: a party can
        // legitimately spend more than NoProgressS on the trash between two
        // bosses — or on the way to the first one, where the clock starts at
        // zero and nothing has been killed yet.
        //
        // What counts is that something CHANGES, never that some state holds.
        // "In combat" held true is the signature of the phantom-combat
        // ghost-flag deadlock, so counting combat itself as progress would make
        // that bug undetectable by construction — the run would sit in a dead
        // fight forever and the watchdog would call it healthy the whole time.
        //
        // So compare this tick's combat picture against last tick's:
        //
        //   entering or leaving combat — a pack pulled, or a pack died.
        //   a different victim         — one mob down, on to the next.
        //   the victim's health moved  — the fight itself is progressing,
        //                                which is what carries a long single
        //                                target where nothing else changes.
        //
        // A real fight moves at least one of those every sample. A wedged one
        // moves none: stuck flag, no victim or a stale one, health frozen.
        // PARTY-wide, not tank-only: live, the tank idled at the entrance
        // while the dps fought their way forward - a perfectly alive run
        // that read as frozen because only the tank's combat picture was
        // sampled. Fold every member's combat state, victim and victim
        // health into one fingerprint; a wedged run still freezes it (a
        // stuck flag with a stale victim moves nothing party-wide either).
        uint32 partySig = 2166136261u;
        auto fold = [&partySig](uint32 v)
        {
            partySig = (partySig ^ v) * 16777619u;
        };
        for (Slot const& sigSlot : _slots)
        {
            Player* member = ObjectAccessor::FindPlayer(sigSlot.guid);
            if (!member)
                continue;
            fold(member->IsInCombat() ? 1u : 0u);
            Unit const* mv = member->GetVictim();
            fold(mv ? mv->GetObjectGuid().GetCounter() : 0u);
            fold(mv ? static_cast<uint32>(mv->GetHealthPct()) : 0u);
        }
        bool const combatChurn = partySig != _lastPartyCombatSig;
        _lastPartyCombatSig = partySig;

        // Sampled every monitor step while somebody is still standing, so the
        // wipe verdict below can name what took the party down. Deaths are read
        // first, against the previous tick's latch — see TrackDeaths.
        TrackDeaths(tank);
        TrackEngagement(tank);
        TrackPulls(tank);

        if (next.has_value() && next->mapId == tank->GetMapId())
        {
            // Re-arm on target change: distance to a NEW anchor is unrelated to
            // the best held for the old one, and would otherwise read as an
            // instant regression that never recovers.
            if (next->entry != _bestDistEntry)
            {
                _bestDistEntry = next->entry;
                _bestDist = -1.f;
            }
            float const dist = tank->GetDistance(next->x, next->y, next->z);
            if (_bestDist < 0.f || dist < _bestDist - DC_TESTRUN_PROGRESS_EPSILON_YD)
            {
                _bestDist = dist;
                progressed = true;
            }
        }

        // Combat churn (members' combat state / victim / victim health) counts
        // as progress only for a bounded stretch after the last HARD progress
        // (a kill-bit, a cleared anchor, closing distance). Unbounded, a party
        // locked in a fight it cannot finish never trips the watchdog: on
        // 2026-09-04 08:22-09:58 six of nine ladder slots sat in Uldaman's
        // keeper hall for 60-96 minutes (~1200 "first contact: Stone Keeper"
        // lines per tank per 14 min), no boss and no objective for over an
        // hour, and the 420s no-progress limit never fired once.
        if (progressed)
            _sinceHardProgressMs = 0;
        else
            _sinceHardProgressMs += dt;
        if (!progressed && combatChurn && _limits.noProgressMs &&
            _sinceHardProgressMs < 3 * _limits.noProgressMs)
            progressed = true;
        if (progressed)
        {
            _sinceProgressMs = 0;
            _frozenDumpLogged = false;
        }
        else
            _sinceProgressMs += dt;

        // THE FREEZE DUMP. Fired once, halfway into the no-progress window, on a
        // run that is going to fail its watchdog unless something changes — and
        // fired HERE rather than at teardown because the state that explains a
        // stuck-in-combat freeze does not survive to teardown: the units holding
        // the party evade, leash home or despawn in the minutes between, and the
        // record ends up naming nothing. Halfway is the compromise: late enough
        // that a slow pull or a long rez is not reported as a wedge, early enough
        // that the holders are still standing where they wedged it.
        //
        // Read-only (DcDiag::Capture is). Deliberately NOT gated on the tank's
        // own combat flag: in both MGT freezes this was written for, the members
        // held in combat were the tank AND one follower, and a variant where the
        // tank is clean while a follower is pinned wedges the run just as hard —
        // gating on `inCombat` here would report nothing on exactly that case.
        // The combat line is what stays conditional, so a run frozen on
        // navigation does not carry a blame line with nothing to blame.
        if (!_frozenDumpLogged && _limits.noProgressMs &&
            _sinceProgressMs >= _limits.noProgressMs / 2)
        {
            _frozenDumpLogged = true;
            DcDiag::Snapshot const frozen = DcDiag::Capture(tank, "frozen");
            LOG_INFO("playerbots.dungeonclear",
                     "TESTRUN FROZEN {} no progress for {}s (limit {}s) — {}",
                     _record.runId, _sinceProgressMs / 1000,
                     _limits.noProgressMs / 1000, DcDiag::Summarize(frozen));
            if (frozen.inCombatCount)
                LOG_INFO("playerbots.dungeonclear", "TESTRUN FROZEN {} combat blame: {}",
                         _record.runId, DcDiag::SummarizeCombat(frozen));
        }

        // Pause / stall trackers.
        if (rs.paused)
        {
            pauseReason = rs.pauseReason;
            if (!_wasPaused)
            {
                _record.pauses.push_back({_totalMs / 1000, pauseReason});
                _pausedForMs = 0;
            }
            else
                _pausedForMs += dt;
            _wasPaused = true;
        }
        else
        {
            _wasPaused = false;
            _pausedForMs = 0;
        }
        obs.paused = rs.paused;
        obs.pausedForMs = _pausedForMs;

        // Wipe tracker. The in-run death bailout (DungeonClearPartyDiedTrigger)
        // covers every case where SOMEONE is left alive to notice — it fires and
        // the disable funnel above ends the run with the death reason. It cannot
        // cover a full wipe: the trigger lives in the `dungeon clear` strategy,
        // which is on the non-combat and combat engines only, and a corpse runs
        // BOT_STATE_DEAD. So the harness has to see this one for itself.
        //
        // Dead = dead OR ghost (a released bot is still a wiped bot). Members off
        // the leader's map are ignored, matching the trigger's own convention:
        // someone alive back in town is not a party that is still fighting.
        bool anyAlive = tank->IsAlive();
        if (anyAlive)
            _lastAliveMember = tank->GetName();
        else if (Group* group = tank->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || member == tank || member->GetMapId() != tank->GetMapId())
                    continue;
                if (member->IsAlive())
                {
                    anyAlive = true;
                    _lastAliveMember = member->GetName();
                    break;
                }
            }
        }

        if (anyAlive)
            _wipedForMs = 0;
        else
            _wipedForMs += dt;
        obs.partyWiped = !anyAlive;
        obs.wipedForMs = _wipedForMs;

        stallReason = ctx->GetValue<std::string&>(DcKey::StallReason)->Get();
        if (!stallReason.empty())
            _stalledForMs += dt;
        else
            _stalledForMs = 0;
        obs.stalled = !stallReason.empty();
        obs.stalledForMs = _stalledForMs;

        obs.sinceProgressMs = _sinceProgressMs;
    }

    DcTestRun::Verdict const verdict = DcTestRun::Classify(obs, _limits);
    if (!DcTestRun::IsTerminal(verdict))
        return;

    std::string failReason;
    switch (verdict)
    {
        case DcTestRun::Verdict::Success:
            break;
        case DcTestRun::Verdict::FailDisabled:
        {
            std::string reason;
            {
                std::lock_guard<std::mutex> lock(_obsMutex);
                reason = _disableReason;
            }
            failReason = "run disabled: " + DcTestRun::StripResumeHint(reason);
            // The rez-recovery bailouts ("no one left alive can resurrect",
            // "couldn't get X resurrected in time") name the corpse but never
            // what put it there — and both fire after combat has ended, so the
            // blame has to come off the death log.
            if (AnyMemberDead(FindTank()))
                failReason += DcTestRun::BlameSuffix(DeathBlame());
            break;
        }
        case DcTestRun::Verdict::FailPartyWiped:
        {
            std::string const who =
                _lastAliveMember.empty() ? std::string()
                                         : " (last standing: " + _lastAliveMember + ")";
            DcTestRun::Engagement const blame = DeathBlame();
            if (blame.Empty())
                failReason = "party wiped out of combat" + who;
            else if (blame.isBoss)
                failReason = "party wiped on " + blame.name + who;
            else
                failReason = "party wiped to trash: " + blame.name + who;
            break;
        }
        case DcTestRun::Verdict::FailPausedTimeout:
            failReason = "paused for over " + std::to_string(_limits.pauseGraceMs / 1000) +
                         "s: " + (pauseReason.empty() ? "(no reason)" : pauseReason);
            break;
        case DcTestRun::Verdict::FailStalledTimeout:
            failReason = "stalled for over " + std::to_string(_limits.stallGraceMs / 1000) +
                         "s: " + (stallReason.empty() ? "(no reason)" : stallReason);
            break;
        case DcTestRun::Verdict::FailNoProgress:
            failReason = "no boss/objective progress for " +
                         std::to_string(_limits.noProgressMs / 1000) + "s";
            break;
        case DcTestRun::Verdict::FailOverallTimeout:
            failReason = "exceeded the overall time limit (" +
                         std::to_string(_limits.overallTimeoutMs / 1000) + "s)";
            break;
        case DcTestRun::Verdict::FailAborted:
        {
            std::lock_guard<std::mutex> lock(_obsMutex);
            failReason = obs.leaderMissing
                             ? (obs.leaderGoneFromWorld
                                    ? "leader tank left the world (logged out or removed)"
                                    : "leader tank lost its bot AI (still online)")
                         : (_abortReason.empty() ? "aborted" : _abortReason);
            break;
        }
        default:
            break;
    }

    Finish(verdict, failReason);
}

void DcTestRunJob::FailSetup(std::string const& why)
{
    _record.setupStage = StageName(_stage.load());
    _record.result = "setup_failed";
    _record.failReason = why;
    LOG_INFO("playerbots.dungeonclear", "TESTRUN {} setup failed at {}: {}",
             _record.runId, _record.setupStage, why);
    Teardown();
}

void DcTestRunJob::Finish(DcTestRun::Verdict verdict, std::string const& failReason)
{
    _record.result = DcTestRun::VerdictName(verdict);
    _record.failReason = failReason;

    // Wipe post-mortem. Also filled for a death bailout — the run ends as
    // "disabled" with a corpse in the party, and "what killed us" is the same
    // question there. Left empty for every outcome nobody died in, so a
    // successful run that lost (and rezzed) a member carries no stale opponent.
    bool const diedHere =
        verdict == DcTestRun::Verdict::FailPartyWiped || AnyMemberDead(FindTank());
    if (diedHere)
    {
        DcTestRun::Engagement const blame = DeathBlame();
        _record.wipeOnBoss = blame.isBoss;
        _record.wipeOpponentEntry = blame.entry;
        _record.wipeOpponent = blame.name;
    }
    // File the pull still in flight, flagged when the run ended on corpses: that
    // row is the pull the party did not survive, complete with what the governor
    // predicted for it.
    ClosePull(diedHere);
    {
        std::lock_guard<std::mutex> lock(_obsMutex);
        _record.disableReason = _disableReason;
    }
    Teardown();
}

// Put the party back the way the run found it: alive, and out of the instance.
//
// The bug this closes is a wipe. Every logout path below ends in
// WorldSession::LogoutPlayer, which repops a character that is still dead
// (BuildPlayerRepop -> RepopAtGraveyard) and then saves it — so a wiped party
// was written to the DB as five ghosts at the instance graveyard, and somebody's
// real character came out of a test run dead in a dungeon they never chose to
// enter. Being resurrected here is what stops LogoutPlayer taking that branch at
// all; the recall is the second half of the same promise.
//
// Ordering is load-bearing in two places:
//   * this must run BEFORE LogoutBots, for the reason above;
//   * the teleport may nonetheless be issued in the same tick as the logout,
//     because LogoutPlayer drains a pending far transfer first
//     (`while (IsBeingTeleportedFar()) HandleMoveWorldportAck()`), and until it
//     does, TeleportTo has already stored the destination for SaveToDB to use
//     in place of the live position.
//
// Nothing happens at all if the run never teleported the party in: a setup
// failure before Teleporting leaves the characters exactly where they were
// standing, which for a hand-picked real character is the only defensible
// outcome — the run never moved it, so the run does not get to move it.
void DcTestRunJob::ReviveAndSendHome()
{
    if (!_teleportIssued)
        return;

    for (Slot const& slot : _slots)
    {
        if (!slot.guid)
            continue;

        // Pool slots have no rosterName; the guid is the only handle that exists
        // before the character resolves, and the only one left if it never does.
        std::string const who = slot.rosterName.empty() ? slot.guid.ToString() : slot.rosterName;

        Player* bot = ObjectAccessor::FindPlayer(slot.guid);
        if (!bot || !bot->IsInWorld())
        {
            LOG_WARN("playerbots.dungeonclear",
                     "TESTRUN {} member {} is not in world at teardown — cannot revive or recall it",
                     _record.runId, who);
            continue;
        }

        bot->CombatStop(true);

        // Dead OR a released ghost: DeathState covers the un-released corpse,
        // PLAYER_FLAGS_GHOST (from the ghost aura, spell 8326) covers the member
        // that already ran back.
        if (!bot->IsAlive() || bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        {
            // Spirit of Redemption is a death the aura is only postponing —
            // LogoutPlayer kills the character outright when it sees this aura,
            // so it has to come off before the resurrect, not after.
            bot->RemoveAurasDueToSpell(27827);
            // No resurrection sickness. The character did not choose this death,
            // and a regression harness must not hand somebody's raider ten
            // minutes of it as the price of being volunteered.
            bot->ResurrectPlayer(1.0f, /*applySickness*/ false);
            // Turn the corpse to bones, or the character keeps a resurrectable
            // corpse inside an instance that is about to be unbound and reset.
            // Resurrect + SpawnCorpseBones is exactly what `.revive` does.
            bot->SpawnCorpseBones();
            LOG_INFO("playerbots.dungeonclear", "TESTRUN {} resurrected {} at teardown",
                     _record.runId, bot->GetName());
        }

        // A transfer already in flight owns the destination; issuing a second one
        // would be refused anyway (TeleportTo returns false while a semaphore is
        // set), so say where it landed instead of pretending it went home.
        if (bot->IsBeingTeleported())
        {
            LOG_WARN("playerbots.dungeonclear",
                     "TESTRUN {} {} is mid-transfer at teardown — left wherever that transfer lands",
                     _record.runId, bot->GetName());
            continue;
        }

        // The bind point: the innkeeper/hearthstone destination, read from the
        // same homebind fields the hearthstone spell itself uses. Deliberately
        // NOT the recorded entry position — that can be another instance, or a
        // spot the character can no longer legally be in an hour later, whereas
        // the bind point is by construction somewhere the character may stand.
        // The entry position stays in the record either way, so a manual recall
        // to it is still possible.
        if (!bot->TeleportTo(bot->GetHomebindMapId(), bot->GetHomebindX(), bot->GetHomebindY(),
                             bot->GetHomebindZ(), bot->GetOrientation()))
            LOG_WARN("playerbots.dungeonclear",
                     "TESTRUN {} could not send {} home to map {} — left in the instance",
                     _record.runId, bot->GetName(), bot->GetHomebindMapId());
    }
}

// Log the party out, whichever holder owns it.
//
// A bot's login is filed under the holder that owns its master *at callback
// time*, resolved by sWorldSessionMgr->FindSession(masterAccountId)
// (PlayerbotMgr.cpp AddPlayerBot / PlayerbotOperations.h OnBotLoginOperation).
// The test driver logs in through the masterless fake-session path, and those
// sessions are constructed directly — never handed to WorldSessionMgr::AddSession
// — so that lookup misses and every driver-run party lands in
// sRandomPlayerbotMgr instead of the driver's own PlayerbotMgr. The run itself
// never noticed (it resolves bots through ObjectAccessor), but LogoutPlayerBot
// keys off the holder's map and returned silently: the group disbanded, the
// bots left the instance, and then sat in the world logged in forever.
// A real GM's session *is* in WorldSessionMgr, so their runs took the first
// branch — which is why this only ever showed up on driver/dashboard runs.
void DcTestRunJob::LogoutBots(Player* gm)
{
    PlayerbotMgr* mgr = gm ? GET_PLAYERBOT_MGR(gm) : nullptr;
    for (Slot const& slot : _slots)
    {
        if (!slot.guid)
            continue;

        // Re-point each bot's master at ITSELF before the logout: the
        // logout path tells the master goodbye, and the master here is the
        // driver - whose Player can already be torn down on the abort paths
        // that reach this teardown (live: SIGSEGV in Object::GetByteValue
        // under PlayerbotSecurity::LevelFor(master) mid-teardown). A
        // self-master makes that tell provably safe.
        {
            Player* botPlayer = mgr ? mgr->GetPlayerBot(slot.guid) : nullptr;
            if (!botPlayer)
                botPlayer = sRandomPlayerbotMgr.GetPlayerBot(slot.guid);
            if (botPlayer)
                if (PlayerbotAI* botAi = GetBotAI(botPlayer))
                    botAi->SetMaster(botPlayer);
        }

        // Hand the login back to the random holder's normal policy before
        // logging it out (mark set at provisioning).
        sRandomPlayerbotMgr.SetExternallyManaged(slot.guid.GetCounter(), false);

        if (mgr && mgr->GetPlayerBot(slot.guid))
            mgr->LogoutPlayerBot(slot.guid);
        else if (sRandomPlayerbotMgr.GetPlayerBot(slot.guid))
            sRandomPlayerbotMgr.LogoutPlayerBot(slot.guid);
        else if (ObjectAccessor::FindConnectedPlayer(slot.guid))
            LOG_WARN("playerbots.dungeonclear",
                     "TESTRUN {} bot {} is online but owned by no holder — left logged in",
                     _record.runId, slot.guid.ToString());
    }
}

void DcTestRunJob::Teardown()
{
    // gates the observers off before DisableDungeonClear; the exchange also
    // makes a second Teardown (racing abort / double .dc test stop in one tick
    // gap) a no-op, so the record is appended and the bots logged out once.
    if (_stage.exchange(Stage::TearingDown) == Stage::TearingDown)
        return;

    // Report the relay's tally before Disarm() drops the volume list. Logged
    // whenever the map had anything to relay, INCLUDING zero fires — "0 of 1"
    // is the party never reaching the volume, which is a different failure from
    // a set-piece that fired and did nothing.
    if (uint32 const armed = _areaTriggers.Armed())
        LOG_INFO("playerbots.dungeonclear",
                 "TESTRUN {} areatrigger relay: {} packet(s) over {} volume(s)", _record.runId,
                 _areaTriggers.Relayed(), armed);
    _areaTriggers.Disarm();

    Player* tank = FindTank();
    if (tank)
    {
        _record.finalMap = tank->GetMapId();
        _record.finalX = tank->GetPositionX();
        _record.finalY = tank->GetPositionY();
        _record.finalZ = tank->GetPositionZ();

        // Capture the full picture BEFORE anything below tears the party down:
        // DisableDungeonClear resets the run state, Group::Disband drops every
        // member, and LogoutBots removes them from the world. Afterwards there
        // is nothing left to describe why the run failed.
        _record.diag = DcDiag::Capture(tank, "teardown");
        _record.stallAtEnd = _record.diag.stallReason;
        _record.phaseAtEnd = _record.diag.phase;
        if (_record.diag.valid && _record.result != "success")
        {
            LOG_INFO("playerbots.dungeonclear", "TESTRUN DIAG {} {}",
                     _record.runId, DcDiag::Summarize(_record.diag));
            // Second line, only when it has something to say. The freeze dump
            // above is the authoritative one (it sampled while the party was
            // still wedged); this is the same question asked at teardown, and
            // the pair together show whether the holders changed in between —
            // which is itself the answer on a run that recovered and re-froze.
            if (_record.diag.inCombatCount)
                LOG_INFO("playerbots.dungeonclear", "TESTRUN DIAG {} combat blame: {}",
                         _record.runId, DcDiag::SummarizeCombat(_record.diag));
        }

        if (PlayerbotAI* tankAI = GET_PLAYERBOT_AI(tank))
            if (DcRun::Of(tankAI).enabled)
                DcActionShared::DisableDungeonClear(tankAI, "test run teardown");
    }

    if (_tankGuid)
        DcSettings::ClearRun(_tankGuid);

    if (tank)
        if (Group* group = tank->GetGroup())
            group->Disband(true);

    // Second guild sweep, while the party is still in world. Provisioning undoes
    // the join stock playerbots makes at LOGIN; this catches anything that guilded
    // a member later in the run (a guild strategy/task acting mid-run), so the
    // guarantee is about the state a character comes OUT with, not just the moment
    // after it logged in. No-op in the normal case.
    if (_realChars)
        for (Slot const& slot : _slots)
            if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
                UndoUnwantedGuild(bot, slot);

    // Alive and out of the dungeon before anything logs the party out — a dead
    // member reaching LogoutPlayer is saved as a ghost at the instance graveyard.
    ReviveAndSendHome();

    Player* gm = FindGm();
    LogoutBots(gm);

    // Shed the saves the run just created so the characters go back clean (and
    // the instance can reset) — the mirror of the pre-teleport unbind, and
    // unconditional for the same reason: a bind left behind here is a bind the
    // next run has to teleport around. Guid-keyed, so it works after the logout.
    // Note this does NOT give back the AccountInstancesPerHour slot the entry
    // consumed; that is time-based. A run killed by a worldserver restart never
    // reaches this at all, which is exactly why the pre-teleport sweep cannot
    // rely on it.
    UnbindFromMap();

    _record.endedAtMs = NowUnixMs();
    _record.durationS = static_cast<uint32>((_record.endedAtMs - _record.startedAtMs) / 1000);
    DcTestRunRecord::Append(_record);

    LOG_INFO("playerbots.dungeonclear", "TESTRUN END {} result={} reason={} bosses={}/{} duration={}s",
             _record.runId, _record.result, _record.failReason,
             _record.bossesKilled, _record.bossesTotal, _record.durationS);

    if (gm)
        ChatHandler(gm->GetSession()).SendSysMessage(Acore::StringFormat(
            "Test run {}: {} ({}/{} bosses, {}s){}", _record.dungeon, _record.result,
            _record.bossesKilled, _record.bossesTotal, _record.durationS,
            _record.failReason.empty() ? "" : (" — " + _record.failReason)));

    _done = true;  // manager erases this job (and releases its reservations) next tick
}

void DcTestRunJob::OnRunDisabled(std::string const& reason)
{
    if (_stage.load() != Stage::Monitoring)
        return;

    std::lock_guard<std::mutex> lock(_obsMutex);
    if (_disableFired)
        return;  // first reason wins
    _disableFired = true;
    _disableReason = reason;
    _disableAllCleared = reason == DcActionShared::kReasonAllCleared;
}

void DcTestRunJob::OnStatusPayload(std::string const& payload)
{
    Stage const stage = _stage.load();
    if (stage != Stage::Monitoring && stage != Stage::Starting)
        return;

    // Payload: STATUS \t enabled \t bossEntry \t bossName \t stall \t skipped
    //          \t state \t detail \t pullSetting \t pullDecision
    std::vector<std::string> parts;
    std::size_t from = 0;
    while (from <= payload.size())
    {
        std::size_t const tab = payload.find('\t', from);
        if (tab == std::string::npos)
        {
            parts.push_back(payload.substr(from));
            break;
        }
        parts.push_back(payload.substr(from, tab - from));
        from = tab + 1;
    }
    if (parts.size() < 8 || parts[0] != "STATUS")
        return;

    std::lock_guard<std::mutex> lock(_obsMutex);
    if (parts[6] == _lastStatusState)
        return;
    _lastStatusState = parts[6];
    _record.statusTimeline.push_back({_totalMs / 1000, parts[6], parts[7]});
}

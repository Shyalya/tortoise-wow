/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTDRIVER_H
#define _PLAYERBOT_DCTESTDRIVER_H

#include <string>

#include "ObjectGuid.h"

class Player;

// Headless GM stand-in for `.dc test` issued from the console / dashboard.
//
// The whole test harness leans on an issuing GM Player*: the party bots log in
// under the GM's PlayerbotMgr and account, keep the GM as playerbots MASTER
// (a real-player-or-self-bot master gates the stock fast path — react delay,
// AoE avoidance; the S1062 fix. Stock expresses this as the master being
// IsRealPlayer(master) || IsSelfBot(master); pre-PR-2592 it was the removed
// PlayerbotAI::HasRealPlayerMaster()), and FindGm() liveness-gates every
// stage. So a
// console start needs a real Player object, not a code path around one.
//
// This provides it: a dedicated character (config
// DungeonClear.TestRun.DriverCharacter, on its own bot account) logged in
// headlessly through the playerbots fake-session machinery
// (sRandomPlayerbotMgr.AddPlayerBot(guid, 0) — the masterless login path),
// then made to pass for a real player master:
//   * its own PlayerbotAI is SELF-MASTERED (SetMaster(driver)), so for every
//     party bot the master resolves as IsSelfBot(driver) == true (pre-PR-2592:
//     masterBotAI->IsRealPlayer(), master == bot) — the react-throttle fast
//     path holds exactly as with a human GM;
//   * it gets its own PlayerbotMgr (PlayerbotsMgr::AddPlayerbotData(p, false);
//     the AI map and mgr map are separate, so one character can hold both),
//     which is what AddPlayerBot/LogoutPlayerBot run through;
//   * its AI is neutralized (stay + passive, GM mode on) so the character
//     never wanders, fights, or draws aggro while parked.
//
// The character and its account are created on first use
// (DungeonClear.TestRun.DriverAccount, default "dcdriver") — a console-only
// harness that cannot run until someone logs in with a game client to make a
// level-1 character is a harness nobody can use out of the box. Set the
// account to "" to opt out and provide the character yourself.
//
// The driver's account must NOT be in AiPlayerbot.RandomBotAccounts (the
// random-bot rotation would manage/log it out) and must not be an addclass
// pool account (pool chars get claimed as party slots) — a random-bot account
// is refused rather than used.
//
// All world-thread only.
namespace DcTestDriver
{
    // The driver, once online and initialized; nullptr otherwise. Callers use
    // this as the issuing GM for Start().
    Player* Get();

    // Why the driver isn't usable yet — callers that can wait (the plan
    // scheduler) need to tell "the login is in flight, retry shortly" apart
    // from "this will never come up", which retrying can't fix.
    enum class Readiness
    {
        Ready,         // Get() is usable now
        PendingLogin,  // headless login issued/in flight — retry shortly
        Unavailable,   // no such character / config empty — retrying won't help
    };

    // Kick off the headless login if the driver isn't online yet, and report
    // which of the three states we're in. *why is set for the two non-Ready
    // cases.
    Readiness Ensure(std::string* why);

    // Ensure() for callers that need the driver right now (`.dc test start`,
    // which provisions immediately): true when Get() is usable, false with
    // *whyPending set otherwise.
    bool EnsureOnline(std::string* whyPending);

    // Poll the async login and run the one-time initialization (mgr +
    // self-master + neutralize) once the character is in world. Cheap no-op
    // when idle or already initialized; call every world tick.
    void Tick();
}

#endif  // _PLAYERBOT_DCTESTDRIVER_H

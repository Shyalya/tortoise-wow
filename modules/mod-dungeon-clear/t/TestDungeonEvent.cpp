/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonEventExecutor.h"

namespace
{
    DungeonEventProgress Prog(uint32 eventId, uint32 stepIndex = 0, uint32 stepStartMs = 0)
    {
        DungeonEventProgress p;
        p.eventId = eventId;
        p.stepIndex = stepIndex;
        p.stepStartMs = stepStartMs;
        // Drive stamps progressMs on activation and on every high-water step
        // advance; baseline it to the step start so the forward-progress watchdog
        // isn't spuriously charged for time before the scenario under test began.
        p.maxStepIndex = stepIndex;
        p.progressMs = stepStartMs;
        return p;
    }
}

// --- EventBuilder ---------------------------------------------------------

TEST(EventBuilderTest, BuildsTypedStepsInOrder)
{
    DungeonEvent e = EventBuilder(5, 9, "x")
                         .Anchored(7)
                         .MoveTo(1.0f, 2.0f, 3.0f, 7.0f)
                         .UseGO(100, 12.0f)
                         .WaitForGOState(100, 1, 8000)
                         .Build();

    ASSERT_EQ(e.steps.size(), 3u);
    EXPECT_EQ(e.mapId, 5u);
    EXPECT_EQ(e.id, 9u);
    EXPECT_EQ(e.orderIndex, 7u);
    EXPECT_TRUE(e.required);
    EXPECT_EQ(e.activation, EventActivation::Anchored);

    EXPECT_EQ(e.steps[0].kind, EventStepKind::MoveTo);
    EXPECT_FLOAT_EQ(e.steps[0].radius, 7.0f);
    EXPECT_FLOAT_EQ(e.steps[0].x, 1.0f);

    EXPECT_EQ(e.steps[1].kind, EventStepKind::UseGameObject);
    EXPECT_EQ(e.steps[1].goEntry, 100u);
    EXPECT_FLOAT_EQ(e.steps[1].radius, 12.0f);

    EXPECT_EQ(e.steps[2].kind, EventStepKind::WaitForGameObjectState);
    EXPECT_EQ(e.steps[2].wantState, 1u);
    EXPECT_EQ(e.steps[2].timeoutMs, 8000u);
}

TEST(EventBuilderTest, JumpStepCarriesLandingAndRadius)
{
    // A drop-down event: walk onto the lip, then jump the off-mesh gap onto the
    // landing shelf (Wailing Caverns → Lord Serpentis).
    DungeonEvent e = EventBuilder(43, 1, "Drop to Lord Serpentis")
                         .Anchored(3)
                         .MoveTo(-100.0f, 10.0f, -20.0f, 3.0f)
                         .Jump(-120.0f, -24.0f, -28.0f, 5.0f)
                         .Build();

    ASSERT_EQ(e.steps.size(), 2u);
    EXPECT_EQ(e.steps[0].kind, EventStepKind::MoveTo);

    EventStep const& j = e.steps[1];
    EXPECT_EQ(j.kind, EventStepKind::Jump);
    EXPECT_FLOAT_EQ(j.x, -120.0f);
    EXPECT_FLOAT_EQ(j.y, -24.0f);
    EXPECT_FLOAT_EQ(j.z, -28.0f);
    EXPECT_FLOAT_EQ(j.radius, 5.0f);
}

TEST(EventBuilderTest, JumpStepDefaultRadius)
{
    DungeonEvent e = EventBuilder(43, 2, "j").Jump(1.0f, 2.0f, 3.0f).Build();
    ASSERT_EQ(e.steps.size(), 1u);
    EXPECT_EQ(e.steps[0].kind, EventStepKind::Jump);
    EXPECT_FLOAT_EQ(e.steps[0].radius, 4.0f);
}

TEST(EventBuilderTest, OptionalAndConditionalFlags)
{
    DungeonEvent e = EventBuilder(1, 1, "c").Conditional([](Player*, AiObjectContext*) { return false; }).Wait(500).Optional().Build();
    EXPECT_EQ(e.activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(e.condition));
    EXPECT_FALSE(e.required);
    ASSERT_EQ(e.steps.size(), 1u);
    EXPECT_EQ(e.steps[0].kind, EventStepKind::Wait);
    EXPECT_EQ(e.steps[0].durationMs, 500u);
}

TEST(EventBuilderTest, EscortCreatureCarriesEscorteeAndCompletionGate)
{
    DungeonEvent e = EventBuilder(43, 2, "Escort the Disciple of Naralex")
                         .EscortCreature(/*escortee*/ 3678, /*startGossipOption*/ 0,
                                         /*doneEntry*/ 3654, /*doneBit*/ 7)
                         .Build();
    ASSERT_EQ(e.steps.size(), 1u);
    EventStep const& s = e.steps[0];
    EXPECT_EQ(s.kind, EventStepKind::EscortCreature);
    EXPECT_EQ(s.creatureEntry, 3678u);   // escortee
    EXPECT_EQ(s.gossipOption, 0);        // start option
    EXPECT_EQ(s.escortDoneEntry, 3654u); // Mutanus
    EXPECT_EQ(s.escortDoneBit, 7);       // his DungeonEncounter bit
    // Defaults applied.
    EXPECT_FLOAT_EQ(s.escortStandoff, 5.0f);
    EXPECT_FLOAT_EQ(s.escortThreatRadius, 18.0f);
    EXPECT_FLOAT_EQ(s.zBand, 20.0f);
    EXPECT_FLOAT_EQ(s.radius, 80.0f);
}

// --- DungeonEventExecutor::Advance (pure) ---------------------------------

TEST(DungeonEventAdvance, EmptyEventCompletes)
{
    DungeonEvent ev = EventBuilder(0, 1, "e").Build();
    DungeonEventProgress p = Prog(1);
    EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Running, 1000, 30000),
              EventDriveOutcome::Completed);
}

TEST(DungeonEventAdvance, DoneWalksThroughStepsThenCompletes)
{
    DungeonEvent ev = EventBuilder(0, 1, "e").Wait(1000).Wait(1000).Build();
    DungeonEventProgress p = Prog(1, 0, 0);

    // First step Done -> still running, cursor advanced and step clock re-stamped.
    EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Done, 500, 30000),
              EventDriveOutcome::Running);
    EXPECT_EQ(p.stepIndex, 1u);
    EXPECT_EQ(p.stepStartMs, 500u);

    // Last step Done -> whole event complete.
    EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Done, 800, 30000),
              EventDriveOutcome::Completed);
    EXPECT_EQ(p.stepIndex, 2u);
}

TEST(DungeonEventAdvance, RunningHoldsBeforeTimeout)
{
    DungeonEvent ev = EventBuilder(0, 1, "e").WaitForSpawn(5, true, 5000).Build();
    DungeonEventProgress p = Prog(1, 0, 1000);
    EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Running, 3000, 30000),
              EventDriveOutcome::Running);  // elapsed 2000 < 5000
    EXPECT_EQ(p.stepIndex, 0u);
}

TEST(DungeonEventAdvance, RunningTimesOutRequiredStallsOptionalSkips)
{
    DungeonEvent req = EventBuilder(0, 1, "e").WaitForSpawn(5, true, 5000).Build();
    DungeonEventProgress p1 = Prog(1, 0, 1000);
    EXPECT_EQ(DungeonEventExecutor::Advance(req, p1, StepResult::Running, 7000, 30000),
              EventDriveOutcome::Stalled);  // elapsed 6000 >= 5000

    DungeonEvent opt = EventBuilder(0, 1, "e").WaitForSpawn(5, true, 5000).Optional().Build();
    DungeonEventProgress p2 = Prog(1, 0, 1000);
    EXPECT_EQ(DungeonEventExecutor::Advance(opt, p2, StepResult::Running, 7000, 30000),
              EventDriveOutcome::Skipped);
}

TEST(DungeonEventAdvance, DefaultTimeoutUsedWhenStepHasNone)
{
    DungeonEvent ev = EventBuilder(0, 1, "e").Wait(1000).Build();  // step timeoutMs 0
    DungeonEventProgress p = Prog(1, 0, 0);
    EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Running, 2000, 3000),
              EventDriveOutcome::Running);  // 2000 < default 3000
    EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Running, 4000, 3000),
              EventDriveOutcome::Stalled);  // 4000 >= default 3000
}

TEST(DungeonEventAdvance, EscortCreatureNeverTimesOut)
{
    // The escort's own dead-air watchdog owns liveness; Advance must NOT escalate
    // a long-Running EscortCreature step to Failed even far past any timeout (else
    // it would fire during the Disciple's 32.5s banish / the long ritual hold).
    DungeonEvent ev = EventBuilder(43, 2, "e")
                          .EscortCreature(3678, 0, 3654, 7)
                          .Build();
    DungeonEventProgress p = Prog(2, 0, 0);
    EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Running, 10u * 60u * 1000u, 30000),
              EventDriveOutcome::Running);
    EXPECT_EQ(p.stepIndex, 0u);  // still on the escort step
}

TEST(DungeonEventAdvance, RewindLoopReDoningStepZeroIsCaughtByForwardProgress)
{
    // Regression: Steamvault's "Open the Main Chambers Door" wedged for a whole
    // run with no stall and no log. The stale-gap rewind fired every tick (the
    // tank's out-of-combat AI tick is slower than the old flat 1s threshold), so
    // each Drive re-ran the already-satisfied leading MoveTo, which reports Done
    // and re-stamps stepStartMs. The per-step timeout could therefore never
    // accumulate, the UseGO step was never reached, and the event returned Running
    // forever. The high-water step index is the clock the rewind can't forge.
    DungeonEvent ev = EventBuilder(545, 1, "Open the Main Chambers Door")
                          .MoveTo(1.0f, 2.0f, 3.0f, 6.0f)
                          .UseGO(184126, 14.0f)
                          .Wait(6000)
                          .Build();

    DungeonEventProgress p = Prog(1, 0, 0);
    uint32 now = 0;

    // Simulate the loop: rewind to step 0, MoveTo reports Done, step -> 1, repeat.
    // The first Done is genuine forward progress (0 -> 1) and stamps progressMs;
    // every later one is not.
    for (int i = 0; i < 100; ++i)
    {
        now += 2000;  // a tick slower than the old 1s gap threshold
        p.stepIndex = 0;
        p.stepStartMs = now;  // what the rewind used to do
        EventDriveOutcome const out =
            DungeonEventExecutor::Advance(ev, p, StepResult::Done, now, 30000);
        if (out == EventDriveOutcome::Stalled)
            return;  // escalated as it should
    }
    FAIL() << "rewind loop never escalated: the event would run Running forever";
}

TEST(DungeonEventAdvance, ForwardProgressWatchdogDoesNotFireWhileStepsAdvance)
{
    // The watchdog must only catch a wedge. An event whose steps keep advancing —
    // even slowly, well past a single step timeout in total — is healthy.
    DungeonEvent ev = EventBuilder(545, 2, "e")
                          .MoveTo(0.0f, 0.0f, 0.0f, 5.0f)
                          .MoveTo(1.0f, 0.0f, 0.0f, 5.0f)
                          .MoveTo(2.0f, 0.0f, 0.0f, 5.0f)
                          .Wait(1000)
                          .Build();
    DungeonEventProgress p = Prog(2, 0, 0);

    uint32 now = 0;
    for (int i = 0; i < 3; ++i)
    {
        now += 25000;  // each step takes most of, but not all of, its timeout
        EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Done, now, 30000),
                  EventDriveOutcome::Running);
    }
    now += 25000;
    EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Done, now, 30000),
              EventDriveOutcome::Completed);
}

TEST(DungeonEventAdvance, ForwardProgressWatchdogSpareEscortCreature)
{
    // EscortCreature owns its own liveness (see EscortCreatureNeverTimesOut); the
    // forward-progress watchdog must respect that exemption too, or a long escort
    // that legitimately sits on one step would be failed at 3x the step timeout.
    DungeonEvent ev = EventBuilder(43, 3, "e")
                          .EscortCreature(3678, 0, 3654, 7)
                          .Build();
    DungeonEventProgress p = Prog(3, 0, 0);
    EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Running, 10u * 60u * 1000u, 30000),
              EventDriveOutcome::Running);
}

TEST(DungeonEventAdvance, BlockedAlwaysStallsEvenWhenOptional)
{
    // Blocked means "needs the human", distinct from Failed — an optional event
    // does NOT silently skip a hard block.
    DungeonEvent ev = EventBuilder(0, 1, "e").Custom(1).Optional().Build();
    DungeonEventProgress p = Prog(1);
    EXPECT_EQ(DungeonEventExecutor::Advance(ev, p, StepResult::Blocked, 0, 30000),
              EventDriveOutcome::Stalled);
}

TEST(DungeonEventAdvance, FailedRequiredStallsOptionalSkips)
{
    DungeonEvent req = EventBuilder(0, 1, "e").Wait(1).Build();
    DungeonEventProgress p1 = Prog(1);
    EXPECT_EQ(DungeonEventExecutor::Advance(req, p1, StepResult::Failed, 0, 30000),
              EventDriveOutcome::Stalled);

    DungeonEvent opt = EventBuilder(0, 1, "e").Wait(1).Optional().Build();
    DungeonEventProgress p2 = Prog(1);
    EXPECT_EQ(DungeonEventExecutor::Advance(opt, p2, StepResult::Failed, 0, 30000),
              EventDriveOutcome::Skipped);
}

// --- DungeonEventRegistry (shipped table) ---------------------------------

TEST(DungeonEventRegistryTest, FindAndHasEvents)
{
    EXPECT_NE(DungeonEventRegistry::Find(109, 1), nullptr);  // Sunken Temple forcefield
    EXPECT_NE(DungeonEventRegistry::Find(209, 1), nullptr);  // ZulFarrak summit
    EXPECT_NE(DungeonEventRegistry::Find(209, 2), nullptr);  // ZulFarrak Gahz'rilla
    EXPECT_NE(DungeonEventRegistry::Find(230, 1), nullptr);  // BRD Ring of Law
    EXPECT_NE(DungeonEventRegistry::Find(109, 11), nullptr);  // ST Atal'alarion
    EXPECT_EQ(DungeonEventRegistry::Find(109, 99), nullptr);  // no such event
    EXPECT_EQ(DungeonEventRegistry::Find(0, 1), nullptr);    // no such map
    EXPECT_EQ(DungeonEventRegistry::Find(109, 0), nullptr);  // id 0 is "none"

    EXPECT_TRUE(DungeonEventRegistry::HasEvents(109));
    EXPECT_TRUE(DungeonEventRegistry::HasEvents(209));
    EXPECT_FALSE(DungeonEventRegistry::HasEvents(34));  // Stockades — none
}

// Sunken Temple GATE 1: the forcefield is dropped via SIX Anchored ring-anchor
// events (ids 1/12/13/14/15/16), one per Atal'ai defender, each engage-killing
// exactly ONE defender. Anchored (not conditional) because each defender sits on
// its own balcony only boss-nav's LongRangePathfinder can reach — a raw MoveTo
// walks the tank to the wrong floor-level room. Pairing two per anchor skipped
// the second (far) defender, the reported bug. The kill step has a generous
// per-step timeout (mini-boss walk + fight).
TEST(DungeonEventRegistryTest, SunkenTempleForcefieldRingAnchors)
{
    struct Anchor { uint32 id; uint32 defender; };
    for (Anchor const& a : { Anchor{1, 5717},    // Mijan
                             Anchor{12, 5716},   // Zul'Lor
                             Anchor{13, 5712},   // Zolo
                             Anchor{14, 5713},   // Gasher
                             Anchor{15, 5714},   // Loro
                             Anchor{16, 5715} }) // Hukku
    {
        DungeonEvent const* e = DungeonEventRegistry::Find(109, a.id);
        ASSERT_NE(e, nullptr);
        EXPECT_EQ(e->activation, EventActivation::Anchored);
        EXPECT_TRUE(e->required);
        ASSERT_EQ(e->steps.size(), 1u);
        EXPECT_EQ(e->steps[0].kind, EventStepKind::KillCreature);
        EXPECT_TRUE(e->steps[0].engage);
        EXPECT_EQ(e->steps[0].creatureEntry, a.defender);
        EXPECT_EQ(e->steps[0].timeoutMs, 120000u);
    }
}

// Sunken Temple GATE 2: events 2-7 are the six statue clicks, one Anchored
// (Optional) UseGO each, in entry/click order 148830..148835.
TEST(DungeonEventRegistryTest, SunkenTempleStatueClicks)
{
    uint32 const statues[6] = {148830, 148831, 148832, 148833, 148834, 148835};
    for (uint32 i = 0; i < 6; ++i)
    {
        DungeonEvent const* e = DungeonEventRegistry::Find(109, 2 + i);
        ASSERT_NE(e, nullptr);
        EXPECT_EQ(e->activation, EventActivation::Anchored);
        EXPECT_FALSE(e->required);  // optional pit wing
        ASSERT_EQ(e->steps.size(), 1u);
        EXPECT_EQ(e->steps[0].kind, EventStepKind::UseGameObject);
        EXPECT_EQ(e->steps[0].goEntry, statues[i]);
    }
}

// Sunken Temple GATE 3 + reordered bosses: the summon (8), Avatar (9),
// Weaver & Dreamscythe (10), and Atal'alarion (11) events.
TEST(DungeonEventRegistryTest, SunkenTempleIdolAvatarAndReorderedBosses)
{
    // Event 8 — Awaken the Soulflayer (optional pit-wing beat 1). The encounter
    // starts only by USING the Egg of Hakkar (10465) at the Sanctum centre — the
    // idol click is a QUESTGIVER no-op and a bare spell cast is rejected. The egg
    // summon FAILS unless used from dead centre and the objective arrive radius
    // lets boss-nav park the tank short, so a tight MoveTo precedes the UseItem to
    // walk the tank precisely to centre first (egg-positioning fix 2026-06-15).
    DungeonEvent const* idol = DungeonEventRegistry::Find(109, 8);
    ASSERT_NE(idol, nullptr);
    EXPECT_EQ(idol->activation, EventActivation::Anchored);
    EXPECT_FALSE(idol->required);
    ASSERT_EQ(idol->steps.size(), 2u);
    EXPECT_EQ(idol->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_EQ(idol->steps[1].kind, EventStepKind::UseItem);
    EXPECT_EQ(idol->steps[1].itemId, 10465u);

    // Event 9 — the Avatar fight: Persistent + Optional. The summon cast spawns
    // the Shade of Hakkar, which transforms into the Avatar (8443) ON ITS OWN
    // once the Nightmare Suppressors drive its counter to 25 (~79s). The bots
    // must NOT engage the suppressors (combat stops their counter tick), so the
    // event has NO channeler pre-kill steps — it only waits for the Avatar to
    // manifest, then kills it.
    DungeonEvent const* avatar = DungeonEventRegistry::Find(109, 9);
    ASSERT_NE(avatar, nullptr);
    EXPECT_EQ(avatar->activation, EventActivation::Anchored);
    EXPECT_TRUE(avatar->persistent);
    EXPECT_FALSE(avatar->required);
    ASSERT_EQ(avatar->steps.size(), 2u);
    EXPECT_EQ(avatar->steps[0].kind, EventStepKind::WaitForSpawn);
    EXPECT_EQ(avatar->steps[0].creatureEntry, 8443u);  // Avatar of Hakkar
    EXPECT_GE(avatar->steps[0].timeoutMs, 120000u);    // long enough to manifest
    EXPECT_EQ(avatar->steps[1].kind, EventStepKind::KillCreature);
    EXPECT_EQ(avatar->steps[1].creatureEntry, 8443u);  // Avatar
    EXPECT_TRUE(avatar->steps[1].engage);

    // Event 10 — Weaver & Dreamscythe (required spine, Persistent), both engaged.
    DungeonEvent const* wd = DungeonEventRegistry::Find(109, 10);
    ASSERT_NE(wd, nullptr);
    EXPECT_TRUE(wd->persistent);
    EXPECT_TRUE(wd->required);
    ASSERT_EQ(wd->steps.size(), 2u);
    EXPECT_EQ(wd->steps[0].creatureEntry, 5720u);  // Weaver
    EXPECT_TRUE(wd->steps[0].engage);
    EXPECT_EQ(wd->steps[1].creatureEntry, 5721u);  // Dreamscythe
    EXPECT_TRUE(wd->steps[1].engage);

    // Event 11 — Atal'alarion (optional pit wing, Persistent), engaged.
    DungeonEvent const* atal = DungeonEventRegistry::Find(109, 11);
    ASSERT_NE(atal, nullptr);
    EXPECT_TRUE(atal->persistent);
    EXPECT_FALSE(atal->required);
    ASSERT_EQ(atal->steps.size(), 1u);
    EXPECT_EQ(atal->steps[0].kind, EventStepKind::KillCreature);
    EXPECT_EQ(atal->steps[0].creatureEntry, 8580u);
    EXPECT_TRUE(atal->steps[0].engage);
}

// Event 17 — central-circle PRE-CLEAR (before Jammal'an): a ClearRadius step, a
// position-based area sweep, on a Persistent + Optional objective. Cleared
// before Weaver/Dreamscythe un-phase and circle the floor.
TEST(DungeonEventRegistryTest, SunkenTempleCentralCirclePreClear)
{
    DungeonEvent const* circle = DungeonEventRegistry::Find(109, 17);
    ASSERT_NE(circle, nullptr);
    EXPECT_EQ(circle->activation, EventActivation::Anchored);
    EXPECT_TRUE(circle->persistent);
    EXPECT_FALSE(circle->required);
    ASSERT_EQ(circle->steps.size(), 1u);
    EXPECT_EQ(circle->steps[0].kind, EventStepKind::ClearRadius);
    EXPECT_TRUE(circle->steps[0].engage);          // driving action seeks & fights
    EXPECT_GT(circle->steps[0].radius, 0.0f);
    EXPECT_GT(circle->steps[0].zBand, 0.0f);       // floor band set
}

// The ClearRadius builder packs the centre, radius and floor z-band, and marks
// the step engage-driven (the objective action walks the tank in to fight).
TEST(DungeonEventBuilderTest, ClearRadiusStep)
{
    DungeonEvent e = EventBuilder(1, 1, "e")
                         .ClearRadius(10.0f, 20.0f, 30.0f, 55.0f, /*zBand*/ 18.0f)
                         .Build();
    ASSERT_EQ(e.steps.size(), 1u);
    EXPECT_EQ(e.steps[0].kind, EventStepKind::ClearRadius);
    EXPECT_FLOAT_EQ(e.steps[0].x, 10.0f);
    EXPECT_FLOAT_EQ(e.steps[0].y, 20.0f);
    EXPECT_FLOAT_EQ(e.steps[0].z, 30.0f);
    EXPECT_FLOAT_EQ(e.steps[0].radius, 55.0f);
    EXPECT_FLOAT_EQ(e.steps[0].zBand, 18.0f);
    EXPECT_TRUE(e.steps[0].engage);
}

// ZulFarrak temple (Executioner / Bly's Band) event: a PERSISTENT anchored step
// list that runs the whole pyramid set-piece — kill the executioner, crack a cage
// (UseGO), survive the waves (wait for Sezz'ziz), descend to kill the temple
// bosses (engage steps), gossip Weegli (door) then Bly (fight), and kill Bly.
TEST(DungeonEventRegistryTest, ZulFarrakTempleEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(209, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_TRUE(e->persistent);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 9u);

    // 1. kill the executioner (engage-driven), then crack a cage to start it.
    EXPECT_EQ(e->steps[0].kind, EventStepKind::KillCreature);
    EXPECT_EQ(e->steps[0].creatureEntry, 7274u);
    EXPECT_TRUE(e->steps[0].engage);
    EXPECT_EQ(e->steps[1].kind, EventStepKind::UseGameObject);
    EXPECT_EQ(e->steps[1].goEntry, 141073u);

    // 2. GARRISON the ramp head (MoveTo with a monotonic instance-phase gate):
    //    hold there — re-moving if combat displaced the tank — until DATA_PYRAMID
    //    (0) reaches WAVE_3 (7).
    EXPECT_EQ(e->steps[2].kind, EventStepKind::MoveTo);
    EXPECT_EQ(e->steps[2].instanceDataId, 0);   // DATA_PYRAMID
    EXPECT_EQ(e->steps[2].instanceDataMin, 7u);  // PYRAMID_WAVE_3

    // 3. descend and kill the two temple bosses (engage-driven).
    EXPECT_EQ(e->steps[3].kind, EventStepKind::KillCreature);
    EXPECT_EQ(e->steps[3].creatureEntry, 7796u);  // Nekrum
    EXPECT_TRUE(e->steps[3].engage);
    EXPECT_EQ(e->steps[4].kind, EventStepKind::KillCreature);
    EXPECT_EQ(e->steps[4].creatureEntry, 7275u);  // Sezz'ziz
    EXPECT_TRUE(e->steps[4].engage);

    // 4. goblin FIRST (opens the door), then a short dwell before provoking Bly.
    //    Both gossips skip if the NPC is dead so a lost helper can't deadlock, and
    //    wait for the crew to finish walking down before talking.
    EXPECT_EQ(e->steps[5].kind, EventStepKind::Gossip);
    EXPECT_EQ(e->steps[5].creatureEntry, 7607u);  // Weegli
    EXPECT_TRUE(e->steps[5].skipIfMissing);
    EXPECT_TRUE(e->steps[5].waitForStill);
    EXPECT_EQ(e->steps[6].kind, EventStepKind::Wait);
    EXPECT_GT(e->steps[6].durationMs, 0u);

    // 5. human starts the fight; killing Bly ends the event.
    EXPECT_EQ(e->steps[7].kind, EventStepKind::Gossip);
    EXPECT_EQ(e->steps[7].creatureEntry, 7604u);  // Bly
    EXPECT_TRUE(e->steps[7].skipIfMissing);
    EXPECT_TRUE(e->steps[7].waitForStill);
    EXPECT_EQ(e->steps[8].kind, EventStepKind::KillCreature);
    EXPECT_EQ(e->steps[8].creatureEntry, 7604u);
    EXPECT_TRUE(e->steps[8].engage);
}

// ZulFarrak Sacred Pool (Gahz'rilla) event (map 209, id 2): a PERSISTENT anchored
// step list ordered LAST (anchor at encounterIndex 8). Ring the gong (UseGO 141832)
// to summon Gahz'rilla, WAIT for it to emerge, then engage and kill it. The
// WaitForSpawn between the ring and the kill is essential — without it the kill
// step would read "no live boss" before the summon and false-complete.
TEST(DungeonEventRegistryTest, ZulFarrakGahzrillaEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(209, 2);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_TRUE(e->persistent);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 3u);

    // 1. ring the gong -> summons Gahz'rilla (go->Use cheats the lock, no mallet).
    EXPECT_EQ(e->steps[0].kind, EventStepKind::UseGameObject);
    EXPECT_EQ(e->steps[0].goEntry, 141832u);

    // 2. hold until the summoned boss has materialised.
    EXPECT_EQ(e->steps[1].kind, EventStepKind::WaitForSpawn);
    EXPECT_EQ(e->steps[1].creatureEntry, 7273u);
    EXPECT_TRUE(e->steps[1].wantAlive);

    // 3. engage and kill it (the pool boss does not auto-aggro).
    EXPECT_EQ(e->steps[2].kind, EventStepKind::KillCreature);
    EXPECT_EQ(e->steps[2].creatureEntry, 7273u);
    EXPECT_TRUE(e->steps[2].engage);
}

// Blackrock Depths Ring of Law (map 230): a PERSISTENT anchored event that walks
// the tank onto the centre trigger, ensures the encounter started (Custom
// fallback), then holds dead-centre until TYPE_RING_OF_LAW (1) reaches DONE (3)
// while the random waves + boss are fought reactively. NOT a ClearRadius/count
// gate (those would false-complete in the empty-floor windows).
TEST(DungeonEventRegistryTest, BlackrockRingOfLawEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(230, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_EQ(e->orderIndex, 3u);  // between Grebmar (2) and Loregrain (4)
    EXPECT_TRUE(e->persistent);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 3u);

    // 1. settle on the arena centre (area trigger 1526 spot).
    EXPECT_EQ(e->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_FLOAT_EQ(e->steps[0].x, 596.432f);
    EXPECT_EQ(e->steps[0].instanceDataId, -1);  // plain MoveTo, no gate

    // 2. ensure the encounter started (Custom -> EnsureRingStarted hook id 1).
    EXPECT_EQ(e->steps[1].kind, EventStepKind::Custom);
    EXPECT_EQ(e->steps[1].hookId, 1u);

    // 3. garrison the centre until TYPE_RING_OF_LAW (1) reaches DONE (3); long
    //    timeout for the boss fight. The garrison re-runs the SAME start hook
    //    while it holds, because the state is not monotonic (npc_grimstone's
    //    no-victim watchdog resets it to NOT_STARTED).
    EXPECT_EQ(e->steps[2].kind, EventStepKind::MoveTo);
    EXPECT_EQ(e->steps[2].instanceDataId, 1);    // TYPE_RING_OF_LAW
    EXPECT_EQ(e->steps[2].instanceDataMin, 3u);  // EncounterState::DONE
    EXPECT_EQ(e->steps[2].timeoutMs, 600000u);
    EXPECT_EQ(e->steps[2].hookId, 1u);           // .WhileHolding(EnsureRingStarted)
    EXPECT_FLOAT_EQ(e->steps[2].radius, e->steps[0].radius)
        << "walk-in and hold must agree on where the centre is";
}

// Blackrock Depths Shadowforge Lock (map 230 event 2): walk to the lever in the
// East Garrison, pull it, then confirm the Giant Doors it drives actually closed.
// ANCHORED because the lever is 113yd from — and a floor above — the doors it
// moves, so only boss-nav can deliver the tank to it.
TEST(DungeonEventRegistryTest, BlackrockShadowforgeLockEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(230, 2);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_EQ(e->orderIndex, 9u);  // between Bael'Gar (8) and Angerforge (9)
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 3u);

    // 1. settle at the lever, on the garrison floor beneath it.
    EXPECT_EQ(e->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_FLOAT_EQ(e->steps[0].x, 615.61f);
    EXPECT_FLOAT_EQ(e->steps[0].y, -49.78f);
    EXPECT_EQ(e->steps[0].instanceDataId, -1);  // plain MoveTo, no gate

    // 2. pull it. UseGO goes straight through GameObject::Use(), whose DOOR
    //    branch has no lock check — which is how the Shadowforge Key is bypassed.
    EXPECT_EQ(e->steps[1].kind, EventStepKind::UseGameObject);
    EXPECT_EQ(e->steps[1].goEntry, 161460u);  // The Shadowforge Lock

    // 3. gate on the DOORS, not on the lever: the lever flipping only proves the
    //    click landed, GO_STATE_READY (1) on 157923 proves its SmartAI chain ran.
    EXPECT_EQ(e->steps[2].kind, EventStepKind::WaitForGameObjectState);
    EXPECT_EQ(e->steps[2].goEntry, 157923u);  // Giant Doors
    EXPECT_EQ(e->steps[2].wantState, 1u);     // GO_STATE_READY == shut
    EXPECT_GT(e->steps[2].radius, 113.0f)
        << "the doors are 113yd from the lever — the scan must reach them";
}

// Deadmines Defias Cannon: walk to the cannon, fire it (Custom hook 2 casts the
// gunpowder spell at GO 16398), then hold until the Iron Clad Door (16397) opens.
TEST(DungeonEventRegistryTest, DeadminesCannonEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(36, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_EQ(e->orderIndex, 3u);  // between Gilnid (2) and Mr. Smite (3)
    EXPECT_TRUE(e->persistent);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 3u);

    // 1. step onto the cannon.
    EXPECT_EQ(e->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_FLOAT_EQ(e->steps[0].x, -107.56f);

    // 2. fire it (Custom -> FireDefiasCannon hook id 2).
    EXPECT_EQ(e->steps[1].kind, EventStepKind::Custom);
    EXPECT_EQ(e->steps[1].hookId, 2u);

    // 3. hold until the Iron Clad Door opens (GO_STATE_ACTIVE_ALTERNATIVE).
    EXPECT_EQ(e->steps[2].kind, EventStepKind::WaitForGameObjectState);
    EXPECT_EQ(e->steps[2].goEntry, 16397u);
    EXPECT_EQ(e->steps[2].wantState, 2u);
    EXPECT_EQ(e->steps[2].timeoutMs, 30000u);
}

// Wailing Caverns drop to Lord Serpentis: settle on the lip, then jump the
// off-mesh gap onto Serpentis's shelf. Persistent (the drop is one-way; a rewind
// would walk back to the now-unreachable lip).
TEST(DungeonEventRegistryTest, WailingCavernsSerpentisDropEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(43, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_EQ(e->orderIndex, 5u);  // shared with Serpentis (bit 5)
    EXPECT_TRUE(e->persistent);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 2u);

    // 1. settle on the jump lip.
    EXPECT_EQ(e->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_FLOAT_EQ(e->steps[0].x, -290.65567f);
    EXPECT_FLOAT_EQ(e->steps[0].radius, 3.0f);

    // 2. leap onto the landing shelf.
    EXPECT_EQ(e->steps[1].kind, EventStepKind::Jump);
    EXPECT_FLOAT_EQ(e->steps[1].x, -285.45773f);
    EXPECT_FLOAT_EQ(e->steps[1].y, 4.021016f);
    EXPECT_FLOAT_EQ(e->steps[1].z, -63.919395f);
    EXPECT_FLOAT_EQ(e->steps[1].radius, 5.0f);
}

// Wailing Caverns return-fall (event 3): off Verdan's shelf down the narrow hole
// to the lower caverns. Settle on the lip, then DropInHole — glide over the open
// shaft mouth and MoveFall pure-vertical into the water. Persistent (the drop is
// one-way; a rewind would walk back to the now-unreachable lip).
TEST(DungeonEventRegistryTest, WailingCavernsReturnFallEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(43, 3);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_EQ(e->orderIndex, 7u);  // after Verdan (6), before the escort (also 7)
    EXPECT_TRUE(e->persistent);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 2u);

    // 1. settle on the lip (so stepIndex reaches 1, the persistence sticky).
    EXPECT_EQ(e->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_FLOAT_EQ(e->steps[0].x, -55.89f);
    EXPECT_FLOAT_EQ(e->steps[0].radius, 4.0f);

    // 2. drop: x/y/z is the over-hole nudge target; landX/Y/Z the deep-floor
    //    landing the leader falls onto and the followers teleport to.
    EventStep const& drop = e->steps[1];
    EXPECT_EQ(drop.kind, EventStepKind::DropInHole);
    EXPECT_FLOAT_EQ(drop.x, -49.5f);
    EXPECT_FLOAT_EQ(drop.y, 47.6f);
    EXPECT_FLOAT_EQ(drop.z, -29.0f);
    EXPECT_FLOAT_EQ(drop.landX, -49.5f);
    EXPECT_FLOAT_EQ(drop.landY, 47.6f);
    EXPECT_FLOAT_EQ(drop.landZ, -105.83f);
}

// The Slave Pens (547) post-Mennu drop: a single TeleportParty step that
// relocates the whole party across a one-way navmesh break (ramp ledge -> lower
// level). Anchored on the ledge objective, NOT persistent (the teleport is one
// synchronous tick, idempotent on restart). x/y/z is the ledge checkpoint;
// landX/Y/Z the lower landing the party is teleported to.
TEST(DungeonEventRegistryTest, SlavePensDropDownEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(547, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_EQ(e->orderIndex, 1u);  // between Mennu (bit 0) and Rokmar (bit 1)
    EXPECT_FALSE(e->persistent);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 1u);
    EventStep const& tp = e->steps[0];
    EXPECT_EQ(tp.kind, EventStepKind::TeleportParty);
    EXPECT_FLOAT_EQ(tp.x, -186.52f);
    EXPECT_FLOAT_EQ(tp.y, -412.79f);
    EXPECT_FLOAT_EQ(tp.z, 55.32f);
    EXPECT_FLOAT_EQ(tp.landX, -209.02f);
    EXPECT_FLOAT_EQ(tp.landY, -384.32f);
    EXPECT_FLOAT_EQ(tp.landZ, 8.53f);
    // Gate radius is generous so the objective arrival always satisfies it (no
    // mid-ramp teleport). Must comfortably exceed the objective's 6yd arrive
    // radius.
    EXPECT_GT(tp.radius, 6.0f);

    // Anchored, so it is not in the map's conditional set.
    EXPECT_TRUE(DungeonEventRegistry::Conditional(547).empty());
}

// The Underbog (546) post-Ghaz'an drop: a TWO-hop TeleportParty with a 10s pause
// between, relocating the party down a tiered navmesh break (upper ledge -> mid
// landing -> lower landing). Anchored on the ledge objective and PERSISTENT (the
// first hop moves the leader far from the anchor, so the at-objective trigger
// must stay sticky for the Wait + second hop to run).
TEST(DungeonEventRegistryTest, UnderbogDropDownEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(546, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_EQ(e->orderIndex, 2u);  // between Ghaz'an (bit 1) and Swamplord (bit 2)
    EXPECT_TRUE(e->persistent);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 3u);

    // Hop 1: upper ledge -> mid landing.
    EventStep const& hop1 = e->steps[0];
    EXPECT_EQ(hop1.kind, EventStepKind::TeleportParty);
    EXPECT_FLOAT_EQ(hop1.x, 274.72f);
    EXPECT_FLOAT_EQ(hop1.y, -462.60f);
    EXPECT_FLOAT_EQ(hop1.z, 81.37f);
    EXPECT_FLOAT_EQ(hop1.landX, 333.63f);
    EXPECT_FLOAT_EQ(hop1.landY, -471.46f);
    EXPECT_FLOAT_EQ(hop1.landZ, 52.10f);
    EXPECT_GT(hop1.radius, 6.0f);  // comfortably exceeds the 6yd objective arrive

    // Pause between hops.
    EventStep const& wait = e->steps[1];
    EXPECT_EQ(wait.kind, EventStepKind::Wait);
    EXPECT_EQ(wait.durationMs, 10000u);

    // Hop 2: mid landing (the prior hop's landing, now the checkpoint) -> lower.
    EventStep const& hop2 = e->steps[2];
    EXPECT_EQ(hop2.kind, EventStepKind::TeleportParty);
    EXPECT_FLOAT_EQ(hop2.x, 333.63f);
    EXPECT_FLOAT_EQ(hop2.y, -471.46f);
    EXPECT_FLOAT_EQ(hop2.z, 52.10f);
    EXPECT_FLOAT_EQ(hop2.landX, 355.71f);
    EXPECT_FLOAT_EQ(hop2.landY, -471.68f);
    EXPECT_FLOAT_EQ(hop2.landZ, 24.32f);

    // Anchored, so it is not itself in the map's conditional set — which is not
    // empty: 546 also carries the conditional "Send Ghaz'an up to his platform".
    for (DungeonEvent const* c : DungeonEventRegistry::Conditional(546))
        EXPECT_NE(c->id, 1u) << "the drop-down chain must stay Anchored";
}

// Stratholme (329) dead-side "Baron run": the persistent Slaughterhouse chain
// (eventId 4), anchored at Ramstein's DBC bit 11 (after the ziggurats + Barthilas,
// before Baron 12). Abominations+Ramstein (one ClearRadius) -> wait+clear undead
// wave (ClearRadius) -> wait+kill guard wave (KillCreatureEngage, since the
// guards post far off and a bot-centred ClearRadius can't see them) -> gate on
// the Baron door opening. Slaughter progress isn't exposed via GetData, so the
// phase barriers are the monotonic doors (combat-gap proof), not transient
// creature checks.
TEST(DungeonEventRegistryTest, StratholmeSlaughterhouseEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(329, 4);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_EQ(e->orderIndex, 11u);  // Ramstein's bit, between Barthilas (10) and Baron (12)
    EXPECT_TRUE(e->persistent);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 9u);

    // 1. clear the hall of the pre-spawned abominations (bulk).
    EXPECT_EQ(e->steps[0].kind, EventStepKind::ClearRadius);
    EXPECT_TRUE(e->steps[0].engage);
    EXPECT_FLOAT_EQ(e->steps[0].x, 4032.0f);
    EXPECT_FLOAT_EQ(e->steps[0].y, -3415.0f);

    // 1b/1c. actively seek any straggler abomination the centre clear couldn't
    //    reach (Bile Spewer 10416 / Venom Belcher 10417) — a bot-centred,
    //    reachability-filtered ClearRadius can leave far ones, and Ramstein spawns
    //    only when EVERY abomination dies (issue #5).
    EXPECT_EQ(e->steps[1].kind, EventStepKind::KillCreature);
    EXPECT_TRUE(e->steps[1].engage);
    EXPECT_EQ(e->steps[1].creatureEntry, 10416u);
    EXPECT_EQ(e->steps[2].kind, EventStepKind::KillCreature);
    EXPECT_TRUE(e->steps[2].engage);
    EXPECT_EQ(e->steps[2].creatureEntry, 10417u);

    // 1d. all abominations dead -> seek + kill the summoned Ramstein (10439).
    EXPECT_EQ(e->steps[3].kind, EventStepKind::KillCreature);
    EXPECT_TRUE(e->steps[3].engage);
    EXPECT_EQ(e->steps[3].creatureEntry, 10439u);

    // 2. wave 1: wait for the mindless undead, then ClearRadius them.
    EXPECT_EQ(e->steps[4].kind, EventStepKind::WaitForSpawn);
    EXPECT_EQ(e->steps[4].creatureEntry, 11030u);
    EXPECT_TRUE(e->steps[4].wantAlive);
    EXPECT_EQ(e->steps[5].kind, EventStepKind::ClearRadius);

    // 3. wave 2: wait for the black guards, then actively seek+kill them
    //    (KillCreatureEngage) — they post far off, so a bot-centred ClearRadius
    //    can't see them.
    EXPECT_EQ(e->steps[6].kind, EventStepKind::WaitForSpawn);
    EXPECT_EQ(e->steps[6].creatureEntry, 10394u);
    EXPECT_EQ(e->steps[7].kind, EventStepKind::KillCreature);
    EXPECT_EQ(e->steps[7].creatureEntry, 10394u);

    // 4. monotonic completion gate: the Baron door (175796) opens when the guards
    //    die. GO_STATE_ACTIVE (0) = open.
    EXPECT_EQ(e->steps[8].kind, EventStepKind::WaitForGameObjectState);
    EXPECT_EQ(e->steps[8].goEntry, 175796u);
    EXPECT_EQ(e->steps[8].wantState, 0u);
    EXPECT_GT(e->steps[8].radius, 100.0f);  // reaches the door from across the hall
}

// Stratholme (329) live side: Grand Crusader Dathrohan -> Balnazzar (eventId 5),
// anchored at the Dathrohan objective (bit 6, after Galford). Balnazzar (10813)
// has no spawn — he is Dathrohan (10812) after an UpdateEntry at 40% HP — so two
// KillCreatureEngage steps: pull 10812 (Done when he transforms away), then hold
// on 10813 until he's dead. Non-persistent: both steps are idempotent kill-gates.
TEST(DungeonEventRegistryTest, StratholmeDathrohanBalnazzarEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(329, 5);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_EQ(e->orderIndex, 6u);  // Balnazzar's DBC bit, right after Galford (5)
    EXPECT_FALSE(e->persistent);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 2u);

    // 1. seek + engage Dathrohan; Done once he UpdateEntry's away (no live 10812).
    EXPECT_EQ(e->steps[0].kind, EventStepKind::KillCreature);
    EXPECT_TRUE(e->steps[0].engage);
    EXPECT_EQ(e->steps[0].creatureEntry, 10812u);

    // 2. finish the transformed Balnazzar; Done when 10813 is dead.
    EXPECT_EQ(e->steps[1].kind, EventStepKind::KillCreature);
    EXPECT_TRUE(e->steps[1].engage);
    EXPECT_EQ(e->steps[1].creatureEntry, 10813u);
}

// The three ziggurat acolyte clears (eventIds 1/2/3) are conditional events
// (conditions 5/6/7) that fire when a ziggurat door is open but the chamber not
// yet cleared, each a single ClearRadius of its acolyte chamber.
TEST(DungeonEventConditional, StratholmeZigguratAcolyteEvents)
{
    std::vector<DungeonEvent const*> str = DungeonEventRegistry::Conditional(329);
    // the 3 ziggurats + Timmy's pack pre-clear (id 6); the Slaughterhouse is anchored
    ASSERT_EQ(str.size(), 4u);
    for (DungeonEvent const* e : str)
    {
        EXPECT_EQ(e->activation, EventActivation::Conditional);
        EXPECT_TRUE(e->required);
        ASSERT_EQ(e->steps.size(), 1u);
        EXPECT_EQ(e->steps[0].kind, EventStepKind::ClearRadius);
        EXPECT_TRUE(e->steps[0].engage);
    }

    // event id N maps to condition N+4 (1->5, 2->6, 3->7).
    for (uint32 id : {1u, 2u, 3u})
    {
        DungeonEvent const* e = DungeonEventRegistry::Find(329, id);
        ASSERT_NE(e, nullptr);
        EXPECT_TRUE(static_cast<bool>(e->condition));
    }

    // Each acolyte clear sorts in the panel just before the next anchor (so it
    // renders right after the boss whose door it follows, not dumped at the end):
    // zig1 -> before Nerub'enkan, zig2 -> before Maleki, zig3 -> before the
    // Slaughterhouse objective.
    EXPECT_EQ(DungeonEventRegistry::Find(329, 1)->panelGatesBossEntry, 10437u);
    EXPECT_EQ(DungeonEventRegistry::Find(329, 2)->panelGatesBossEntry, 10438u);
    EXPECT_EQ(DungeonEventRegistry::Find(329, 3)->panelGatesBossEntry,
              BossRosterRegistry::ObjectiveEntry(1));

    // Timmy's pre-clear (id 6) is conditional too and sorts in the panel just
    // before Timmy himself (10808).
    DungeonEvent const* timmy = DungeonEventRegistry::Find(329, 6);
    ASSERT_NE(timmy, nullptr);
    EXPECT_EQ(timmy->activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(timmy->condition));
    EXPECT_EQ(timmy->panelGatesBossEntry, 10808u);

    // These are NOT room-aggro pre-clears (ClearRadius, not KillCreature(0)).
    EXPECT_FALSE(DungeonEventRegistry::HasRoomAggroEvent(329));
}

// Uldaman (70): the Ironaya seal — a conditional event that clears the
// antechamber, uses the keystone, then waits for the Seal of Khaz'Mul to open.
TEST(DungeonEventConditional, UldamanIronayaSeal)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(70, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(e->condition));
    EXPECT_TRUE(e->required);
    EXPECT_FALSE(e->repeatable);

    ASSERT_EQ(e->steps.size(), 4u);
    EXPECT_EQ(e->steps[0].kind, EventStepKind::ClearRadius);
    EXPECT_TRUE(e->steps[0].engage);
    EXPECT_EQ(e->steps[1].kind, EventStepKind::MoveTo);
    EXPECT_EQ(e->steps[2].kind, EventStepKind::UseGameObject);
    EXPECT_EQ(e->steps[2].goEntry, 124371u);            // the Keystone
    EXPECT_EQ(e->steps[3].kind, EventStepKind::WaitForGameObjectState);
    EXPECT_EQ(e->steps[3].goEntry, 124372u);            // the Seal of Khaz'Mul
    EXPECT_EQ(e->steps[3].wantState, 0u);               // GO_STATE_ACTIVE (open)

    // Uldaman has ONE conditional event (the Ironaya seal — its antechamber has
    // live Stonevault trash whose ClearRadius seek walks the tank in). The Altar
    // of the Keepers and Altar of Archaedas are ANCHORED events on roster
    // objectives instead (the halls are dormant immune statues with nothing to
    // seek, so a conditional gate can't navigate the tank in). NO room-aggro
    // pre-clear (the seal is a multi-step ClearRadius, not the lone
    // KillCreature(0) shape).
    EXPECT_EQ(DungeonEventRegistry::Conditional(70).size(), 1u);
    EXPECT_FALSE(DungeonEventRegistry::IsRoomAggroPreClear(*e));
    EXPECT_FALSE(DungeonEventRegistry::HasRoomAggroEvent(70));
}

// Hellfire Ramparts (543): the "Approach Vazruden" event — a CONDITIONAL +
// REPEATABLE single MoveTo that walks the tank onto the lower platform between
// the two Hellfire Sentries, whose deaths fly the summoned final boss down.
// Same summon-boss shape as RFD's "Approach Tuten'kash": the real boss (17537)
// is a TempSummon invisible to the live-boss spawn-store scan, so the repeatable
// MoveTo holds the tank on the trigger until aggro lands. Folded under Vazruden
// in the panel.
TEST(DungeonEventConditional, HellfireRampartsApproachVazruden)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(543, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(e->condition));
    EXPECT_TRUE(e->repeatable);
    EXPECT_EQ(e->panelGatesBossEntry, 17537u);  // folded under Vazruden

    ASSERT_EQ(e->steps.size(), 1u);
    EXPECT_EQ(e->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_FLOAT_EQ(e->steps[0].x, -1378.0f);
    EXPECT_FLOAT_EQ(e->steps[0].y, 1718.0f);
    EXPECT_FLOAT_EQ(e->steps[0].radius, 6.0f);

    // It is the map's only conditional event, and not a room-aggro pre-clear.
    EXPECT_EQ(DungeonEventRegistry::Conditional(543).size(), 1u);
    EXPECT_FALSE(DungeonEventRegistry::IsRoomAggroPreClear(*e));
}

// Blood Furnace (542): Broggok's cell-door event — two CONDITIONAL + REPEATABLE
// events. Event 1 (condition 17) Use()s the "Cell Door Lever" once boss-nav has
// parked the tank by it, starting the four cell waves; event 2 (condition 18)
// holds the tank at the lever spot through the waves until the rear gate opens
// and Broggok becomes attackable. Both folded under Broggok in the panel.
TEST(DungeonEventConditional, BloodFurnaceBroggokCellDoor)
{
    DungeonEvent const* lever = DungeonEventRegistry::Find(542, 1);
    ASSERT_NE(lever, nullptr);
    EXPECT_EQ(lever->activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(lever->condition));
    EXPECT_TRUE(lever->repeatable);
    EXPECT_EQ(lever->panelGatesBossEntry, 17380u);   // folded under Broggok

    ASSERT_EQ(lever->steps.size(), 1u);
    EXPECT_EQ(lever->steps[0].kind, EventStepKind::UseGameObject);
    EXPECT_EQ(lever->steps[0].goEntry, 181982u);     // Cell Door Lever
    EXPECT_FLOAT_EQ(lever->steps[0].radius, 35.0f);

    DungeonEvent const* hold = DungeonEventRegistry::Find(542, 2);
    ASSERT_NE(hold, nullptr);
    EXPECT_EQ(hold->activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(hold->condition));
    EXPECT_TRUE(hold->repeatable);
    EXPECT_EQ(hold->panelGatesBossEntry, 17380u);    // folded under Broggok

    ASSERT_EQ(hold->steps.size(), 1u);
    EXPECT_EQ(hold->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_FLOAT_EQ(hold->steps[0].x, 456.56f);
    EXPECT_FLOAT_EQ(hold->steps[0].y, 54.35f);
    EXPECT_FLOAT_EQ(hold->steps[0].radius, 6.0f);

    // Two conditional events on the map, neither a room-aggro pre-clear.
    EXPECT_EQ(DungeonEventRegistry::Conditional(542).size(), 2u);
    EXPECT_FALSE(DungeonEventRegistry::IsRoomAggroPreClear(*lever));
    EXPECT_FALSE(DungeonEventRegistry::IsRoomAggroPreClear(*hold));
}

// Uldaman (70): the Altar of the Keepers — an ANCHORED event on roster objective
// OBJ(1). Boss-nav delivers the tank into the hall; the event clears the live
// trash (Stewards / Earthen), centres on the altar, fires the SEND_EVENT to
// awaken the 4 stoned keepers, kills them, then waits for the temple door.
// Persistent so the multi-keeper fight can't rewind it.
TEST(DungeonEventAnchored, UldamanStoneKeepers)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(70, 2);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_TRUE(e->required);
    EXPECT_TRUE(e->persistent);

    ASSERT_EQ(e->steps.size(), 5u);
    EXPECT_EQ(e->steps[0].kind, EventStepKind::ClearRadius);  // clear live hall trash
    EXPECT_TRUE(e->steps[0].engage);
    EXPECT_EQ(e->steps[1].kind, EventStepKind::MoveTo);
    EXPECT_EQ(e->steps[2].kind, EventStepKind::CastSpell);
    EXPECT_EQ(e->steps[2].spellId, 11568u);              // Altar of The Keepers SEND_EVENT
    EXPECT_EQ(e->steps[3].kind, EventStepKind::KillCreature);
    EXPECT_EQ(e->steps[3].creatureEntry, 4857u);         // Stone Keeper
    EXPECT_FALSE(e->steps[3].engage);                    // plain gate (party auto-aggros)
    EXPECT_EQ(e->steps[4].kind, EventStepKind::WaitForGameObjectState);
    EXPECT_EQ(e->steps[4].goEntry, 124367u);             // temple door
    EXPECT_EQ(e->steps[4].wantState, 0u);                // GO_STATE_ACTIVE (open)
}

// Uldaman (70): the Altar of Archaedas — an ANCHORED event on roster objective
// OBJ(2). Boss-nav delivers the tank onto the altar, this fires its SEND_EVENT to
// wake the stoned final boss, and the boss pull then kills him.
TEST(DungeonEventAnchored, UldamanArchaedasAltar)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(70, 3);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_TRUE(e->required);

    ASSERT_EQ(e->steps.size(), 2u);
    EXPECT_EQ(e->steps[0].kind, EventStepKind::MoveTo);
    EXPECT_EQ(e->steps[1].kind, EventStepKind::CastSpell);
    EXPECT_EQ(e->steps[1].spellId, 10340u);             // Altar of Archaedas SEND_EVENT
}

// Garrison MoveTo (MoveToHoldUntilSpawn): a MoveTo step carrying a spawn-gate
// creature, so the executor holds at the point until that creature is up.
TEST(DungeonEventBuilderTest, MoveToHoldUntilSpawn)
{
    DungeonEvent e = EventBuilder(1, 1, "e")
                         .MoveTo(1.0f, 2.0f, 3.0f, 5.0f)
                         .MoveToHoldUntilSpawn(4.0f, 5.0f, 6.0f, 8.0f, /*until*/ 7275)
                         .Build();
    ASSERT_EQ(e.steps.size(), 2u);
    EXPECT_EQ(e.steps[0].kind, EventStepKind::MoveTo);
    EXPECT_EQ(e.steps[0].creatureEntry, 0u);  // plain MoveTo, no gate
    EXPECT_EQ(e.steps[1].kind, EventStepKind::MoveTo);
    EXPECT_EQ(e.steps[1].creatureEntry, 7275u);  // garrison gate
    EXPECT_TRUE(e.steps[1].wantAlive);
    EXPECT_EQ(e.steps[1].instanceDataId, -1);  // no instance gate on the spawn variant
}

// Garrison MoveTo, instance-data variant: a MoveTo carrying a monotonic phase
// gate (GetData(id) >= min), preferred for content the party kills mid-combat.
TEST(DungeonEventBuilderTest, MoveToHoldUntilInstanceData)
{
    DungeonEvent e = EventBuilder(1, 1, "e")
                         .MoveToHoldUntilInstanceData(1.0f, 2.0f, 3.0f, 10.0f,
                                                      /*dataId*/ 0, /*min*/ 7)
                         .Build();
    ASSERT_EQ(e.steps.size(), 1u);
    EXPECT_EQ(e.steps[0].kind, EventStepKind::MoveTo);
    EXPECT_EQ(e.steps[0].instanceDataId, 0);
    EXPECT_EQ(e.steps[0].instanceDataMin, 7u);
    EXPECT_EQ(e.steps[0].creatureEntry, 0u);  // instance gate, not a creature gate
}

// The builder's KillCreatureEngage marks the engage flag (vs plain KillCreature
// which only gates), and Timeout() tunes the last-added step's timeout.
TEST(DungeonEventBuilderTest, KillCreatureEngageAndTimeout)
{
    DungeonEvent e = EventBuilder(1, 1, "e")
                         .KillCreature(100)
                         .KillCreatureEngage(200)
                         .WaitForSpawn(300, true).Timeout(900000)
                         .Build();
    ASSERT_EQ(e.steps.size(), 3u);
    EXPECT_FALSE(e.steps[0].engage);
    EXPECT_TRUE(e.steps[1].engage);
    EXPECT_EQ(e.steps[1].creatureEntry, 200u);
    EXPECT_EQ(e.steps[2].timeoutMs, 900000u);
}

// SkipIfTargetMissing / WaitTargetStill flag the last-added step's gossip bits.
TEST(DungeonEventBuilderTest, SkipIfTargetMissing)
{
    DungeonEvent e = EventBuilder(1, 1, "e")
                         .Gossip(100, 0)
                         .Gossip(200, 0).SkipIfTargetMissing().WaitTargetStill()
                         .Build();
    ASSERT_EQ(e.steps.size(), 2u);
    EXPECT_FALSE(e.steps[0].skipIfMissing);
    EXPECT_FALSE(e.steps[0].waitForStill);
    EXPECT_TRUE(e.steps[1].skipIfMissing);
    EXPECT_TRUE(e.steps[1].waitForStill);
}

// --- Milestone 2: conditional activation ----------------------------------

// Conditional() returns only EventActivation::Conditional events for the map.
// Sunken Temple (109) is anchored-only (forcefield ring anchors + statues etc.);
// Shadowfang Keep (33) has the conditional courtyard-door event; ZulFarrak (209)
// has exactly one conditional (the Zum'rah wake-up) alongside its two anchored.
TEST(DungeonEventConditional, ConditionalListFiltersByActivation)
{
    EXPECT_TRUE(DungeonEventRegistry::Conditional(109).empty());  // anchored only

    std::vector<DungeonEvent const*> zf = DungeonEventRegistry::Conditional(209);
    ASSERT_EQ(zf.size(), 1u);
    EXPECT_EQ(zf[0]->id, 3u);  // Wake Witch Doctor Zum'rah

    // SFK has the two faction-specific courtyard events (Alliance + Horde) plus
    // the Sorcerer's Gate voidwalker sweep.
    std::vector<DungeonEvent const*> sfk = DungeonEventRegistry::Conditional(33);
    ASSERT_EQ(sfk.size(), 3u);
    EXPECT_EQ(sfk[0]->id, 1u);
    EXPECT_TRUE(static_cast<bool>(sfk[0]->condition));  // Alliance
    EXPECT_EQ(sfk[1]->id, 2u);
    EXPECT_TRUE(static_cast<bool>(sfk[1]->condition));  // Horde
    EXPECT_EQ(sfk[2]->id, 3u);
    EXPECT_TRUE(static_cast<bool>(sfk[2]->condition));  // Arugal's Voidwalkers
    for (DungeonEvent const* e : sfk)
        EXPECT_EQ(e->activation, EventActivation::Conditional);
}

// Each SFK courtyard event: walk to the faction's cell lever, pull it (UseGO) to
// open the prison gate, gossip the freed prisoner (option 0), then wait for the
// Courtyard Door (18895) to open. Optional so a non-firing script degrades to the
// normal door-blocked stall instead of livelocking. Alliance frees Ashcrombe via
// lever 18901; Horde frees Adamant via lever 18900.
TEST(DungeonEventConditional, ShadowfangCourtyardEventShape)
{
    struct Faction { uint32 id; uint32 lever; uint32 prisoner; };
    for (Faction const& f : { Faction{1, 18901, 3850}, Faction{2, 18900, 3849} })
    {
        DungeonEvent const* e = DungeonEventRegistry::Find(33, f.id);
        ASSERT_NE(e, nullptr);
        EXPECT_EQ(e->activation, EventActivation::Conditional);
        EXPECT_FALSE(e->required);
        ASSERT_EQ(e->steps.size(), 5u);

        EXPECT_EQ(e->steps[0].kind, EventStepKind::MoveTo);  // to the lever

        EXPECT_EQ(e->steps[1].kind, EventStepKind::UseGameObject);
        EXPECT_EQ(e->steps[1].goEntry, f.lever);

        EXPECT_EQ(e->steps[2].kind, EventStepKind::Gossip);
        EXPECT_EQ(e->steps[2].creatureEntry, f.prisoner);
        EXPECT_EQ(e->steps[2].gossipOption, 0);

        EXPECT_EQ(e->steps[3].kind, EventStepKind::MoveTo);  // to the courtyard door

        EXPECT_EQ(e->steps[4].kind, EventStepKind::WaitForGameObjectState);
        EXPECT_EQ(e->steps[4].goEntry, 18895u);
        EXPECT_EQ(e->steps[4].wantState, 0u);  // GO_STATE_ACTIVE (open)
    }
}

// The Sorcerer's Gate (18972) is opened by 'Arugal's Voidwalker - On Just Died -
// Set GO State', not by the party. The gate wears an empty lock 85, so before
// this event the door-blocked action rated itself entitled and clicked it open
// inside the ~6s window between Fenrus dying and Arugal summoning the adds — the
// party walked out, the voidwalkers spawned behind it, and the run wedged.
//
// The shape that fixes it, and why each piece is load-bearing:
//   - an ARRIVAL step first, anchored on the summon ring, so the party is inside
//     the adds' 40yd Attack-Start radius when they appear (and so the event
//     can't false-latch "done" from across the map — the Stratholme #5 lint);
//   - a WaitForSpawn gate BEFORE the sweep, or the sweep certifies the empty
//     6s-window room clear and latches having killed nothing;
//   - the sweep entry-filtered to the voidwalkers, with a by-entry
//     KillCreatureEngage backstop for anything the position sweep can't see;
//   - Persistent, because the sweep is a real fight and a combat gap would
//     otherwise rewind a non-persistent event to step 0;
//   - Optional, so a wipe that burned the adds' 60s out-of-combat despawn
//     degrades to the door-blocked pause (which auto-resumes on open) instead
//     of a hard event stall.
TEST(DungeonEventConditional, ShadowfangSorcererGateVoidwalkerEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(33, 3);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(e->condition));
    EXPECT_TRUE(e->persistent);
    EXPECT_FALSE(e->required);

    ASSERT_EQ(e->steps.size(), 5u);

    // 1. Arrival on the summon ring — never straight to the gate.
    EXPECT_EQ(e->steps[0].kind, EventStepKind::MoveTo);

    // 2. Wait for the adds to exist before judging the room.
    EXPECT_EQ(e->steps[1].kind, EventStepKind::WaitForSpawn);
    EXPECT_EQ(e->steps[1].creatureEntry, 4627u);  // Arugal's Voidwalker

    // 3. Sweep the ring, voidwalkers only.
    EXPECT_EQ(e->steps[2].kind, EventStepKind::ClearRadius);
    ASSERT_EQ(e->steps[2].entryFilter.size(), 1u);
    EXPECT_EQ(e->steps[2].entryFilter[0], 4627u);
    // The clear verdict is only trusted from within DC_EVENT_CLEAR_JUDGE_RADIUS
    // (12) of the centre, so the arrival radius must land the tank inside it.
    EXPECT_LE(e->steps[0].radius, 12.0f);

    // 4. By-entry backstop for anything the position sweep couldn't resolve.
    EXPECT_EQ(e->steps[3].kind, EventStepKind::KillCreature);
    EXPECT_TRUE(e->steps[3].engage);
    EXPECT_EQ(e->steps[3].creatureEntry, 4627u);

    // 5. Confirm the gate the voidwalkers' death opens.
    EXPECT_EQ(e->steps[4].kind, EventStepKind::WaitForGameObjectState);
    EXPECT_EQ(e->steps[4].goEntry, 18972u);  // Sorcerer's Gate
    EXPECT_EQ(e->steps[4].wantState, 0u);    // GO_STATE_ACTIVE (open)
}

// The synthetic latch key is pure, injective, and lives in a high range that
// can't collide with real creature/anchor entries.
TEST(DungeonEventConditional, ConditionalLatchKeyIsHighAndInjective)
{
    EXPECT_EQ(DungeonEventExecutor::ConditionalLatchKey(1),
              DungeonEventExecutor::ConditionalLatchKey(1));
    EXPECT_NE(DungeonEventExecutor::ConditionalLatchKey(1),
              DungeonEventExecutor::ConditionalLatchKey(2));
    EXPECT_GT(DungeonEventExecutor::ConditionalLatchKey(1), 1000000u);
}

// --- Milestone 3: room-aggro pre-clear -----------------------------------

// The SM Cathedral (189) room-aggro pre-clear is a Conditional gate (condition 3)
// with a single KillCreature step in "room-trash mode" (creatureEntry 0). It is
// required (hold the boss pull until the room is clear).
TEST(DungeonEventRoomAggro, ScarletCathedralEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(189, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(e->condition));
    EXPECT_TRUE(e->required);
    ASSERT_EQ(e->steps.size(), 1u);
    EXPECT_EQ(e->steps[0].kind, EventStepKind::KillCreature);
    EXPECT_EQ(e->steps[0].creatureEntry, 0u);  // room-trash mode
}

// Scholomance (289) re-uses the same room-aggro pre-clear shape for the merged
// Marduk & Vectus boss: Conditional(3) + a lone KillCreature(0) room-trash step.
TEST(DungeonEventRoomAggro, ScholomanceMardukVectusEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(289, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(e->condition));
    EXPECT_TRUE(e->required);
    ASSERT_EQ(e->steps.size(), 1u);
    EXPECT_EQ(e->steps[0].kind, EventStepKind::KillCreature);
    EXPECT_EQ(e->steps[0].creatureEntry, 0u);  // room-trash mode

    EXPECT_TRUE(DungeonEventRegistry::IsRoomAggroPreClear(*e));
    EXPECT_TRUE(DungeonEventRegistry::HasRoomAggroEvent(289));
}

// IsRoomAggroPreClear distinguishes the room-trash gate from the step-driven
// SFK gossip events and from anchored objectives — only the lone-KillCreature(0)
// Conditional shape qualifies, so HasRoomAggroEvent flags only those maps.
TEST(DungeonEventRoomAggro, PredicateAndHasRoomAggroEvent)
{
    DungeonEvent const* cath = DungeonEventRegistry::Find(189, 1);
    DungeonEvent const* sfk = DungeonEventRegistry::Find(33, 1);   // gossip event
    DungeonEvent const* st = DungeonEventRegistry::Find(109, 1);   // forcefield anchor
    ASSERT_NE(cath, nullptr);
    ASSERT_NE(sfk, nullptr);
    ASSERT_NE(st, nullptr);

    EXPECT_TRUE(DungeonEventRegistry::IsRoomAggroPreClear(*cath));
    EXPECT_FALSE(DungeonEventRegistry::IsRoomAggroPreClear(*sfk));
    // ST's forcefield is an Anchored ring-anchor event, not the lone
    // Conditional KillCreature(0) room-trash shape.
    EXPECT_FALSE(DungeonEventRegistry::IsRoomAggroPreClear(*st));

    EXPECT_TRUE(DungeonEventRegistry::HasRoomAggroEvent(189));
    EXPECT_FALSE(DungeonEventRegistry::HasRoomAggroEvent(33));   // gossip only
    EXPECT_FALSE(DungeonEventRegistry::HasRoomAggroEvent(109));  // anchored only
    EXPECT_FALSE(DungeonEventRegistry::HasRoomAggroEvent(0));

    // A non-Conditional KillCreature(0) (e.g. a hypothetical anchored row) is NOT
    // a room-aggro pre-clear — the activation guard matters.
    DungeonEvent anchored = EventBuilder(1, 1, "x").Anchored(0).KillCreature(0).Build();
    EXPECT_FALSE(DungeonEventRegistry::IsRoomAggroPreClear(anchored));
}

// --- Dire Maul West (map 429) --------------------------------------------

// Immol'thar's five Crystal Generator pylons (events 4-8) are Anchored UseGO +
// Wait objectives — Persistent (a combat gap at a guarded pylon must not rewind
// and re-click the spent BUTTON) and Optional (a misfire degrades to standing at
// the still-shielded boss). Each clicks one generator GO; the instance flips the
// pylon bit and, at all five, makes Immol'thar attackable.
TEST(DungeonEventAnchored, DireMaulWestPylonEventShape)
{
    struct Pylon { uint32 eventId; uint32 goEntry; };
    for (Pylon const& p : { Pylon{4, 177259}, Pylon{5, 177257}, Pylon{6, 177258},
                            Pylon{7, 179504}, Pylon{8, 179505} })
    {
        DungeonEvent const* e = DungeonEventRegistry::Find(429, p.eventId);
        ASSERT_NE(e, nullptr) << "missing pylon event " << p.eventId;
        EXPECT_EQ(e->activation, EventActivation::Anchored);
        EXPECT_TRUE(e->persistent);
        EXPECT_FALSE(e->required);  // Optional
        // Uldaman keeper pattern: clear the guards, close to the crystal, click,
        // wait. ClearRadius first (kill guards/treants), then MoveTo (reach the
        // dais), then UseGO (click), then Wait (activation delay).
        ASSERT_EQ(e->steps.size(), 4u);
        EXPECT_EQ(e->steps[0].kind, EventStepKind::ClearRadius);
        EXPECT_GT(e->steps[0].radius, 15.0f);      // covers guards + blink hop
        EXPECT_GT(e->steps[0].timeoutMs, 30000u);  // generous for a caster pack
        EXPECT_EQ(e->steps[1].kind, EventStepKind::MoveTo);
        EXPECT_EQ(e->steps[2].kind, EventStepKind::UseGameObject);
        EXPECT_EQ(e->steps[2].goEntry, p.goEntry);
        EXPECT_EQ(e->steps[3].kind, EventStepKind::Wait);
        EXPECT_GT(e->steps[3].durationMs, 0u);
    }
}

// The Warpwood entrance is swept by two small ClearRadius-only Anchored
// waypoints (events 429/11 west, 429/12 east), ordered first. Small radius (so
// the tank fights the closing pack in place, not chasing far); no UseGO.
TEST(DungeonEventAnchored, DireMaulWestEntranceSweepShape)
{
    for (uint32 id : {11u, 12u})
    {
        DungeonEvent const* e = DungeonEventRegistry::Find(429, id);
        ASSERT_NE(e, nullptr) << "missing entrance sweep event " << id;
        EXPECT_EQ(e->activation, EventActivation::Anchored);
        EXPECT_TRUE(e->persistent);
        EXPECT_FALSE(e->required);  // Optional
        ASSERT_EQ(e->steps.size(), 1u);
        EXPECT_EQ(e->steps[0].kind, EventStepKind::ClearRadius);
        EXPECT_GT(e->steps[0].radius, 30.0f);
        EXPECT_LT(e->steps[0].radius, 60.0f);      // small -> fight in place
        EXPECT_GT(e->steps[0].timeoutMs, 30000u);
    }
}

// The two Crescent Key doors (events 9/10, conditions 14/15) are on-path
// Conditional door events — the same UseGO + WaitForGameObjectState shape as the
// Gordok doors, so they preempt the door-blocked stall. GameObject::Use on a DOOR
// ignores the lock, so no Crescent Key is needed.
TEST(DungeonEventConditional, DireMaulWestCrescentDoorEventShape)
{
    struct Door { uint32 eventId; uint32 goEntry; };
    for (Door const& d : { Door{9, 177221}, Door{10, 179550} })
    {
        DungeonEvent const* e = DungeonEventRegistry::Find(429, d.eventId);
        ASSERT_NE(e, nullptr) << "missing crescent door event " << d.eventId;
        EXPECT_EQ(e->activation, EventActivation::Conditional);
        EXPECT_TRUE(static_cast<bool>(e->condition));
        EXPECT_FALSE(e->required);  // Optional
        ASSERT_EQ(e->steps.size(), 2u);
        EXPECT_EQ(e->steps[0].kind, EventStepKind::UseGameObject);
        EXPECT_EQ(e->steps[0].goEntry, d.goEntry);
        EXPECT_EQ(e->steps[1].kind, EventStepKind::WaitForGameObjectState);
        EXPECT_EQ(e->steps[1].goEntry, d.goEntry);
        EXPECT_EQ(e->steps[1].wantState, 0u);  // GO_STATE_ACTIVE (open)

    }
}

// Wailing Caverns FINALE: the Disciple of Naralex escort (event 2). Anchored +
// Persistent (it spans 3 ambushes + the final-chamber waves + Mutanus — every
// combat is a >1s gap that would rewind a non-persistent event). Step 0 is a
// short MoveTo so the persistent stepIndex reaches 1 (the at-objective sticky);
// step 1 is the escort proper, completing STRICTLY on Mutanus (3654, bit 7) —
// never on "reached the end".
TEST(DungeonEventRegistryTest, WailingCavernsDiscipleEscort)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(43, 2);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Anchored);
    EXPECT_TRUE(e->persistent);
    EXPECT_TRUE(e->required);
    ASSERT_EQ(e->steps.size(), 2u);

    EXPECT_EQ(e->steps[0].kind, EventStepKind::MoveTo);  // close to the Disciple

    EventStep const& esc = e->steps[1];
    EXPECT_EQ(esc.kind, EventStepKind::EscortCreature);
    EXPECT_EQ(esc.creatureEntry, 3678u);   // Disciple of Naralex
    EXPECT_EQ(esc.gossipOption, 0);        // "Let the event begin!"
    EXPECT_EQ(esc.escortDoneEntry, 3654u); // Mutanus the Devourer
    EXPECT_EQ(esc.escortDoneBit, 7);       // his DungeonEncounter bit
}

// ZulFarrak Zum'rah wake-up (map 209, id 3). He spawns FRIENDLY (faction 35) and
// only turns hostile when someone crosses area trigger 962 — a client packet no
// bot sends for a non-teleport trigger, so an all-bot party deadlocks on a live
// but unattackable boss. CONDITIONAL (his state, not a travel anchor, decides) +
// REPEATABLE (a wipe restores faction 35, and a one-shot latch would leave the
// retry deadlocked). A single Custom step (hook 5) sets the faction the trigger's
// SmartAI row would have set; forging the packet was tried first and did not work
// live, so no MoveTo-onto-the-trigger step remains.
TEST(DungeonEventConditional, ZulFarrakZumrahWakeEventShape)
{
    DungeonEvent const* e = DungeonEventRegistry::Find(209, 3);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(e->condition));
    EXPECT_TRUE(e->repeatable);
    EXPECT_EQ(e->panelGatesBossEntry, 7271u);  // folds under Zum'rah in the panel

    ASSERT_EQ(e->steps.size(), 1u);
    EXPECT_EQ(e->steps[0].kind, EventStepKind::Custom);
    EXPECT_EQ(e->steps[0].hookId, 5u);
    EXPECT_TRUE(ObjectiveHookRegistry::Has(e->steps[0].hookId));
}

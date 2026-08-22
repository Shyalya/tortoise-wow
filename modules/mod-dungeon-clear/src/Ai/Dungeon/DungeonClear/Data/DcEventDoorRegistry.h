/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCEVENTDOORREGISTRY_H
#define _PLAYERBOT_DCEVENTDOORREGISTRY_H

#include "Common.h"

// Per-ENTRY list of door gameobjects that are SCRIPT-ONLY: the live client
// refuses a direct player open and ONLY an in-game event opens them, even though
// their template (an empty lock-85, the same template as plenty of plainly
// clickable doors) reads as openable to BotCanOpenDoorLikePlayer / DcDoorPolicy.
// A bot generic-Use()ing one of these toggles the server GO state while the
// client still treats the door as shut — a desync — and it also skips the
// intended event (e.g. Shadowfang Keep's courtyard door, which only opens when a
// freed prisoner walks over and unlocks it).
//
// This is DELIBERATELY keyed by GO ENTRY, not by lock id: lock 85 is shared with
// many doors bots SHOULD open (Deadmines Factory/Foundry/Mast Room, etc.), so a
// lock-level rule would break them. Keep this list to doors verified to be
// script/event-opened only; the door-blocked action consults it before deciding
// it is "entitled" to open a door, and leaves a listed door for the events
// framework or the human instead.
namespace DcEventDoorRegistry
{
    inline bool IsScriptOnly(uint32 goEntry)
    {
        switch (goEntry)
        {
            case 18895:  // Shadowfang Keep — Courtyard Door (freed-prisoner event)
            // Shadowfang Keep's other two gates, both empty-lock-85 like the
            // Courtyard Door and both driven purely by SmartAI:
            //
            //   18972 Sorcerer's Gate (guid 33785) — the Fenrus room's east exit
            //     toward Nandos. It opens on 'Arugal's Voidwalker (4627) - On Just
            //     Died - Set GO State'. The intended sequence is: Fenrus (4274)
            //     dies -> his SmartAI sets data on Archmage Arugal (4275) -> that
            //     runs timed actionlist 427500, which summons the four voidwalkers
            //     6s later -> killing one opens the gate. Force-opening the gate
            //     skipped that whole mechanic: the party walked out ~6s before the
            //     adds existed, then the voidwalkers spawned BEHIND it at the room's
            //     west end and the run wedged between advancing to Nandos and
            //     turning back for them (live report 2026-08-01). The gate has
            //     door.autoCloseTime 0, so a bot click opened it permanently.
            //     The "Arugal's Voidwalkers" event (map 33 id 3) now drives the
            //     real sequence.
            //
            //   18971 Arugal's Lair (guid 33241) — opens on 'Wolf Master Nandos
            //     (3927) - On Just Died - Set GO State'. Nandos stands 2.6yd in
            //     FRONT of it, so the ordinary run kills him and the door opens
            //     itself; a bot that force-opened it could instead walk straight
            //     past him to Archmage Arugal and skip an encounter. The
            //     door-blocked watchdog in DcEngageActions already named this door
            //     as the reason it exists — this is the entry that fixes it.
            case 18971:  // Shadowfang Keep — Arugal's Lair (opens on Nandos' death)
            case 18972:  // Shadowfang Keep — Sorcerer's Gate (voidwalker event)
                return true;
            default:
                return false;
        }
    }

    // Doors NAVIGATION must ignore entirely: never flagged as a corridor
    // blocker, never opened, never a reason to park or auto-pause. These are
    // interact-THROUGH gates — the run's objective is completed from the
    // players' side of the shut door (a gossip through the bars), after which
    // the event script opens the door itself. Flagging one as blocking is
    // always wrong: the route intentionally ends beside it, and the pause
    // machinery would halt a run that needs nothing from the door at all.
    inline bool IsNavigationIgnored(uint32 goEntry)
    {
        switch (goEntry)
        {
            case 184393:  // Old Hillsbrad — Thrall's Prison Door (gossip through
                          // the gate; his script opens it via EVENT_OPEN_DOORS)
                return true;
            // The Steamvault — Main Chambers Access Panels. These are wall
            // CONTROLS, not doors, but their template is GAMEOBJECT_TYPE_DOOR
            // and they spawn (and permanently stay) in GO_STATE_READY, so the
            // closed-door predicate reads each one as a shut gate sitting on
            // the corridor. Clicking one runs go_main_chambers_access_panel's
            // OnGossipHello, which returns true BEFORE GameObject::Use reaches
            // UseDoorOrButton — so the panel's own GOState never flips, and the
            // door-blocked action concluded "clicked it, still closed, can't
            // open" and auto-paused the run 13.8yd from its objective (live run
            // 2026-07-20, tank Fedrel). The panel is opened by nothing and
            // blocks nothing; the Steamvault event (map 545 id 1) clicks it,
            // which is what opens the real Main Chambers Door (183049).
            case 184125:  // Hydromancer Thespia's panel
            case 184126:  // Mekgineer Steamrigger's panel
                return true;
            // Blackrock Depths — the Giant Doors apparatus (map 230). Four
            // GAMEOBJECT_TYPE_DOOR entries make up one machine, and only the
            // lever (161460, key-exempt below) is ever meant to be clicked. The
            // other three are the machine's moving parts: they carry no lock, no
            // ScriptName and no gossip, and their GO state is driven ENTIRELY by
            // the lever's SmartAI (161460 source_type 1: on GO state changed ->
            // SMART_ACTION_ACTIVATE_GOBJECT on guids 15639/15576/15640/15352).
            //
            // Their states are INVERTED with respect to each other, so whichever
            // way the machine stands, one of them is sitting in GO_STATE_READY on
            // the corridor and reads to the closed-door predicate as a shut gate:
            //
            //   doors open  (spawn state) — Giant Doors ACTIVE, Fake Collision +
            //     BigDoorDummyCollision02 READY. The Fake Collision spawns on top
            //     of the Giant Doors at (723.1,-105.9,-71.5) with a 18x21x25yd
            //     model box, so it lands within the blocking-door value's 5yd
            //     corridor band on the lower passage the route to Bael'Gar uses.
            //     This is what ended run tr-20260817-044457-30 at 9/19 bosses:
            //     "blocking-door: flagged ... 'Giant Door Fake Collision' (entry
            //     161462) 78.1yd from bot as corridor-blocking" -> walk-in ->
            //     "can't open ... -> auto-pausing".
            //   doors closed (after the lever) — exactly the reverse, so the
            //     Giant Doors themselves (157923) become the flagged blocker,
            //     on the very state the Shadowforge Lock event works to reach.
            //
            // Neither state is ever something a player solves at the door, and
            // neither obstructs a bot: server-side GameObject collision feeds the
            // dynamic LoS tree only — mmaps carry no gameobjects and the movement
            // splines are not collision-checked — and the navmesh runs straight
            // through the doorway at z ~ -71.5 either way. So the whole apparatus
            // is navigation-invisible; the lever alone drives it.
            case 157923:  // Giant Doors (startOpen=1; closed by the lever)
            case 161461:  // Giant Door Mechanism (the winding wheel, 3.3yd from
                          // the lever — lock-free, so BotCanOpenDoorLikePlayer
                          // refuses it and it would auto-pause the run standing
                          // AT the objective it is part of)
            case 161462:  // Giant Door Fake Collision (open-state collision hull)
            case 161516:  // BigDoorDummyCollision02 (the upper-level portcullis
                          // hull, (702.1,-125.7,-45.7))
                return true;
            // Utgarde Keep (map 574) — the three forge FLAME WALLS. The forge
            // hall is a ring around a central hearth, cut into three sectors by
            // three walls of fire, one per forge. Each is a
            // GAMEOBJECT_TYPE_DOOR: lock 0, startOpen 0, autoCloseTime 0, no
            // ScriptName, no AIName, addon flags 32 (NODESPAWN, and notably no
            // GO_FLAG_LOCKED), spawned in GO_STATE_READY. Their model
            // (Vr_Forgefire_01, display 7503) is not a door panel at all — it is
            // a ~60yd-long, ~2yd-thick, ~37yd-tall slab that runs from the
            // hearth out to the outer wall, so it lies across the ring rather
            // than beside it and the closed-door predicate reads it as a shut
            // gate straddling the corridor.
            //
            // Nothing a player does at the wall opens it. instance_utgarde_keep
            // owns all three GO states: SetData(DATA_FORGE_n, ...) opens that
            // forge's bellows + fire + anvil together, and the only caller is
            // npc_dragonflayer_forge_master — DONE on JustDied, NOT_STARTED on
            // Reset (which shuts the wall again). The master that opens a wall
            // stands in the sector on the PARTY'S side of it, so the wall is
            // never the thing to solve: it is the readout of a fight the run has
            // to walk past it to reach.
            //
            // Reading the ring as a bearing off the hearth at (360.7,-16.5) —
            // the navmesh is an annulus r 16..~62 the whole way round, with the
            // entrance corridor running out at bearing ~195-205 deg and the exit
            // corridor toward Keleseth at ~75-110 deg — the three walls sit at
            // 288.5 / 48.5 / 168.5 deg and each master sits one to a sector:
            //
            //   sector 1  168.5..288.5  entrance corridor, forge master 1 (246 deg)
            //     kill him -> 186692 (288.5 deg) drops -> sector 2
            //   sector 2  288.5.. 48.5  forge master 2 (1 deg)
            //     kill him -> 186693 (48.5 deg) drops -> sector 3
            //   sector 3   48.5..168.5  exit corridor, forge master 3 (122 deg)
            //     kill him -> 186691 (168.5 deg) drops, closing the loop back
            //     onto the entrance corridor
            //
            // so the intended clear is one counter-clockwise lap of the ring,
            // and the map-574 forge objectives are what enforce it. The wall
            // itself enforces nothing on a bot: mmaps carry no gameobjects and
            // movement splines are not collision-checked, so a raised wall has
            // never stopped one.
            //
            // Left unlisted it ends the run outright. Runs tr-20260818-070705-4
            // and -7 flagged "'Doodad_VR_ForgeFire_First' (entry 186692) 98.0yd
            // from bot as corridor-blocking", walked in, reported "can't open
            // ... -> auto-pausing", and died at 0/3 bosses 3m45s in with the
            // tank still 57yd short of forge 1.
            //
            // Nor is the flagged wall reliably the one the corridor crosses.
            // PathLegCrossesDoor tests the DBC GeoBox through ToDoorLocal, the
            // GameObject::IsInRange frame, whose matrix [[sinA,cosA],[cosA,-sinA]]
            // has determinant -1 — it MIRRORS. The server's real collision uses
            // GameObjectModel's Rz(orientation) instead. The two agree on a
            // symmetric door panel and disagree on a slab that sits entirely to
            // one side of its origin: here they place the same wall 120 degrees
            // apart, so per-door geometry tuning cannot be made to work on these
            // three anyway.
            //
            // IsScriptOnly would only stop the click — the wall would still be
            // flagged, still parked at, still auto-paused on. IsSelfClearing
            // would hold instead of pausing, but there is no timer to hold for:
            // the wall opens on a kill, and holding at it starves the very fight
            // that opens it. Navigation-invisible is the only correct answer.
            //
            // This does not blind the run to them. DcEngageGeometry::
            // ClosedDoorBetween rays the REAL collision mesh and does not
            // consult this list, so trash and bosses on the far side of a wall
            // that is still up stay vetoed — the party is never dragged through
            // a raised flame wall by a far-side pack.
            case 186691:  // Doodad_VR_ForgeFire_Third  (opens on forge master 3)
            case 186692:  // Doodad_VR_ForgeFire_First  (opens on forge master 1)
            case 186693:  // Doodad_VR_ForgeFire_Second (opens on forge master 2)
                return true;
            default:
                return false;
        }
    }

    // Doors that shut TEMPORARILY under instance-script control and reopen
    // themselves on a timer. The bot must neither open one (the script owns the
    // GO state) nor auto-pause on one (there is nothing for a player to come
    // and solve) — it holds where it stands and the door frees it.
    //
    // Stratholme's two gate traps are the whole list. instance_stratholme's
    // Update() watches two floor positions — (3612.3,-3335.4) Scarlet side,
    // (3919.9,-3547.3) undead side — and the instant a non-GM player comes
    // within 5.5yd it slams the matching PAIR of portcullises shut, spawns
    // plagued critters on the trapped player 2s later, and reopens both gates
    // 20s after that (EVENT_GATE*_DELAY). The trap then sits on a 30-minute
    // cooldown, so a run meets it at most once per side.
    //
    // Nothing about that shape fits the pause machinery: the gates are
    // lock-free with startOpen=1 (so BotCanOpenDoorLikePlayer already refuses
    // them, and a bare Use() would fight the script's own DoUseDoorOrButton
    // toggle), and they are shut for a bounded 20s. Run tr-20260816-151006-14
    // walked its tank over the Scarlet-side trigger at Crusaders' Square and
    // auto-paused 13.1yd from the portcullis; it burned 36s of a 60s pause
    // budget before the script reopened the gate and the door-reopened trigger
    // resumed it. Holding is the correct behaviour and costs nothing.
    //
    // Deliberately NOT extended to the ziggurat / gauntlet / slaughter gates:
    // those are progress gates the run must EARN (kill the acolytes, finish the
    // gauntlet), not timers, so a hold there would be an infinite one.
    inline bool IsSelfClearing(uint32 goEntry)
    {
        switch (goEntry)
        {
            // --- Stratholme (map 329) — the two rat-trap portcullis pairs ---
            case 175350:  // Doodad_SmallPortcullis04 — gate trap 1, Scarlet side
            case 175351:  // Doodad_SmallPortcullis03 — gate trap 1, Scarlet side
            case 175354:  // Doodad_SmallPortcullis09 — gate trap 2, undead side
            case 175355:  // Doodad_SmallPortcullis08 — gate trap 2, undead side
                return true;
            default:
                return false;
        }
    }

    // Doors whose KEY requirement we deliberately waive: the bot opens them as
    // if it held the key, no item in inventory needed.
    //
    // Scarlet Monastery's Armory (Herod's Door) and Cathedral (Chapel Door)
    // both sit on lock 299 — Scarlet Key (7146) or lockpicking 175. A tank bot
    // carries neither, so an autonomous SM run parked at the wing entrance and
    // auto-paused every time, making those two wings unclearable without a
    // human handing the key over first. The doors are otherwise ordinary
    // traversal gates: no ScriptName, no AIName, no instance-script GO-state
    // control, and nothing behind them the key is meant to gate beyond the
    // wing itself (the key is a convenience item players farm from the
    // Graveyard/Library side, not an encounter lock).
    //
    // Keyed by GO ENTRY, not by lock id, for the same reason as the lists
    // above: a lock id is shared across dungeons (299 covers both the SM wing
    // gates and the Stratholme Scarlet-side doors), so only an entry list can
    // waive one door without waiving another that happens to share its lock.
    //
    // The same argument extends to Dire Maul North, Scholomance and Stratholme
    // (added 2026-08-08): every entry below is a plain traversal gate whose key
    // is a farmed convenience item, not an encounter lock. Each was verified in
    // the world DB before being listed, against the checklist this list demands:
    //
    //   * GAMEOBJECT_TYPE_DOOR with a real lock whose only slots are a key item
    //     and/or lockpicking — never a lock-free script seal (see the
    //     IsLockFreeClickable note for why lock-free is the dangerous shape).
    //   * gameobject_template_addon.flags == 34 (GO_FLAG_LOCKED | NODESPAWN):
    //     no GO_FLAG_NOT_SELECTABLE and no GO_FLAG_INTERACT_COND, so a player
    //     at the keyboard really can click them. (GO_FLAG_LOCKED is exactly
    //     what DcDoorPolicy suppresses bare-hands opening on, which is why
    //     these needed an exemption rather than just working.)
    //   * No ScriptName. Where an AIName exists it is SmartGameObjectAI whose
    //     only action is a gossip-hello SET_INST_DATA recording wing progress —
    //     and GameObject::Use() runs that GossipHello BEFORE the lock check, so
    //     the door-blocked action's Use() drives the identical sequence a keyed
    //     player does. Nothing is skipped or desynced.
    //   * The instance script, where it mentions the door at all, only calls
    //     AllowSaveToDB(true) on it (instance_stratholme / instance_scholomance)
    //     so a player-opened gate persists across a relog. It never reads or
    //     drives the GO state, so no encounter can be desynced by opening one.
    //
    // Deliberately NOT listed: keyed objects that are not doors (Stratholme's
    // postboxes and Scarlet Cannons, Scholomance's Brazier of the Herald), and
    // the script-driven lock-free gates of both dungeons (Scholomance's Kirtonos
    // gate 175570 and the seven Gandling gates, Stratholme's ziggurat doors) —
    // those are instance-script GO-state territory and stay untouched. Same
    // call in Blackrock Depths: the empty-lock-85 doors (170573/170574 Golem
    // Room, 170575 Throne Room, 170576/170577 Tomb of the Seven) and the Bar
    // Door 170571 (lock 739, Grim Guzzler Key) are all cached AND state-driven
    // by instance_blackrock_depths, so they are script territory whatever their
    // lock says; and the Relic Coffer Doors (lock 639, Relic Coffer Key) are the
    // Vault puzzle's loot cells, not a corridor the run has to walk through.
    inline bool IsKeyExempt(uint32 goEntry)
    {
        switch (goEntry)
        {
            case 101854:  // Scarlet Monastery — Herod's Door (Armory, lock 299)
            case 104591:  // Scarlet Monastery — Chapel Door (Cathedral, lock 299)

            // --- Scholomance (map 289) -----------------------------------
            // The only keyed door inside the instance; every other Scholomance
            // door/gate is lock-free (handled by IsLockFreeClickable or by the
            // instance script). Viewing Room Key (13873) drops from Doctor
            // Theolen Krastinov, i.e. from behind a boss the run may not have
            // reached yet, so a keyless party could never open it.
            case 175167:  // Viewing Room Door (lock 1199, Viewing Room Key)
            // Caer Darrow's outdoor entrance door (map 0), the door INTO
            // Scholomance. Not inside the instance, so DC only meets it on a
            // walk-in rather than a teleport-in run; listed for completeness
            // since it is a keyed Scholomance door. Autocloses after 3s, which
            // the door-blocked action's re-click cooldown already handles.
            case 174626:  // Scholomance Door (lock 1159, Skeleton Key 13704)

            // --- Stratholme (map 329) ------------------------------------
            // Scarlet side — lock 299, The Scarlet Key (7146). This is the same
            // lock as the SM wing gates above; both dungeons are now exempt, but
            // still one entry at a time.
            case 175967:  // The Bastion Door
            case 175968:  // Hoard Door
            case 176194:  // Hall of the High Command
            // Undead side — lock 879, Key to the City (12382) or lockpicking
            // 300. The two King's Square Gates carry door.autoCloseTime 3000, so
            // they re-shut ~3s after opening; the door-blocked action re-clicks
            // on its per-door cooldown rather than latching once (that latch bug
            // was found on exactly this gate).
            case 175352:  // King's Square Gate
            case 175353:  // King's Square Gate
            case 175356:  // Gauntlet Gate
            case 175357:  // Gauntlet Gate (SmartAI: gossip-hello SET_INST_DATA)
            case 175368:  // Service Entrance Gate (SmartAI: gossip-hello set data)

            // --- Dire Maul North (map 429) -------------------------------
            // The two Gordok doors already open via the map-429 events 2 and 3
            // (a conditional UseGO — see DireMaulEvents.cpp). Listing them here
            // is the belt to that braces: the events are Optional, and if one
            // misfires the run used to fall through to the door-blocked
            // auto-pause because DcDoorPolicy suppresses bare-hands opening on
            // GO_FLAG_LOCKED. Both paths end in the same GameObject::Use(), so
            // whichever fires first wins and the second is a no-op (a Use() on
            // an already-activated door returns early on lootState).
            case 177219:  // Gordok Courtyard Door (lock 1563, Gordok Courtyard Key)
            case 177217:  // Gordok Inner Door (lock 1564, Gordok Inner Door Key)
            // The North wing's Crescent Key door, in the lower corridor among
            // the Gordok Brute/Mastiff/Mage-Lord packs. Dire Maul's other two
            // lock-1562 doors (177221, 179550) are West-wing and already open
            // via map-429 events 9 and 10; this one has no event because it sits
            // off the West boss path — the exemption is its only opener.
            case 179549:  // Dire Maul North — Door (lock 1562, Crescent Key)

            // --- Blackrock Depths (map 230) ------------------------------
            // Lock 680 — the Shadowforge Key (11000), or lockpicking 250. The
            // key drops from Fineous Darkvire, so a party that killed him could
            // in principle hold it; a bot party never does, and GO_FLAG_LOCKED
            // (addon flags 34 on every entry below) makes DcDoorPolicy suppress
            // the lockpicking slots as well. All five are plain traversal gates:
            // no ScriptName, no autoCloseTime, no SmartAI (bar the lever's), and
            // instance_blackrock_depths only caches two of their GUIDs — the
            // lever's (GoShadowLockGUID) and the Lyceum's (GoLyceumGUID) — and
            // never reads or writes any of their GO states.
            //
            // Lock 680 gates the run in three places, and it is the whole set or
            // nothing: opening one only moves the auto-pause to the next.
            //
            //   * The two Shadowforge Gates (170559 at x 496 / 170560 at x 570,
            //     both on the z ~ -70 floor) are the west and east ends of the
            //     Shadowforge City concourse. 170560 is the one the route east
            //     out of Bael'Gar walks into — it was the recorded blocker in
            //     6 of 10 runs of test plan tp-20260817-171356-1, every one of
            //     them parked at 10/20 bosses with "can't open ... 170560".
            //   * The East Garrison Door (x 560, z ~ -60) is the doorway into
            //     the room that holds the lever: that floor runs x 552..620 /
            //     y -68..-36 and pinches at x ~ 560, with the lever at the far
            //     (east) end. So it has to open before the Shadowforge Lock
            //     objective can be reached at all.
            //   * The Lyceum (x 1312, z ~ -92) is the single door out of the
            //     Shadowforge City side into the back half of the dungeon. Every
            //     boss from Ambassador Flamelash and The Seven through Magmus and
            //     Emperor Dagran Thaurissan is behind it. No run has reached it
            //     yet only because 170560 stopped them first.
            //
            // The lever is listed for the same reason the two Gordok doors above
            // are: map-230 event 2 clicks it (UseGO, which bypasses DcDoorPolicy),
            // but the route to that objective ENDS on the lever, so the
            // blocking-door value flags it first and the door-blocked action would
            // auto-pause the run one step short of the click. Both paths end in
            // the same GameObject::Use(); whichever fires first wins and the other
            // is a no-op (UseDoorOrButton early-returns unless lootState is
            // GO_READY). Clicking it is the intended sequence in full: Use() runs
            // SmartGameObjectAI::GossipHello (which returns false) before the lock
            // check, reaches the DOOR branch, and the resulting GO_ACTIVATED loot
            // state fires the lever's SMART_EVENT_GO_STATE_CHANGED chain that
            // closes the Giant Doors.
            case 170559:  // Shadowforge Gate — west (lock 680, Shadowforge Key)
            case 170560:  // Shadowforge Gate — east (lock 680, Shadowforge Key)
            case 170570:  // East Garrison Door (lock 680, Shadowforge Key)
            case 170558:  // The Lyceum (lock 680, Shadowforge Key)
            case 161460:  // The Shadowforge Lock (lock 680; SmartAI closes the
                          // Giant Doors off its own state change)

            // Lock 699 — the Prison Cell Key (11140), or lockpicking 250, on the
            // eight Detention Block cell doors. Same shape as the lock-680 gates
            // above and screened the same way: GAMEOBJECT_TYPE_DOOR, addon flags
            // 34, no ScriptName, no AIName, no smart_scripts row, no conditions
            // row, and no mention anywhere in instance_blackrock_depths — the
            // instance script's door enum stops at the Lyceum. They are ordinary
            // traversal gates into the cells, and the cells are where the route
            // to Houndmaster Grebmar goes: 170567 auto-paused a run of test plan
            // tp-20260817-171356-1 at 2/20 bosses, parked 0.0yd into the door on
            // a route the diag reported as ok/1seg dev=0.5.
            //
            // Listing all eight rather than only the observed one: the boss route
            // threads several of these cells depending on where the roster's
            // anchors land, they are interchangeable in every respect the
            // checklist tests, and one-at-a-time would just replay this failure
            // from a different cell.
            case 170562:
            case 170563:
            case 170564:
            case 170565:
            case 170566:
            case 170567:
            case 170568:
            case 170569:  // Cell Door ×8 (lock 699, Prison Cell Key)
                return true;
            default:
                return false;
        }
    }

    // The MIRROR-IMAGE special case: door gameobjects carrying NO lock at all
    // (template lockId 0) that a player nonetheless opens by simply clicking
    // them — ordinary traversal gates the dungeon expects you to walk through.
    //
    // BotCanOpenDoorLikePlayer otherwise refuses every lock-free door, because
    // lockId 0 is ALSO the shape of script/event seals the bot must not pop
    // (Uldaman's Seal of Khaz'Mul, lock-free and only opened by the keystone
    // event, isn't flagged GO_FLAG_NOT_SELECTABLE until its encounter is done,
    // so the generic flag screen can't tell them apart). We can't relax the
    // lock-free rule wholesale; instead we allowlist the entries verified in
    // the world DB to be plain clickable doors — no ScriptName, no AIName, no
    // instance-script GO-state control, no SmartAI.
    //
    // Scholomance's Iron Gates (175611-175618, 175620) and plain interior Doors
    // (175610, 175619) are exactly this: lock-free, scriptless room-to-room
    // gates the player clicks open. (The dungeon's *event* gates — Kirtonos
    // 175570 and the seven Gandling gates 177371-177377 — are deliberately
    // EXCLUDED; the instance script drives their state.)
    inline bool IsLockFreeClickable(uint32 goEntry)
    {
        switch (goEntry)
        {
            // Scholomance — interior traversal gates/doors (map 289)
            case 175610:  // Door
            case 175611:  // Iron Gate
            case 175612:  // Iron Gate
            case 175613:  // Iron Gate
            case 175614:  // Iron Gate
            case 175615:  // Iron Gate
            case 175616:  // Iron Gate
            case 175617:  // Iron Gate
            case 175618:  // Iron Gate
            case 175619:  // Door
            case 175620:  // Iron Gate
                return true;
            default:
                return false;
        }
    }
}

#endif

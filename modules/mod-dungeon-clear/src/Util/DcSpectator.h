/*
 * mod-dungeon-clear — DcSpectator.h
 *
 * Spectator camera (`.dc spectate` / addon "spectate"). Two modes, both of
 * which detach the human's CAMERA from their body:
 *
 *   Free   — possess an invisible, unattackable, flying WORLD_TRIGGER
 *            TempSummon via Unit::SetCharmedBy(CHARM_TYPE_POSSESS), the Eye of
 *            Acherus pattern. The client natively moves its camera and input to
 *            the possessed unit, so the camera flies anywhere in the map. The
 *            player's own character keeps running under bot AI (bot-self mode).
 *
 *   Follow — ride a bot: Player::SetViewpoint(target, true), the same call
 *            SPELL_AURA_BIND_SIGHT (Mind Vision) uses. The camera sits on the
 *            watched unit and tracks it as the SERVER moves it, with the
 *            client's normal orbit/zoom still live. The body is rooted for the
 *            duration so blind movement keys can't walk it off.
 *
 * Why the modes use different mechanisms — the load-bearing lesson from the
 * abandoned feat/cinematic-camera branch. A possessed unit is CLIENT-
 * authoritative: the client owns its movement, so a server-driven "camera that
 * follows the party" fights the client for control and loses. Farsight inverts
 * that — the watched unit is an ordinary server-moved unit the client merely
 * renders, so its motion is interpolated exactly as smoothly as any other mob's
 * and we synthesise no motion of our own. Free-fly needs client control (you
 * steer it); Follow must NOT have it. Do not "unify" them.
 *
 * Neither mode opens at the BODY. Follow rides a bot by construction; Free
 * summons its dummy at that same bot and hands the client across the gap under
 * farsight before possessing it (see Start). A `.dc test watch` GM's body sits
 * at the instance entrance for the whole run, so a free camera that spawned
 * there meant flying the length of the dungeon to catch up with the party.
 *
 * The watched unit is re-resolved on a slow tick (Tick below), so the camera
 * survives a leader-tank re-election, the tank dying, or the party moving on —
 * it hands off to another live tank bot on the map instead of freezing on a
 * corpse.
 *
 * This is session plumbing, not bot AI: it acts on the issuing player directly
 * and deliberately does NOT go through DungeonClearDispatch, the action/
 * trigger/strategy pipeline, or the leader-tank concept as a driver. It is
 * independent of the DC run — `dc off`/pause do not end spectate.
 *
 * Threading: Start/Toggle run on the world thread (commands, the addon chat
 * hook), but the teardown hooks (death, teleport) fire on map threads, and both
 * the AI mover window and Tick run on every map thread each player tick — so
 * the internal state map is mutex-guarded, with a lock-free active-count fast
 * path for the per-tick entry points. Per-player operations never race each
 * other (a player's session phase and map update are sequential); the lock only
 * guards the shared container across players on different maps.
 */

#ifndef _DUNGEON_CLEAR_DC_SPECTATOR_H
#define _DUNGEON_CLEAR_DC_SPECTATOR_H

#include <string>
#include <vector>

#include "ObjectGuid.h"

class Player;

namespace DcSpectator
{
    // True while `player` has an active spectator camera in EITHER mode
    // (guid-keyed lookup).
    bool IsActive(Player* player);

    // True while `player` is in Follow mode specifically.
    bool IsFollowing(Player* player);

    // The unit the Follow camera currently rides, or an empty guid when the
    // player isn't following. For status readouts.
    ObjectGuid WatchedGuid(Player* player);

    // True when GM mode was turned on BY US to hide this watcher (the flag
    // RequestFollowOnArrival carries), on either a live camera or a still-
    // pending arrival request. The GM-watch entry point reads it before ending
    // one watch to start another: Stop restores the flip, so without carrying
    // the answer forward the second hop would land the watcher visible and
    // targetable in the middle of the next run.
    bool HiddenByWatch(Player* player);

    // --- Free mode (possession) --------------------------------------------

    // Summon + possess the camera dummy. Returns false and fills `whyNot`
    // (when non-null) if the player is in a state that refuses possession.
    //
    // The dummy is summoned ON the bot the player would watch (the one a live
    // Follow camera is riding, else the default seat), NOT on the player's
    // body: `.dc test watch` parks that body at the instance entrance, so a
    // camera opened there began a dungeon's length behind the run. Switching
    // out of a live Follow camera is handled internally — that transition is
    // what carries the client across the gap, so do NOT Stop first.
    bool Start(Player* player, std::string* whyNot = nullptr);

    // Free -> off, Follow -> Free, nothing -> Free. Following is a step ON THE
    // WAY to free-fly rather than a separate thing to cancel, so a watching GM
    // toggles into the flying camera at the seat instead of back into their
    // parked body. Returns false only on a refused Start.
    bool Toggle(Player* player, std::string* whyNot = nullptr);

    // --- Follow mode (farsight) --------------------------------------------

    // Ride `target`'s camera. `target` may be null, in which case a watchable
    // bot is resolved automatically (see ResolveWatchTarget). Switching from an
    // active Free camera is handled internally — no need to Stop first.
    bool StartFollow(Player* player, Player* target = nullptr,
                     std::string* whyNot = nullptr);

    // Stop if already following, StartFollow otherwise.
    bool ToggleFollow(Player* player, Player* target = nullptr,
                      std::string* whyNot = nullptr);

    // The bot this player would watch BY DEFAULT: the leader tank of their group
    // when they have one, else the elected dungeon-clear leader on their map,
    // else any alive tank bot there. Null when the map holds nothing worth
    // watching — which is the normal answer in the open world. `exclude` skips a
    // candidate that is still live but on its way out (a logging-out bot is
    // valid right up until it isn't, so handing the camera to it would be a bad
    // seat). This picks a good STARTING seat; it is not the set of reachable
    // seats — that is WatchRoster.
    Player* ResolveWatchTarget(Player* viewer, Player* exclude = nullptr);

    // Every watchable bot on the viewer's map, in a stable order (leader tank
    // first, then GUID). Tanks hold no privilege here: cycling exists so a
    // watcher can sit on the healer through a wipe or a DPS through a burn.
    // Stable ordering is load-bearing — next/prev would shuffle under the
    // viewer if this re-ordered as the map's player list churns.
    std::vector<ObjectGuid> WatchRoster(Player* viewer);

    // Resolve a bot by exact name, else by case-insensitive prefix within the
    // viewer's map roster (randomised test-comp names are not worth typing in
    // full). Ambiguous prefixes take the first in roster order.
    Player* FindWatchableByName(Player* viewer, std::string const& name);

    // Point the camera at `target` (null = the default seat). Starts a camera
    // when there isn't one, and MOVES it in place when there is — never
    // Stop+Start, which would restore a `.dc test watch` GM-mode flip and drop
    // the flag recording it, un-hiding the watcher mid-run.
    bool SeatFollow(Player* viewer, Player* target, std::string* whyNot = nullptr);

    // Step through WatchRoster by `delta` (+1 next, -1 previous), wrapping. From
    // a cold start this begins following the default seat, so one keybind can
    // both start and cycle.
    bool CycleFollow(Player* viewer, int delta, std::string* whyNot = nullptr);

    // Human-readable roster for `.dc spectate list`, marking the current seat.
    std::string RosterText(Player* viewer);

    // Arm a Follow camera that starts once `viewer` finishes zoning into
    // `tank`'s instance. A teleport is asynchronous (the player spends a
    // loading screen off-map, and the teleport teardown hook below deliberately
    // stops any live camera on the way out), so the GM-watch entry point parks
    // its intent here and Tick picks it up on arrival. Overwrites any pending
    // request; Stop clears it. Pass gmModeApplied when the caller turned GM
    // mode ON for this watch, so Stop can put the GM back as it found them.
    void RequestFollowOnArrival(Player* viewer, ObjectGuid tankGuid,
                                bool gmModeApplied = false);

    // --- Lifecycle ----------------------------------------------------------

    // Tear down whichever camera is active: the possession + dummy in Free
    // mode, the viewpoint + root in Follow mode. Idempotent — safe to call
    // blind from every exit path (teleport, death, logout, toggle).
    void Stop(Player* player);

    // MANDATORY crash guard, called for EVERY logging-out player (bots
    // included), not just spectators. A Follow camera is an aura-less farsight,
    // and nothing in the core resets a viewer's m_seer when the watched unit is
    // destroyed: Player::SetViewpoint is only ever undone by the bind-sight
    // AURA's own removal (SpellAuraEffects HandleBindSight), and ~Unit merely
    // unlinks the shared-vision lists. So a watched bot logging out — which is
    // exactly how a test run ends — would leave every viewer's m_seer dangling
    // at freed memory, read by GetSightPosition() on the next visibility
    // update. This is the same shape as the aura-less-possession death crash
    // (see the spectator death guard in DungeonClearModule.cpp): the core's
    // cleanup assumes an aura we don't have, so we do it ourselves, here, while
    // the unit is still alive. Cheap no-op while nobody is spectating.
    void NotifyWatchedLoggingOut(Player* watched);

    // Slow per-player tick (PLAYERHOOK_ON_AFTER_UPDATE, the player's own map
    // thread). Starts a pending arrival request, and re-resolves the watched
    // unit for a live Follow camera. Cheap no-op while nobody is spectating.
    void Tick(Player* player, uint32 diff);

    // Scoped "AI mover window". While spectating in FREE mode, the session's
    // m_mover is the camera dummy, and the core packet handlers playerbots
    // injects bot actions into (HandleUseItemOpcode, HandleGameObjectUseOpcode,
    // …) silently drop on their remote-control guard `m_mover != _player` — so
    // a self-bot could not use items (soulstone, healthstone, potions, food)
    // while its human flew the camera. These two calls bracket the bot's AI
    // tick (both fired from PLAYERHOOK_ON_AFTER_UPDATE on the player's map
    // thread — see the mover window scripts in DungeonClearModule.cpp for the
    // ordering contract) and swap m_mover back to the player for just that
    // duration. Client movement packets for the dummy are processed in the
    // session phases, never inside this window, so camera control is
    // unaffected. Follow mode never redirects the mover, so both are no-ops
    // there. Both are cheap no-ops while nobody on the server is spectating.
    void BeginBotAiMoverWindow(Player* player);
    void EndBotAiMoverWindow(Player* player);
}

#endif

/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARCHATACTIONS_H
#define _PLAYERBOT_DUNGEONCLEARCHATACTIONS_H

#include "Action.h"

class PlayerbotAI;

class DcOnAction : public Action
{
public:
    DcOnAction(PlayerbotAI* botAI) : Action(botAI, "dc on") {}
    bool Execute(Event& event) override;
};

class DcOffAction : public Action
{
public:
    DcOffAction(PlayerbotAI* botAI) : Action(botAI, "dc off") {}
    bool Execute(Event& event) override;
};

class DcSkipAction : public Action
{
public:
    DcSkipAction(PlayerbotAI* botAI) : Action(botAI, "dc skip") {}
    bool Execute(Event& event) override;
};

// Toggles the `dungeon clear paused` flag. When running (and not paused) it
// pauses — the tank stops navigating and behaves like `dc off` while keeping
// all boss/skip progress. When already paused it resumes on the same boss
// (refusing if a party member is dead, like `dc on`).
class DcPauseAction : public Action
{
public:
    DcPauseAction(PlayerbotAI* botAI) : Action(botAI, "dc pause") {}
    bool Execute(Event& event) override;
};

// Auto-resume fired by DungeonClearDoorReopenedTrigger: when the run is paused
// for a door the tank couldn't open and a player then opens it, this resumes on
// the same boss without the player also hitting Resume. Shares the manual
// resume's cache-rebuild path; refuses (and the trigger retries) while a party
// member is dead. Not a chat keyword — driven only by the trigger.
class DcResumeOnDoorOpenedAction : public Action
{
public:
    DcResumeOnDoorOpenedAction(PlayerbotAI* botAI) : Action(botAI, "dungeon clear door reopened") {}
    bool Execute(Event& event) override;
};

// Toggles advanced-pull (LOS pull-to-camp) mode for the run. `dc pull on` /
// `dc pull off` set it explicitly; a bare `dc pull` flips it. Leader-owned, like
// pause; turning it off mid-pull also aborts the in-flight maneuver (releasing
// the party) via a phase reset.
class DcPullAction : public Action
{
public:
    DcPullAction(PlayerbotAI* botAI) : Action(botAI, "dc pull") {}
    bool Execute(Event& event) override;
};

class DcStatusAction : public Action
{
public:
    DcStatusAction(PlayerbotAI* botAI) : Action(botAI, "dc status") {}
    bool Execute(Event& event) override;
};

class DcBossesAction : public Action
{
public:
    DcBossesAction(PlayerbotAI* botAI) : Action(botAI, "dc bosses") {}
    bool Execute(Event& event) override;
};

class DcGoAction : public Action
{
public:
    DcGoAction(PlayerbotAI* botAI) : Action(botAI, "dc go") {}
    bool Execute(Event& event) override;
};

#endif

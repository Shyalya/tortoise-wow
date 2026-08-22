/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef MANGOS_INSTANCE_DATA_H
#define MANGOS_INSTANCE_DATA_H

#include "Common.h"
#include "ZoneScript.h"
#include "ObjectGuid.h"

class Map;
class Unit;
class Player;
class GameObject;
class Creature;
class WorldObject;

class InstanceData : public ZoneScript
{
    public:

        explicit InstanceData(Map *map) : instance(map) { SetMap(map); }
        virtual ~InstanceData() {}

        Map *instance;

        //On creation, NOT load.
        virtual void Initialize() {}

        //On load
        virtual void Load(const char* /*data*/) {}
        virtual void Create() {} // A la creation. Pas au chargement.

        //When save is needed, this function generates the data
        virtual const char* Save() { return ""; }

        void SaveToDB();

        //Called every map update
        void Update(uint32 /*diff*/) override {}

        //Used by the map's CanEnter function.
        //This is to prevent players from entering during boss encounters.
        virtual bool IsEncounterInProgress() const { return false; }

        // Spells
        virtual void CustomSpellCasted (uint32 /*spellId*/, Unit* /*caster*/ = nullptr, Unit* /*target*/ = nullptr) {}

        //All-purpose data storage 64 bit
        virtual uint64 GetData64(uint32 /*Data*/) { return 0; }
        virtual void SetData64(uint32 /*Data*/, uint64 /*Value*/) { }

        //Guid data storage (wrapper for set/get from uint64 storage
        ObjectGuid GetGuid(uint32 dataIdx) { return ObjectGuid(GetData64(dataIdx)); }
        void SetGuid(uint32 dataIdx, ObjectGuid value) { SetData64(dataIdx, value.GetRawValue()); }

        //All-purpose data storage 32 bit
        // AzerothCore encounter faces, honest defaults. There is no encounter
        // state machine on this core: NOT_STARTED (0) and an empty mask mean a
        // ported caller never skips a boss it should fight - it only walks to
        // one it could have skipped. GetPersistentData mirrors GetData, which
        // is this core's only per-instance store. ProcessEvent has no general
        // dispatcher here; scripts wire their events directly, so the default
        // swallows the call.
        virtual uint8 GetBossState(uint32 /*id*/) const { return 0; }
        virtual uint32 GetCompletedEncounterMask() const { return 0; }
        virtual uint32 GetPersistentData(uint32 type) { return GetData(type); }
        virtual void ProcessEvent(WorldObject* /*source*/, uint32 /*eventId*/) {}
        // Cross-faction instances arrived later; on this core the group's
        // faction is the instance's faction and nothing remaps it.
        // 2 is TEAM_NEUTRAL in the AzerothCore numbering the ported caller
        // compares against - the value that says "no faction stamped", which
        // is the truth here. 0 would read as "stamped Alliance".
        virtual uint32 GetTeamIdInInstance() const { return 2; }
        virtual uint32 GetData(uint32 /*Type*/) { return 0; }
        virtual void SetData(uint32 /*Type*/, uint32 /*Data*/) {}

        // Condition criteria additional requirements check
        // This is used for such things are heroic loot
        virtual bool CheckConditionCriteriaMeet(Player const* player, uint32 map_id, WorldObject const* source, uint32 instance_condition_id) const;
};
#endif

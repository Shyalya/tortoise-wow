-- Offline full-audit integrity repairs for the Turtle world database.
-- Every statement is idempotent and is limited to a reference that cannot be
-- resolved by the current world data.  No replacement loot or encounter data
-- is invented here.

-- A literal "0" is not an empty ScriptName. The script loader attempts to
-- resolve it and reports hundreds of false missing-script errors.
UPDATE `creature_template` SET `script_name` = '' WHERE `script_name` = '0';
UPDATE `gameobject_template` SET `script_name` = '' WHERE `script_name` = '0';
UPDATE `item_template` SET `script_name` = '' WHERE `script_name` = '0';
UPDATE `spell_template` SET `script_name` = '' WHERE `script_name` = '0';

-- Do not advertise loot stores that do not exist. This prevents loader/runtime
-- attempts to resolve impossible references while preserving every valid row.
UPDATE `creature_template` ct
LEFT JOIN `creature_loot_template` lt ON lt.`entry` = ct.`loot_id`
SET ct.`loot_id` = 0
WHERE ct.`loot_id` <> 0 AND lt.`entry` IS NULL;

UPDATE `creature_template` ct
LEFT JOIN `pickpocketing_loot_template` lt ON lt.`entry` = ct.`pickpocket_loot_id`
SET ct.`pickpocket_loot_id` = 0
WHERE ct.`pickpocket_loot_id` <> 0 AND lt.`entry` IS NULL;

UPDATE `creature_template` ct
LEFT JOIN `skinning_loot_template` lt ON lt.`entry` = ct.`skinning_loot_id`
SET ct.`skinning_loot_id` = 0
WHERE ct.`skinning_loot_id` <> 0 AND lt.`entry` IS NULL;

DELETE gl
FROM `gameobject_loot_template` gl
LEFT JOIN `item_template` i ON i.`entry` = gl.`item`
WHERE gl.`item` > 0 AND i.`entry` IS NULL;

DELETE nv
FROM `npc_vendor` nv
LEFT JOIN `item_template` i ON i.`entry` = nv.`item`
WHERE i.`entry` IS NULL;

-- Remove relationships whose endpoint no longer exists. These rows cannot be
-- reached by a player and generate loader errors or stale pool/event state.
DELETE cm
FROM `creature_movement` cm
LEFT JOIN `creature` c ON c.`guid` = cm.`id`
WHERE c.`guid` IS NULL;

DELETE cl
FROM `creature_linking` cl
LEFT JOIN `creature` child ON child.`guid` = cl.`guid`
LEFT JOIN `creature` master ON master.`guid` = cl.`master_guid`
WHERE child.`guid` IS NULL OR master.`guid` IS NULL;

DELETE cg
FROM `creature_groups` cg
LEFT JOIN `creature` member ON member.`guid` = cg.`member_guid`
LEFT JOIN `creature` leader ON leader.`guid` = cg.`leader_guid`
WHERE member.`guid` IS NULL OR leader.`guid` IS NULL;

DELETE gec
FROM `game_event_creature` gec
LEFT JOIN `creature` c ON c.`guid` = gec.`guid`
WHERE c.`guid` IS NULL;

DELETE geg
FROM `game_event_gameobject` geg
LEFT JOIN `gameobject` g ON g.`guid` = geg.`guid`
WHERE g.`guid` IS NULL;

DELETE pc
FROM `pool_creature` pc
LEFT JOIN `creature` c ON c.`guid` = pc.`guid`
LEFT JOIN `pool_template` p ON p.`entry` = pc.`pool_entry`
WHERE c.`guid` IS NULL OR p.`entry` IS NULL;

DELETE pg
FROM `pool_gameobject` pg
LEFT JOIN `gameobject` g ON g.`guid` = pg.`guid`
LEFT JOIN `pool_template` p ON p.`entry` = pg.`pool_entry`
WHERE g.`guid` IS NULL OR p.`entry` IS NULL;

DELETE pp
FROM `pool_pool` pp
LEFT JOIN `pool_template` child ON child.`entry` = pp.`pool_id`
LEFT JOIN `pool_template` mother ON mother.`entry` = pp.`mother_pool`
WHERE child.`entry` IS NULL OR mother.`entry` IS NULL;

DELETE gqr
FROM `gameobject_questrelation` gqr
LEFT JOIN `gameobject_template` g ON g.`entry` = gqr.`id`
LEFT JOIN `quest_template` q ON q.`entry` = gqr.`quest`
WHERE g.`entry` IS NULL OR q.`entry` IS NULL;

DELETE gir
FROM `gameobject_involvedrelation` gir
LEFT JOIN `gameobject_template` g ON g.`entry` = gir.`id`
LEFT JOIN `quest_template` q ON q.`entry` = gir.`quest`
WHERE g.`entry` IS NULL OR q.`entry` IS NULL;

DELETE cqr
FROM `creature_questrelation` cqr
LEFT JOIN `creature_template` c ON c.`entry` = cqr.`id`
LEFT JOIN `quest_template` q ON q.`entry` = cqr.`quest`
WHERE c.`entry` IS NULL OR q.`entry` IS NULL;

DELETE cir
FROM `creature_involvedrelation` cir
LEFT JOIN `creature_template` c ON c.`entry` = cir.`id`
LEFT JOIN `quest_template` q ON q.`entry` = cir.`quest`
WHERE c.`entry` IS NULL OR q.`entry` IS NULL;

-- Broken chain pointers prevent an otherwise valid quest from completing its
-- chain cleanly. Keep the quest and terminate only its missing next link.
UPDATE `quest_template` q
LEFT JOIN `quest_template` next_q ON next_q.`entry` = q.`NextQuestInChain`
SET q.`NextQuestInChain` = 0
WHERE q.`NextQuestInChain` <> 0 AND next_q.`entry` IS NULL;

-- A spawn with a real multi-point path must use waypoint movement. Single
-- point records are intentionally left alone because they may be script marks.
UPDATE `creature` c
JOIN
(
    SELECT `id`
    FROM `creature_movement`
    GROUP BY `id`
    HAVING COUNT(*) >= 2
) paths ON paths.`id` = c.`guid`
SET c.`movement_type` = 2
WHERE c.`movement_type` <> 2;

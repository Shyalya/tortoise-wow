-- ==============================================
-- FILE: cleanup_orphaned_creature_movement.sql
-- GENERATED: 20260905175321
-- ==============================================
-- A clean boot logs 304 "Table creature_movement contain path for creature
-- guid #, but this creature guid does not exist" warnings. One cause is
-- traced exactly: 20260513233443_world.sql deletes ~300 rows from `creature`
-- (including 8880, 8988, 9001, 9296 below) but never touches
-- `creature_movement` for the same guids. 20260625083947_world.sql shows the
-- same omission pattern against `creature_linking` instead (see the other
-- block in this file). The remaining guids below (21173, 21218, 660961,
-- 1068616, the 2562709-2562713 pool_creature members) trace to other
-- deletes with the same gap, or - for the pool members - to a spawn that
-- was never inserted in the first place (confirmed identical upstream in
-- Penqle/tortoise-wow, not a fork regression).
--
-- These are dead waypoints for creatures that no longer spawn; the engine
-- already skips them at load (Creature::LoadFromDB never runs, so nothing
-- reads creature_movement for a missing guid) - this only removes the log
-- noise and the stale rows.
DELETE FROM `creature_movement`
WHERE `id` IN (
    8880, 8988, 9001, 9296, 9581, 9874, 21173, 21218, 660961, 1068616,
    2562709, 2562710, 2562711, 2562712, 2562713
);

-- ==============================================
-- FILE: cleanup_orphaned_creature_linking.sql
-- GENERATED: 20260905175321
-- ==============================================
-- Same shape, `creature_linking` side: 41 rows whose slave `guid` has no
-- matching `creature` row (20260625083947_world.sql is the traced example -
-- it deletes `creature`, `creature_movement` and `creature_addon` for the
-- 190214-190242 batch but leaves `creature_linking` behind). The
-- 2570745-2570837 cluster's master guids don't exist either, upstream
-- included - content that was scripted as an escort/group encounter but
-- whose `creature` spawns were never authored.
DELETE FROM `creature_linking`
WHERE `guid` IN (
    99967, 99968,
    190215, 190216, 190217, 190219, 190220, 190221, 190223, 190224, 190226, 190227, 190229, 190231,
    2570745, 2570747, 2570748, 2570750, 2570752, 2570753, 2570754, 2570755, 2570757, 2570758, 2570759,
    2570760, 2570762, 2570766, 2570769, 2570803, 2570817, 2570818, 2570826, 2570830, 2570831, 2570832,
    2570833, 2570834, 2570837, 2577558, 2577560
);

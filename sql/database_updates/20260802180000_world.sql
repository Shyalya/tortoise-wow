-- ==============================================
-- FILE: survival_guard_directions.sql
-- GENERATED: 20260802180000
-- ==============================================
-- Guards never mentioned Survival, and Alah'Thalas had no Survival trainer.
--
-- Every other profession is listed in the guards' "profession trainers" menus -
-- thirteen entries for Herbalism, Alchemy and Enchanting, eleven for Mining.
-- Survival had none at all, in any city, so a player had no way to be told where
-- to go.
--
-- Two halves.
--
-- First, Hellador Swiftluck (62962) is finally placed. He is a complete
-- creature_template with 44 trainer rows and no spawn anywhere - trainer_type
-- already 2, level 50, and faction 371, which is the Alah'Thalas faction shared
-- by Callon Sunsail and the Dawnsparks. He was built for that city and never put
-- in it, which is also why Alah'Thalas was the one place with no Survival
-- trainer. He goes beside the skinner and the leatherworker, the outdoor trades.
--
-- Second, a Survival option in each of the thirteen profession menus, pointing
-- at the nearest trainer. The mapping was measured, not guessed: the centroid of
-- each menu's existing points of interest was compared against every Survival
-- trainer position. Eleven cities have one within 550 yards. Thunder Bluff's
-- nearest is Nasnan Hillcreek in Bloodhoof Village, 1173 yards away in the same
-- zone, which is still the right answer for a guard to give.
--
-- action_menu_id -1 with a point of interest is the convention the Turtle-added
-- entries already use - see Jewelcrafting in every one of these menus. It shows
-- the marker without a follow-up page. icon 6 and flags 99 are what all 275
-- existing points of interest carry, without exception.

-- ------------------------------------------------------------ Hellador's spawn
INSERT IGNORE INTO `creature`
    (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
     `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`,
     `health_percent`, `mana_percent`, `movement_type`)
VALUES
    (2902648, 62962, 0, 4440.0, -2824.0, 10.38, 1.0, 300, 300, 0, 100, 100, 0);

-- ------------------------------------------------------------ Points of interest
INSERT IGNORE INTO `points_of_interest` (`entry`, `x`, `y`, `icon`, `flags`, `data`, `icon_name`)
VALUES
    (10101, -9073.82,  338.141, 6, 99, 0, 'Krennan Wildberry'),
    (10102, -2282.58, -252.914, 6, 99, 0, 'Nasnan Hillcreek'),
    (10103,  2004.57, -4643.45, 6, 99, 0, 'Brakan'),
    (10104, -4908.46, -1181.60, 6, 99, 0, 'Eissinn Cragbelly'),
    (10105, 10062.70,  2399.61, 6, 99, 0, 'Nallaeth'),
    (10106,  1506.47,  49.4654, 6, 99, 0, 'Cynthessa Grimblood'),
    (10107,  315.389, -4669.36, 6, 99, 0, 'Thonk'),
    (10108,  2286.59,  300.348, 6, 99, 0, 'Karolina Cloven'),
    (10109, -5651.79, -495.228, 6, 99, 0, 'Dyrohrinn Boulderhorn'),
    (10110,  9795.25,  960.877, 6, 99, 0, 'Filadon Shieldarrow'),
    (10111,   4440.0,  -2824.0, 6, 99, 0, 'Hellador Swiftluck')
ON DUPLICATE KEY UPDATE `icon_name` = VALUES(`icon_name`);

-- ------------------------------------------------------------ Guard menu entries
INSERT IGNORE INTO `gossip_menu_option`
    (`menu_id`, `id`, `option_icon`, `option_text`, `option_id`, `npc_option_npcflag`,
     `action_menu_id`, `action_poi_id`, `box_coded`, `condition_id`)
VALUES
    (  421, 14, 0, 'Survival', 1, 1, -1, 10101, 0, 0),  -- Stormwind
    (  751, 12, 0, 'Survival', 1, 1, -1, 10102, 0, 0),  -- Thunder Bluff, points to Bloodhoof
    ( 1942, 14, 0, 'Survival', 1, 1, -1, 10103, 0, 0),  -- Orgrimmar
    ( 2168, 14, 0, 'Survival', 1, 1, -1, 10104, 0, 0),  -- Ironforge
    ( 2351, 10, 0, 'Survival', 1, 1, -1, 10105, 0, 0),  -- Darnassus
    ( 2847, 14, 0, 'Survival', 1, 1, -1, 10106, 0, 0),  -- Undercity
    ( 3284, 13, 0, 'Survival', 1, 1, -1, 10107, 0, 0),  -- Razor Hill
    ( 3330, 12, 0, 'Survival', 1, 1, -1, 10102, 0, 0),  -- Bloodhoof Village
    ( 3355, 13, 0, 'Survival', 1, 1, -1, 10108, 0, 0),  -- Brill
    ( 3532, 13, 0, 'Survival', 1, 1, -1, 10101, 0, 0),  -- Goldshire
    ( 3558, 13, 0, 'Survival', 1, 1, -1, 10109, 0, 0),  -- Kharanos
    ( 3572, 10, 0, 'Survival', 1, 1, -1, 10110, 0, 0),  -- Dolanaar
    (61772, 11, 0, 'Survival', 1, 1, -1, 10111, 0, 0)   -- Alah'Thalas
ON DUPLICATE KEY UPDATE `action_poi_id` = VALUES(`action_poi_id`);

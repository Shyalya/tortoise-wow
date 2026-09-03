-- First-pass world data integrity repairs.

-- These two custom spawn rows survived after their creature templates were
-- removed. They cannot be instantiated and only generate loader errors.
DELETE c
FROM `creature` c
LEFT JOIN `creature_template` ct ON ct.`entry` = c.`id`
WHERE ct.`entry` IS NULL;

-- Preserve both original endpoints while normalizing reversed stat ranges.
UPDATE `creature_template` ct
JOIN
(
    SELECT `entry`,
           LEAST(`health_min`, `health_max`) AS `new_min`,
           GREATEST(`health_min`, `health_max`) AS `new_max`
    FROM `creature_template`
    WHERE `health_min` > `health_max`
) fixed ON fixed.`entry` = ct.`entry`
SET ct.`health_min` = fixed.`new_min`,
    ct.`health_max` = fixed.`new_max`;

UPDATE `creature_template` ct
JOIN
(
    SELECT `entry`,
           LEAST(`dmg_min`, `dmg_max`) AS `new_min`,
           GREATEST(`dmg_min`, `dmg_max`) AS `new_max`
    FROM `creature_template`
    WHERE `dmg_min` > `dmg_max`
) fixed ON fixed.`entry` = ct.`entry`
SET ct.`dmg_min` = fixed.`new_min`,
    ct.`dmg_max` = fixed.`new_max`;

UPDATE `creature_template` ct
JOIN
(
    SELECT `entry`,
           LEAST(`gold_min`, `gold_max`) AS `new_min`,
           GREATEST(`gold_min`, `gold_max`) AS `new_max`
    FROM `creature_template`
    WHERE `gold_min` > `gold_max`
) fixed ON fixed.`entry` = ct.`entry`
SET ct.`gold_min` = fixed.`new_min`,
    ct.`gold_max` = fixed.`new_max`;

-- Darrowshire's town-square focus does not have a linked trap. Entry 1 is
-- not a trap and cannot legally be referenced from data2.
UPDATE `gameobject_template`
SET `data2` = 0
WHERE `entry` = 944 AND `type` = 8 AND `data2` = 1;

-- The matching 1.18.1 client has no TaxiPath 436. Keep the outdoor
-- Naxxramas model visible as a static generic object instead of discarding it
-- as an unusable moving transport during startup.
UPDATE `gameobject_template`
SET `type` = 5, `data0` = 0
WHERE `entry` = 181056 AND `type` = 15 AND `data0` = 436;

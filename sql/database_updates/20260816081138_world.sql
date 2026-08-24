-- Warsong: die Schranke gehoert an den Quartiermeister, nicht an den
-- Gegenstand. Am Gegenstand haengt sie zusaetzlich am Anlegen, und das war
-- in Warsong nie so - dort entschied immer der Haendler. Alterac bleibt
-- unberuehrt, dort sitzt sie richtig.

-- 1) fehlende Bedingungen
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 889, 5, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=889 AND `value2`=5);
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 889, 6, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=889 AND `value2`=6);
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 889, 7, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=889 AND `value2`=7);
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 890, 5, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=890 AND `value2`=5);
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 890, 6, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=890 AND `value2`=6);
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 890, 7, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=890 AND `value2`=7);

-- 2) Schranke an die Haendlerzeile
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=890 AND `value2`=5 LIMIT 1)
WHERE `entry`=14753 AND `item` IN (19514,19515,19516,19517,19522,19523,19524,19525,19530,19531,19532,19533,19538,19539,19540,19541,20428,20431,20439,20444);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=890 AND `value2`=6 LIMIT 1)
WHERE `entry`=14753 AND `item` IN (19546,19547,19548,19549,19554,19555,19556,19557,19562,19563,19564,19565,19570,19571,19572,19573,20434,20438,20440,20443);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=890 AND `value2`=7 LIMIT 1)
WHERE `entry`=14753 AND `item` IN (19506,22672,22748,22749,22750,22752,22753);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=889 AND `value2`=5 LIMIT 1)
WHERE `entry`=14754 AND `item` IN (19510,19511,19512,19513,19518,19519,19520,19521,19526,19527,19528,19529,19534,19535,19536,19537,20426,20427,20429,20442);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=889 AND `value2`=6 LIMIT 1)
WHERE `entry`=14754 AND `item` IN (19542,19543,19544,19545,19550,19551,19552,19553,19558,19559,19560,19561,19566,19567,19568,19569,20425,20430,20437,20441);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=889 AND `value2`=7 LIMIT 1)
WHERE `entry`=14754 AND `item` IN (19505,22651,22673,22676,22740,22741,22747);

-- 3) und vom Gegenstand wieder herunter
UPDATE `item_template` SET `required_reputation_faction`=0, `required_reputation_rank`=0
WHERE `entry` IN (19505,19506,19510,19511,19512,19513,19514,19515,19516,19517,19518,19519,19520,19521,19522,19523,19524,19525,19526,19527,19528,19529,19530,19531,19532,19533,19534,19535,19536,19537,19538,19539,19540,19541,19542,19543,19544,19545,19546,19547,19548,19549,19550,19551,19552,19553,19554,19555,19556,19557,19558,19559,19560,19561,19562,19563,19564,19565,19566,19567,19568,19569,19570,19571,19572,19573,20425,20426,20427,20428,20429,20430,20431,20434,20437,20438,20439,20440,20441,20442,20443,20444,22651,22672,22673,22676,22740,22741,22747,22748,22749,22750,22752,22753);

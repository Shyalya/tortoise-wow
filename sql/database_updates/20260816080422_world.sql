-- Ruf-Schranken fuer die Alterac- und Warsong-Belohnungen.
-- Turtle liefert diese Felder leer aus; Arathibecken (509/510) ist bereits
-- vollstaendig und bleibt unberuehrt. Leiter nach Ausruestungsplatz und Guete,
-- wie beim Arathibecken. Geteilte Waren koennen keine Fraktion am Gegenstand
-- tragen und laufen deshalb ueber die Haendlerbedingung.

-- 1) Bedingungen fuer die von beiden Seiten verkauften Waren
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 729, 7, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=729 AND `value2`=7);
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 730, 7, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=730 AND `value2`=7);
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 889, 4, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=889 AND `value2`=4);
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 889, 7, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=889 AND `value2`=7);
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 890, 4, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=890 AND `value2`=4);
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
SELECT 5, 890, 7, 0, 0, 0 FROM DUAL WHERE NOT EXISTS
  (SELECT 1 FROM `conditions` WHERE `type`=5 AND `value1`=890 AND `value2`=7);

-- 2) Seitenexklusive Waren: Schranke am Gegenstand (greift auch beim Anlegen)
-- Fraktion 729, Freundlich (1)
UPDATE `item_template` SET `required_reputation_faction`=729, `required_reputation_rank`=4
WHERE `entry` IN (19031);
-- Fraktion 729, Wohlwollend (8)
UPDATE `item_template` SET `required_reputation_faction`=729, `required_reputation_rank`=5
WHERE `entry` IN (19083,19085,19087,19088,19089,19090,19095,19096);
-- Fraktion 729, Respektvoll (3)
UPDATE `item_template` SET `required_reputation_faction`=729, `required_reputation_rank`=6
WHERE `entry` IN (19099,19101,19103);
-- Fraktion 729, Ehrfuerchtig (2)
UPDATE `item_template` SET `required_reputation_faction`=729, `required_reputation_rank`=7
WHERE `entry` IN (19029,19046);
-- Fraktion 730, Freundlich (1)
UPDATE `item_template` SET `required_reputation_faction`=730, `required_reputation_rank`=4
WHERE `entry` IN (19032);
-- Fraktion 730, Wohlwollend (8)
UPDATE `item_template` SET `required_reputation_faction`=730, `required_reputation_rank`=5
WHERE `entry` IN (19084,19086,19091,19092,19093,19094,19097,19098);
-- Fraktion 730, Respektvoll (3)
UPDATE `item_template` SET `required_reputation_faction`=730, `required_reputation_rank`=6
WHERE `entry` IN (19100,19102,19104);
-- Fraktion 730, Ehrfuerchtig (2)
UPDATE `item_template` SET `required_reputation_faction`=730, `required_reputation_rank`=7
WHERE `entry` IN (19030,19045);
-- Fraktion 889, Wohlwollend (20)
UPDATE `item_template` SET `required_reputation_faction`=889, `required_reputation_rank`=5
WHERE `entry` IN (19510,19511,19512,19513,19518,19519,19520,19521,19526,19527,19528,19529,19534,19535,19536,19537,20426,20427,20429,20442);
-- Fraktion 889, Respektvoll (20)
UPDATE `item_template` SET `required_reputation_faction`=889, `required_reputation_rank`=6
WHERE `entry` IN (19542,19543,19544,19545,19550,19551,19552,19553,19558,19559,19560,19561,19566,19567,19568,19569,20425,20430,20437,20441);
-- Fraktion 889, Ehrfuerchtig (7)
UPDATE `item_template` SET `required_reputation_faction`=889, `required_reputation_rank`=7
WHERE `entry` IN (19505,22651,22673,22676,22740,22741,22747);
-- Fraktion 890, Wohlwollend (20)
UPDATE `item_template` SET `required_reputation_faction`=890, `required_reputation_rank`=5
WHERE `entry` IN (19514,19515,19516,19517,19522,19523,19524,19525,19530,19531,19532,19533,19538,19539,19540,19541,20428,20431,20439,20444);
-- Fraktion 890, Respektvoll (20)
UPDATE `item_template` SET `required_reputation_faction`=890, `required_reputation_rank`=6
WHERE `entry` IN (19546,19547,19548,19549,19554,19555,19556,19557,19562,19563,19564,19565,19570,19571,19572,19573,20434,20438,20440,20443);
-- Fraktion 890, Ehrfuerchtig (7)
UPDATE `item_template` SET `required_reputation_faction`=890, `required_reputation_rank`=7
WHERE `entry` IN (19506,22672,22748,22749,22750,22752,22753);

-- 3) Geteilte Waren: Schranke an der Haendlerzeile, je Seite eigene Fraktion
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=730 AND `value2`=7 LIMIT 1)
WHERE `entry`=13216 AND `item` IN (19308,19309,19310,19311,19312,19315,19321,19323,19324,19325,21563);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=730 AND `value2`=7 LIMIT 1)
WHERE `entry`=13217 AND `item` IN (19308,19309,19310,19311,19312,19315,19321,19323,19324,19325,21563);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=729 AND `value2`=7 LIMIT 1)
WHERE `entry`=13218 AND `item` IN (19308,19309,19310,19311,19312,19315,19321,19323,19324,19325,21563);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=729 AND `value2`=7 LIMIT 1)
WHERE `entry`=13219 AND `item` IN (19308,19309,19310,19311,19312,19315,19321,19323,19324,19325,21563);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=890 AND `value2`=4 LIMIT 1)
WHERE `entry`=14753 AND `item` IN (21565,21566,21567,21568);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=890 AND `value2`=7 LIMIT 1)
WHERE `entry`=14753 AND `item` IN (19578,19580,19581,19582,19583,19584,19587,19589,19590,19595,19596,19597);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=889 AND `value2`=4 LIMIT 1)
WHERE `entry`=14754 AND `item` IN (21565,21566,21567,21568);
UPDATE `npc_vendor` SET `condition_id` = (SELECT `condition_entry` FROM `conditions`
    WHERE `type`=5 AND `value1`=889 AND `value2`=7 LIMIT 1)
WHERE `entry`=14754 AND `item` IN (19578,19580,19581,19582,19583,19584,19587,19589,19590,19595,19596,19597);

-- ===================================================================
-- Ruecknahme, falls die Leiter doch nicht gefallen soll:
-- ===================================================================
-- -- Nimmt die Ruf-Schranken wieder zurueck.
-- 
-- UPDATE `item_template` SET `required_reputation_faction`=0, `required_reputation_rank`=0
-- WHERE `entry` IN (19029,19030,19031,19032,19045,19046,19083,19084,19085,19086,19087,19088,19089,19090,19091,19092,19093,19094,19095,19096,19097,19098,19099,19100,19101,19102,19103,19104,19505,19506,19510,19511,19512,19513,19514,19515,19516,19517,19518,19519,19520,19521,19522,19523,19524,19525,19526,19527,19528,19529,19530,19531,19532,19533,19534,19535,19536,19537,19538,19539,19540,19541,19542,19543,19544,19545,19546,19547,19548,19549,19550,19551,19552,19553,19554,19555,19556,19557,19558,19559,19560,19561,19562,19563,19564,19565,19566,19567,19568,19569,19570,19571,19572,19573,20425,20426,20427,20428,20429,20430,20431,20434,20437,20438,20439,20440,20441,20442,20443,20444,22651,22672,22673,22676,22740,22741,22747,22748,22749,22750,22752,22753);
-- UPDATE `npc_vendor` SET `condition_id`=0 WHERE `entry`=13216 AND `item` IN (19308,19309,19310,19311,19312,19315,19321,19323,19324,19325,21563);
-- UPDATE `npc_vendor` SET `condition_id`=0 WHERE `entry`=13217 AND `item` IN (19308,19309,19310,19311,19312,19315,19321,19323,19324,19325,21563);
-- UPDATE `npc_vendor` SET `condition_id`=0 WHERE `entry`=13218 AND `item` IN (19308,19309,19310,19311,19312,19315,19321,19323,19324,19325,21563);
-- UPDATE `npc_vendor` SET `condition_id`=0 WHERE `entry`=13219 AND `item` IN (19308,19309,19310,19311,19312,19315,19321,19323,19324,19325,21563);
-- UPDATE `npc_vendor` SET `condition_id`=0 WHERE `entry`=14753 AND `item` IN (21565,21566,21567,21568);
-- UPDATE `npc_vendor` SET `condition_id`=0 WHERE `entry`=14753 AND `item` IN (19578,19580,19581,19582,19583,19584,19587,19589,19590,19595,19596,19597);
-- UPDATE `npc_vendor` SET `condition_id`=0 WHERE `entry`=14754 AND `item` IN (21565,21566,21567,21568);
-- UPDATE `npc_vendor` SET `condition_id`=0 WHERE `entry`=14754 AND `item` IN (19578,19580,19581,19582,19583,19584,19587,19589,19590,19595,19596,19597);

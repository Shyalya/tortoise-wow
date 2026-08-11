-- Quest 746 "Dwarven Digging" cannot be finished
--
-- The quest asks for five Broken Tools (item 4703). The two creatures that are
-- meant to drop them, Bael'dun Digger (2989) and Bael'dun Appraiser (2990) at
-- the digsite west of Bloodhoof Village in Mulgore, carry item 4702 instead -
-- Prospector's Pick, which no quest in the database asks for. Item 4703 appears
-- in no loot table at all, for any creature or object.
--
-- So the quest is unfinishable, and the digits are one apart. Present in
-- upstream's base data as well, not something introduced here.
--
-- The drop chances are left as they are: -41 and -34, negative meaning a quest
-- drop chance rather than a percentage.
--
-- Reported by a player, 2026-08-11. Takes effect immediately with
-- `reload creature_loot_template`; no restart needed.

UPDATE `creature_loot_template`
   SET `item` = 4703
 WHERE `entry` IN (2989, 2990)
   AND `item` = 4702;

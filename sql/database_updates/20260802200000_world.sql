-- ==============================================
-- FILE: alterac_item_effects.sql
-- GENERATED: 20260802200000
-- ==============================================
-- Four Alterac items carried developer notes instead of effects.
--
-- Their descriptions read "Add this effect when implemented: ..." and then
-- described something that was never built - not on the server, and not in the
-- client's Spell.dbc either. The items did have spells, but different ones, and
-- in one case an obvious copy-paste: the hunting spear carried 13679, the two
-- piece haste bonus of the Viper set.
--
-- The effects are built here. Six new spells, ids from 90001, which neither the
-- server nor the client knows - deliberately, because reusing an id the client
-- does know would make it render that spell's text instead. The cost is that the
-- green "Use:" and "Chance on hit:" lines stay empty: the client builds those
-- from its own Spell.dbc and has nothing for these ids.
--
-- Which is why each item description is rewritten to state the effect in plain
-- words. Item descriptions come from the server, unlike spell tooltips, so that
-- text does reach the player - and it replaces the developer note that was sitting
-- there in the meantime.
--
-- Each spell is copied from a donor so the hundred-odd columns are right, and only
-- what differs is overwritten. Donors: 45425 Potion of Quickness (timed self buff,
-- two auras), 21066 Void Bolt (ranged damage plus a slow), 2652 Touch of Weakness
-- (proc trigger aura), 2050 Lesser Heal (direct heal).
--
-- Existing effects are left alone. The shield keeps 7516, the sword keeps its
-- Vampirism, the robe keeps 18033 and 21619 - and the spear keeps 13679, odd as
-- it looks, because removing it would quietly weaken an item nobody asked me to
-- rebalance.

-- ---------------------------------------------------------------- 90001 Take Cover
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45425;
UPDATE `tmp_spell` SET
    `entry` = 90001,
    `name` = 'Take Cover',
    `nameSubtext` = '',
    `description` = 'Reduces damage taken by 10% for 10 sec, but reduces movement speed by 80%.',
    `effectApplyAuraName1` = 87,  `effectBasePoints1` = -11,
    `effectApplyAuraName2` = 33,  `effectBasePoints2` = -81,
    `durationIndex` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- --------------------------------------------------------------- 90002 Throw Spear
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 21066;
UPDATE `tmp_spell` SET
    `entry` = 90002,
    `name` = 'Throw Spear',
    `nameSubtext` = '',
    `description` = 'Hurls the spear at an enemy, dealing 75 damage and slowing it by 30% for 2 sec.',
    `effectBasePoints1` = 74,
    `effectApplyAuraName2` = 33,  `effectBasePoints2` = -31,
    `durationIndex` = 39;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- ----------------------------------------------------- 90003 Rage of Alterac (buff)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 2652;
UPDATE `tmp_spell` SET
    `entry` = 90003,
    `name` = 'Rage of Alterac',
    `nameSubtext` = '',
    `description` = 'Your attacks heal you for 10 health.',
    `effectApplyAuraName1` = 42,  `effectTriggerSpell1` = 90004,
    `effectBasePoints1` = 0,
    `durationIndex` = 8,
    `procFlags` = 20,             -- melee swing and melee ability
    `procChance` = 100;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- ----------------------------------------------------- 90004 Rage of Alterac (heal)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 2050;
UPDATE `tmp_spell` SET
    `entry` = 90004,
    `name` = 'Rage of Alterac',
    `nameSubtext` = '',
    `description` = '',
    `effectBasePoints1` = 9,
    `effectImplicitTargetA1` = 1,   -- the caster, not a chosen target
    `rangeIndex` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- --------------------------------------------------- 90005 Fiery Temper (equip proc)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 2652;
UPDATE `tmp_spell` SET
    `entry` = 90005,
    `name` = 'Fiery Temper',
    `nameSubtext` = '',
    `description` = 'Your healing spells have a chance to imbue the target with a fiery temper.',
    `effectApplyAuraName1` = 42,  `effectTriggerSpell1` = 90006,
    `effectBasePoints1` = 0,
    `durationIndex` = 21,         -- permanent, it is worn
    `procFlags` = 16384,          -- successful healing spell
    `procChance` = 10;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- ---------------------------------------------------- 90006 Fiery Temper (the buff)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45425;
UPDATE `tmp_spell` SET
    `entry` = 90006,
    `name` = 'Fiery Temper',
    `nameSubtext` = '',
    `description` = 'Attack and casting speed increased by 5%.',
    `effectApplyAuraName1` = 138, `effectBasePoints1` = 4,
    `effectApplyAuraName2` = 65,  `effectBasePoints2` = 4,
    `effectImplicitTargetA1` = 21,  -- lands on whoever was healed
    `effectImplicitTargetA2` = 21,
    `rangeIndex` = 5,
    `durationIndex` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- ------------------------------------------------------------------- The items
-- Shield: the use effect goes in slot 2, two minute cooldown. Slot 1 keeps 7516.
UPDATE `item_template` SET
    `spellid_2` = 90001, `spelltrigger_2` = 0, `spellcooldown_2` = 120000,
    `spellcategorycooldown_2` = 120000,
    `description` = 'Take cover behind the shield, reducing damage taken by 10% for 10 sec, but reducing your movement speed by 80%. (2 min cooldown)'
WHERE `entry` = 55023;

-- Spear: likewise. Slot 1 keeps 13679.
UPDATE `item_template` SET
    `spellid_2` = 90002, `spelltrigger_2` = 0, `spellcooldown_2` = 120000,
    `spellcategorycooldown_2` = 120000,
    `description` = 'Hurl the spear at your target, dealing 75 damage and slowing it by 30% for 2 sec. (2 min cooldown)'
WHERE `entry` = 55026;

-- Sword: chance on hit in slot 2. Slot 1 keeps Vampirism.
UPDATE `item_template` SET
    `spellid_2` = 90003, `spelltrigger_2` = 2, `spellppmrate_2` = 2,
    `description` = 'A sword from the treasury of Alterac. Finally in better hands. Chance on hit: your attacks instill a rage within you, healing you for 10 health with every attack for 15 sec.'
WHERE `entry` = 55030;

-- Robe: worn effect in slot 3. Slots 1 and 2 keep 18033 and 21619.
UPDATE `item_template` SET
    `spellid_3` = 90005, `spelltrigger_3` = 1,
    `description` = 'The heiress of Alterac is known for her fierce temperament. Your healing spells have a chance to imbue your target with a fiery temper, increasing their attack and casting speed by 5% for 10 sec.'
WHERE `entry` = 55032;

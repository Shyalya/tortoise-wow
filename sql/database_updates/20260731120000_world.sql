-- ==============================================
-- FILE: viper_set_wild_regeneration.sql
-- GENERATED: 20260731120000
-- ==============================================
-- Embrace of the Viper (item set 162), five piece bonus.
--
-- Spell 44070 "Wild Regeneration Passive" carries SPELL_AURA_PROC_TRIGGER_SPELL
-- at procChance 100, with procFlags 664232 - among them taking a melee hit,
-- taking a spell hit and taking any damage. There was no spell_proc_event row,
-- so it triggered the heal on every hit with no cooldown at all.
--
-- Its own description states what was intended:
--
--   "When your health drops below 35%, you rapidly heal $44069o1 health over
--    $44069d. This effect can trigger only once every 3 min."
--
-- The cooldown belongs here. The 35% health condition cannot be expressed in
-- this table and lives in the spell script named below.
--
-- procFlags stays 0: with no value here the core uses the ones on the spell
-- itself, which is what we want. Repeating them earns field value redundant
-- at startup and would only drift if the spell data ever changed.

INSERT IGNORE INTO `spell_proc_event`
    (`entry`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`,
     `SpellFamilyMask2`, `procFlags`, `procEx`, `ppmRate`, `CustomChance`, `Cooldown`)
VALUES
    (44070, 0, 0, 0, 0, 0, 0, 0, 0, 0, 180)
ON DUPLICATE KEY UPDATE `procFlags` = 0, `Cooldown` = 180;

UPDATE `spell_template`
SET `script_name` = 'spell_item_wild_regeneration'
WHERE `entry` = 44070;

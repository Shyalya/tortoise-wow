-- Verified fixes from the Penqle/tortoise-wow issue audit.

-- #428: the Starbreeze Village water barrel intersects the building geometry,
-- causing false line-of-sight failures when players try to loot it.
UPDATE `gameobject`
SET `position_y` = 446.778
WHERE `guid` = 49614 AND `id` = 3658;

-- #302: Daghelm exists, but the original coordinate causes the 1.18.1 client
-- to cull his model.  Moving the spawn one centimetre preserves placement and
-- invalidates the bad culling boundary (the upstream owner confirmed that a
-- tiny coordinate adjustment resolves it).
UPDATE `creature`
SET `position_x` = 1850.30
WHERE `guid` = 2583278 AND `id` = 61982;

-- #393: Mark of Sorcery was handled by the appearance-token script but had no
-- skin mapping.  Values are taken from the 1.18.1 client CharSections.dbc.
REPLACE INTO `custom_character_skins` (`token_id`, `skin_male`, `skin_female`)
VALUES (61111, 19, 18);

-- #355: Holy Strike's Mending Light follow-up requires explicit execution;
-- the core script handles the trigger and its reduced self-heal.
UPDATE `spell_template`
SET `script_name` = 'spell_paladin_mending_light'
WHERE `entry` IN (51324, 51875, 51876, 51877, 51878, 51879, 51880, 51881);

-- #423 server-side profession association for Shining Copper Cuffs.  The
-- corresponding client SkillLineAbility.dbc row must also be present for the
-- craft to appear in the profession window.
REPLACE INTO `skill_line_ability`
    (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`,
     `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`)
VALUES
    (36598, 755, 41335, 0, 0, 1, 0, 0, 120, 100, 0);

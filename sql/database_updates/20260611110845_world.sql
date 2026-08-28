-- ==============================================
-- FILE: create_and_populate_spell_extra.sql
-- GENERATED: 20260611110845
-- ==============================================
USE `tw_world`;

CREATE TABLE IF NOT EXISTS `spell_extra` (
  `entry` int(10) unsigned NOT NULL DEFAULT 0,
  `effectBonusCoefficient1` float NOT NULL DEFAULT -1,
  `effectBonusCoefficient2` float NOT NULL DEFAULT -1,
  `effectBonusCoefficient3` float NOT NULL DEFAULT -1,
  `minTargetLevel` int(10) unsigned NOT NULL DEFAULT 0,
  `customFlags` int(10) unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`entry`) USING BTREE
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_general_ci ROW_FORMAT=FIXED COMMENT='Server-only Spell.dbc fields';

REPLACE INTO `spell_extra`
(
    `entry`,
    `effectBonusCoefficient1`,
    `effectBonusCoefficient2`,
    `effectBonusCoefficient3`,
    `minTargetLevel`,
    `customFlags`
)
SELECT
    `entry`,
    `effectBonusCoefficient1`,
    `effectBonusCoefficient2`,
    `effectBonusCoefficient3`,
    `minTargetLevel`,
    `customFlags`
FROM `spell_template`;


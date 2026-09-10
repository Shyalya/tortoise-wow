-- Personality per resident bot (rolled on first sight, panel-editable later).
CREATE TABLE IF NOT EXISTS `mod_turtlebots_personality` (
  `guid` INT UNSIGNED NOT NULL,
  `personality` VARCHAR(24) NOT NULL,
  `assigned_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

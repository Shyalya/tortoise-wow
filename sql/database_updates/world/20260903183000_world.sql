-- Keep the transport templates aligned with the 1.18.1 TaxiPathNode.dbc shipped
-- with this project.  The original base dump contains obsolete development
-- path ids (967, 1221, 1500, 1501 and 1636) which are not present in the
-- released DBC and cause the core to skip the affected boats and zeppelins.
UPDATE `gameobject_template` SET `data0` = 72  WHERE `entry` = 20808;
UPDATE `gameobject_template` SET `data0` = 121 WHERE `entry` = 164871;
UPDATE `gameobject_template` SET `data0` = 110 WHERE `entry` = 175080;
UPDATE `gameobject_template` SET `data0` = 116 WHERE `entry` = 176231;
UPDATE `gameobject_template` SET `data0` = 117 WHERE `entry` = 176244;
UPDATE `gameobject_template` SET `data0` = 323 WHERE `entry` = 176250;
UPDATE `gameobject_template` SET `data0` = 120 WHERE `entry` = 176495;
UPDATE `gameobject_template` SET `data0` = 122 WHERE `entry` = 177233;
UPDATE `gameobject_template` SET `data0` = 294 WHERE `entry` = 181646;
UPDATE `gameobject_template` SET `data0` = 295 WHERE `entry` = 190549;
UPDATE `gameobject_template` SET `data0` = 296 WHERE `entry` = 190550;
UPDATE `gameobject_template` SET `data0` = 297 WHERE `entry` = 190552;

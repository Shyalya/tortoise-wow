-- F-27: ScriptName 0 is an invalid placeholder, not a registered script.
-- Preserve the table's other AI/script fields while removing the bogus
-- registry entry from every table inspected by ScriptMgr::LoadScriptNames.

UPDATE `creature_template`
SET `script_name` = ''
WHERE `script_name` = '0';

UPDATE `gameobject_template`
SET `script_name` = ''
WHERE `script_name` = '0';

UPDATE `item_template`
SET `script_name` = ''
WHERE `script_name` = '0';

UPDATE `spell_template`
SET `script_name` = ''
WHERE `script_name` = '0';

UPDATE `scripted_areatrigger`
SET `script_name` = ''
WHERE `script_name` = '0';

UPDATE `scripted_event_id`
SET `script_name` = ''
WHERE `script_name` = '0';

UPDATE `map_template`
SET `script_name` = ''
WHERE `script_name` = '0';

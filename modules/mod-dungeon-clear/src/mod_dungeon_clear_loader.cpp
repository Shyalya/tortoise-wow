/*
 * mod-dungeon-clear — module entry point.
 *
 * The function name must be Addmod_dungeon_clearScripts() so AzerothCore's
 * generated module script loader (derived from the directory name
 * "mod-dungeon-clear") calls it at startup.
 */

void AddSC_dungeon_clear_module();
void AddSC_dungeon_clear_command();
void AddSC_dungeon_clear_addon_hook();

void Addmod_dungeon_clearScripts()
{
    AddSC_dungeon_clear_module();
    AddSC_dungeon_clear_command();
    AddSC_dungeon_clear_addon_hook();
}

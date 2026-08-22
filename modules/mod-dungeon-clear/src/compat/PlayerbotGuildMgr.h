/*
 * Tortoise port: mod-playerbots' PlayerbotGuildMgr tracks which guilds were
 * created by the bot system. No such classifier exists on this tree, and
 * treating EVERY guild as real is the do-no-harm direction both callers want:
 * the test-pool picker then skips any guilded character outright, and the
 * auto-join cleanup only prints the classification into its log line.
 */
#ifndef MOD_DC_COMPAT_PLAYERBOTGUILDMGR_H
#define MOD_DC_COMPAT_PLAYERBOTGUILDMGR_H

#include "Platform/Define.h"

class PlayerbotGuildMgr
{
public:
    static PlayerbotGuildMgr& instance()
    {
        static PlayerbotGuildMgr mgr;
        return mgr;
    }

    bool IsRealGuild(uint32 /*guildId*/) const { return true; }
};

#endif

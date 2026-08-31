
#include "playerbot/playerbot.h"
#include "XpGainAction.h"
#include "playerbot/LootObjectStack.h"
#ifdef MANGOS
#endif



using namespace ai;

bool XpGainAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    RESET_AI_VALUE(uint32,"death count");

    WorldPacket p(event.getPacket()); // (8+4+1+4+8)
    ObjectGuid guid;
    uint32 xpgain;
    uint8 type = 0; // 00-kill_xp type, 01-non_kill_xp type
    uint32 givenXp = 0;
    float groupBonus = 0;

    p.rpos(0);
    p >> guid;      // 8 victim
    p >> xpgain;    // 1 given experience
    p >> type;      //1 00-kill_xp type, 01-non_kill_xp type

    if (sPlayerbotAIConfig.hasLog("bot_events.csv"))
    {
        sPlayerbotAIConfig.logEvent(ai, "XpGainAction", guid, std::to_string(xpgain));
    }

    int32 bonusXpgain = (int32)xpgain * (sPlayerbotAIConfig.playerbotsXPrate - 1.0f);

    std::ostringstream out;
    out << "Gained ";
    out << std::to_string(xpgain + bonusXpgain);
    out << " xp, current: ";
    out << std::to_string(bot->GetUInt32Value(PLAYER_XP) + bonusXpgain);
    out << "/";
    out << std::to_string(bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP));

    ai->TellDebug(requester, out.str(),"debug xp");
    
    AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
    ai->AccelerateRespawn(guid);

    Creature* creature = ai->GetCreature(guid);

    //if (creature && ((creature->IsElite() && !creature->GetMap()->IsDungeon()) || creature->IsWorldBoss() || creature->GetLevel() > DEFAULT_MAX_LEVEL + 1 || creature->GetLevel() > bot->GetLevel() + 4))
    if (creature && !creature->GetMap()->IsDungeon())
    {
        BroadcastHelper::BroadcastCreatureKill(ai, bot, creature);
    }

    if (!sRandomPlayerbotMgr.IsFreeBot(bot) || sPlayerbotAIConfig.playerbotsXPrate == 1)
        return false;

    if (!type)
    {
        p >> givenXp;      // 4 experience without rested bonus
        p >> groupBonus;   // 8 group bonus
    }

    Unit* victim;
    if (guid)
        victim = ai->GetUnit(guid);

    // Capture level-ups from both base XP (already applied by server) and bonus XP (applied below).
    // The server's Player::GiveXP fires before this action runs, so levelBefore may already be
    // higher than what we saw at packet receipt — compare after GiveXP to catch any remaining jumps.
    uint32 levelBefore = bot->GetLevel();
    GiveXP(bonusXpgain, victim);
    uint32 levelAfter = bot->GetLevel();

    if (levelAfter > levelBefore && levelAfter >= 5 &&
        sRandomPlayerbotMgr.IsRandomBot(bot) && !GetBotAI(bot)->HasRealPlayerMaster())
    {
        sLog.outBasic("Bot #%d <%s> levelled %d->%d (Execute hook), triggering gear update",
            bot->GetGUIDLow(), bot->GetName(), levelBefore, levelAfter);
        sRandomPlayerbotMgr.UpdateGearSpells(bot);
    }

    return false;
}

void XpGainAction::GiveXP(int32 xp, Unit* victim)
{
    if (!bot->IsAlive())
    {
        return;
    }

    uint32 level = bot->GetLevel();

    // No Eluna OnGiveXP hook here. This block came from the cmangos playerbots
    // port and sat dead for months because nothing defined ENABLE_ELUNA; the
    // Eluna merge defines it PUBLIC on `shared`, which woke it up. It cannot
    // compile as written: `sEluna` is a global from the older Eluna, and this
    // one keeps per-map states behind sElunaMgr instead. Removed rather than
    // ported - it never ran, and the hook belongs with the others if it is
    // wanted at all.

    // XP to money conversion processed in Player::RewardQuest
    if (level >= sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
    {
        return;
    }

    if (xp < 0)
    {
        uint32 curXP = bot->GetUInt32Value(PLAYER_XP);
        int32 newXP = (int32)curXP + xp;
        if (newXP < 0)
            newXP = 0;
        bot->SetUInt32Value(PLAYER_XP, newXP);
        return;
    }

    // XP resting bonus for kill
    uint32 rested_bonus_xp = victim ? bot->GetXPRestBonus(xp) : 0;

    //SendLogXPGain(xp, victim, rested_bonus_xp);

    uint32 curXP = bot->GetUInt32Value(PLAYER_XP);
    uint32 nextLvlXP = bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    uint32 newXP = curXP + xp + rested_bonus_xp;

    while (newXP >= nextLvlXP && level < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
    {
        newXP -= nextLvlXP;

        if (level < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        {
            bot->GiveLevel(level + 1);
        }

        level = bot->GetLevel();
        nextLvlXP = bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    }

    bot->SetUInt32Value(PLAYER_XP, newXP);
}
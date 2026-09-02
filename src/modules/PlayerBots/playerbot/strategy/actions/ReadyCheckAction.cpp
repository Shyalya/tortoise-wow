
#include "playerbot/playerbot.h"
#include "ReadyCheckAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

std::string formatPercent(std::string name, uint8 value, float percent)
{
    std::ostringstream out;

    std::string color;
    if (percent > 75)
        color = "|cff00ff00";
    else if (percent > 50)
        color = "|cffffff00";
    else
        color = "|cffff0000";

    out << "|cffffffff[" << name << "]" << color << "x" << (int)value;
    return out.str();
}

class ReadyChecker
{
public:
    virtual bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) = 0;
    virtual std::string GetName() = 0;
    virtual bool PrintAlways() { return true; }

    static std::list<ReadyChecker*> checkers;
};

std::list<ReadyChecker*> ReadyChecker::checkers;

class HealthChecker : public ReadyChecker
{
public:
    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        return AI_VALUE2(uint8, "health", "self target") > sPlayerbotAIConfig.almostFullHealth;
    }

    virtual std::string GetName() override { return "HP"; }
};

class ManaChecker : public ReadyChecker
{
public:
    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        return !AI_VALUE2(bool, "has mana", "self target") || AI_VALUE2(uint8, "mana", "self target") > sPlayerbotAIConfig.mediumHealth;
    }
    virtual std::string GetName() override { return "MP"; }
};

class DistanceChecker : public ReadyChecker
{
public:
    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        Player* bot = ai->GetBot();
        if (requester)
        {
            bool distance = sServerFacade.GetDistance2d(bot, requester) <= sPlayerbotAIConfig.sightDistance;
            if (!distance)
            {
                return false;
            }
        }

        return true;
    }

    virtual bool PrintAlways() override { return true; }
    virtual std::string GetName() override { return "Far away"; }
};

class DurabilityChecker : public ReadyChecker
{
public:
    bool Check(Player* requester, PlayerbotAI* ai, AiObjectContext* context) override
    {
        return AI_VALUE(uint8, "durability inventory") >= 50;
    }

    virtual std::string GetName() override { return "Durability"; }
};

class HunterChecker : public ReadyChecker
{
public:
    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        Player* bot = ai->GetBot();
        if (bot->getClass() == CLASS_HUNTER)
        {
            if (!bot->GetUInt32Value(PLAYER_AMMO_ID))
                return false;

            if (!bot->GetPet())
                return false;

            if (bot->GetPet()->GetHappinessState() == UNHAPPY)
                return false;
        }

        return true;
    }

    virtual bool PrintAlways() override { return true; }
    virtual std::string GetName() override { return "Hunter supplies"; }
};


class ItemCountChecker : public ReadyChecker
{
public:
    ItemCountChecker(std::string item, std::string name) { this->item = item; this->name = name; }

    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        return AI_VALUE2(uint32, "item count", item) > 0;
    }

    virtual std::string GetName() override { return name; }

private:
    std::string item, name;
};

class ManaPotionChecker : public ItemCountChecker
{
public:
    ManaPotionChecker(std::string item, std::string name) : ItemCountChecker(item, name) {}

    bool Check(Player* requester, PlayerbotAI *ai, AiObjectContext* context) override
    {
        return !AI_VALUE2(bool, "has mana", "self target") || ItemCountChecker::Check(requester, ai, context);
    }
};

bool ReadyCheckAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    WorldPacket p = event.getPacket();
    ObjectGuid player;
    p.rpos(0);
    bool incomingReadyCheck = !p.empty();
    if (incomingReadyCheck)
    {
        p >> player;
        if (player == bot->GetObjectGuid())
            return false;
    }

    if (incomingReadyCheck && sPlayerbotAIConfig.forceRebuffOnReadyCheck &&
        !bot->IsInCombat() && ai->HasStrategy("force rebuff", BotState::BOT_STATE_NON_COMBAT))
        return ReadyCheck(requester, true);

    return ReadyCheck(requester);
}

bool ReadyCheckAction::ReadyCheck(Player* requester, bool deferForRebuff)
{
    if (ReadyChecker::checkers.empty())
    {
        ReadyChecker::checkers.push_back(new HealthChecker());
        ReadyChecker::checkers.push_back(new ManaChecker());
        ReadyChecker::checkers.push_back(new DistanceChecker());
        ReadyChecker::checkers.push_back(new DurabilityChecker());
        ReadyChecker::checkers.push_back(new HunterChecker());

        ReadyChecker::checkers.push_back(new ItemCountChecker("food", "Food"));
        ReadyChecker::checkers.push_back(new ManaPotionChecker("drink", "Water"));
        ReadyChecker::checkers.push_back(new ItemCountChecker("healing potion", "Hpot"));
        ReadyChecker::checkers.push_back(new ManaPotionChecker("mana potion", "Mpot"));
    }

    bool startedPreparation = false;
    if (AI_VALUE2(uint8, "health", "self target") <= sPlayerbotAIConfig.almostFullHealth)
        startedPreparation |= ai->DoSpecificAction("food", Event("ready check", "", requester), true);
    if (AI_VALUE2(bool, "has mana", "self target") &&
        AI_VALUE2(uint8, "mana", "self target") <= sPlayerbotAIConfig.mediumHealth)
        startedPreparation |= ai->DoSpecificAction("drink", Event("ready check", "", requester), true);

    if (startedPreparation && !deferForRebuff && !bot->IsInCombat())
    {
        // Reuse the existing deferred reply path. ReadyReplyAction will
        // re-enter this action after the item-use cooldown and re-evaluate.
        ai->BeginForceRebuff(true);
        return true;
    }

    bool result = true;
    std::vector<std::string> blockers;
    for (std::list<ReadyChecker*>::iterator i = ReadyChecker::checkers.begin(); i != ReadyChecker::checkers.end(); ++i)
    {
        ReadyChecker* checker = *i;
        bool ok = checker->Check(requester, ai, context);
        result = result && ok;
        if (!ok)
            blockers.push_back(checker->GetName());
    }

    std::ostringstream out;
    if (!blockers.empty())
    {
        out << "Not ready: ";
        for (size_t n = 0; n < blockers.size(); ++n)
            out << (n ? ", " : "") << blockers[n];
    }

    if (!blockers.empty())
        ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    if (deferForRebuff)
    {
        ai->BeginForceRebuff(true);
        return true;
    }

    WorldPacket packet(MSG_RAID_READY_CHECK);
    // The server expects the answer, not merely an acknowledgement.  Sending
    // ready unconditionally made bots report ready while missing supplies or
    // being out of range.
    packet << uint8(result ? 1 : 0);
    bot->GetSession()->HandleRaidReadyCheckOpcode(packet);

    ai->ChangeStrategy("-ready check", BotState::BOT_STATE_NON_COMBAT);

    return true;
}

bool FinishReadyCheckAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    return ReadyCheck(requester);
}

bool ForceRebuffAction::Execute(Event& event)
{
    if (bot->IsInCombat() || !ai->HasStrategy("force rebuff", BotState::BOT_STATE_NON_COMBAT))
        return false;

    ai->BeginForceRebuff(false);
    return true;
}

bool ReadyReplyAction::isUseful()
{
    if (!ai->IsForceRebuffPending() || bot->IsInCombat())
        return false;

    if (bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) || bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        return false;

    return ai->IsForceRebuffExpired() || !ai->HasForceRebuffBuffWorkThisCycle();
}

bool ReadyReplyAction::Execute(Event& event)
{
    if (!isUseful())
        return false;

    bool shouldReply = ai->ShouldReplyToReadyCheck();
    // End the completed rebuff cycle before re-entering ReadyCheck.  The
    // latter may start a new deferred cycle for food/drink preparation; doing
    // this afterwards would cancel that newly-created cycle.
    ai->EndForceRebuff();
    if (shouldReply)
        ai->DoSpecificAction("ready check finished", Event("ready check", "", ai->GetMaster()), true);
    SetDuration(sPlayerbotAIConfig.globalCoolDown);
    return true;
}

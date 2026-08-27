
#include "playerbot/playerbot.h"
#include <stdarg.h>
#include <iomanip>

#include "Engine.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PerformanceMonitor.h"
#include "playerbot/BotActionLog.h"

#ifdef BUILD_ELUNA
#include "LuaEngine/LuaEngine.h"
#endif

using namespace ai;

Engine::Engine(PlayerbotAI* ai, AiObjectContext *factory, BotState state) : PlayerbotAIAware(ai), aiObjectContext(factory), state(state)
{
    lastRelevance = 0.0f;
    testMode = false;
    lastExecutedAction = nullptr;
}

bool ActionExecutionListeners::Before(Action* action, const Event& event)
{
    bool result = true;
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i!=listeners.end(); i++)
    {
        result &= (*i)->Before(action, event);
    }
    return result;
}

void ActionExecutionListeners::After(Action* action, bool executed, const Event& event)
{
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i!=listeners.end(); i++)
    {
        (*i)->After(action, executed, event);
    }
}

bool ActionExecutionListeners::OverrideResult(Action* action, bool executed, const Event& event)
{
    bool result = executed;
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i!=listeners.end(); i++)
    {
        result = (*i)->OverrideResult(action, result, event);
    }
    return result;
}

bool ActionExecutionListeners::AllowExecution(Action* action, const Event& event)
{
    bool result = true;
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i!=listeners.end(); i++)
    {
        result &= (*i)->AllowExecution(action, event);
    }
    return result;
}

ActionExecutionListeners::~ActionExecutionListeners()
{
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i!=listeners.end(); i++)
    {
        delete *i;
    }
    listeners.clear();
}


Engine::~Engine(void)
{
    Reset();

    strategies.clear();
}

bool Engine::Reset()
{
    // Re-entrancy guard. This drains and deletes every ActionBasket in `queue`.
    // An action's Execute() is allowed to change the strategy set - BGTactics
    // does it with ChangeStrategy("-buff") / ("-collision") / ("-arena"), and
    // PlayerbotAI::ResetStrategies does it too - and every one of those paths
    // lands in Init() -> Reset() while DoNextAction's do/while walk over the
    // same queue is still running. Draining there ended the tick on the spot
    // ("no actions executed") and freed baskets the walk still owned.
    //
    // So while a walk is in progress we do not touch the queue: we record that
    // a re-init is owed and DoNextAction performs it once the walk has ended.
    // Callers must treat a false return as "deferred, nothing was cleared".
    if (inDoNextAction)
    {
        reinitPending = true;
        // Deliberately greppable: this is the only in-world evidence that the
        // deferral fired. A tick that logs it must go on to log another
        // "A:<action> - OK" rather than "no actions executed".
        LogAction("S:reinit deferred");
        return false;
    }

    ActionNode* action = NULL;
    do
    {
        action = queue.Pop();
        if (!action) break;
        delete action;
    } while (true);

    for (std::list<TriggerNode*>::iterator i = triggers.begin(); i != triggers.end(); i++)
    {
        TriggerNode* trigger = *i;
        delete trigger;
    }
    triggers.clear();

    for (std::list<Multiplier*>::iterator i = multipliers.begin(); i != multipliers.end(); i++)
    {
        Multiplier* multiplier = *i;
        delete multiplier;
    }
    multipliers.clear();

    return true;
}

void Engine::Init()
{
    // Deferred: the queue belongs to an in-progress DoNextAction walk, which
    // will call Init() again as soon as it finishes.
    if (!Reset())
        return;

    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        Strategy* strategy = i->second;
        strategy->InitMultipliers(multipliers, state);
        strategy->InitTriggers(triggers, state);
        MultiplyAndPush(strategy->getDefaultActions(state), 0.0f, false, Event(), "default");
    }

	if (testMode)
	{
        FILE* file = fopen("test.log", "w");
        fprintf(file, "\n");
        fclose(file);
	}
}

bool Engine::DoNextAction(Unit* unit, int depth, bool minimal, bool isStunned)
{
    LogAction("--- AI Tick ---");
    if (sPlayerbotAIConfig.logValuesPerTick)
        LogValues();

    bool actionExecuted = false;

    // A strategy change issued from inside an action's Execute() must not drop
    // the queue while we are walking it; Reset() parks the re-init instead and
    // we run it below, after the walk.
    bool const wasInDoNextAction = inDoNextAction;
    inDoNextAction = true;

    time_t currentTime = time(0);
    aiObjectContext->Update();
    ProcessTriggers(minimal);
    PushDefaultActions();

    std::vector<Action*> modifiedActions;

    int iterations = 0;
    int iterationsPerTick = queue.Size() * (minimal ? (uint32)(sPlayerbotAIConfig.iterationsPerTick / 2) : sPlayerbotAIConfig.iterationsPerTick);
    bool hasBasket = false;
    do 
    {
        float relevance = 0.0f, oldRelevance = 0.0f; // just for reference
        bool skipPrerequisites = false;
        Event event;
        ActionNode* actionNode = NULL;
        bool popped = false;

        {
            // `basket` lives only inside this block, and the block ends at the
            // Pop that frees it. Everything the rest of the iteration needs is
            // copied out first, so no code past the Pop can name the freed
            // basket at all - it is a compile error, not a use-after-free.
            // `hasBasket` carries the only thing the loop condition needs.
            ActionBasket* basket = queue.Peek();
            hasBasket = (basket != NULL);
            if (basket)
            {
                relevance = basket->getRelevance();
                oldRelevance = relevance;
                skipPrerequisites = basket->isSkipPrerequisites();
                event = basket->getEvent();
                if (minimal && (relevance < 100))
                    continue;

                // NOTE: queue.Pop() deletes basket
                actionNode = queue.Pop();
                popped = true;
            }
        }

        if (popped)
        {
            Action* action = InitializeAction(actionNode);

            std::string actionName = (action ? action->getName() : "unknown");
            if (!event.getSource().empty())
                actionName += " <" + event.getSource() + ">";
            
            auto pmo1 = sPerformanceMonitor.start(PERF_MON_ACTION, actionName, ai);

            if(action)
                action->setRelevance(relevance);

            if (!action)
            {
                if (sPlayerbotAIConfig.CanLogAction(ai, actionNode->getName(), false, ""))
                {
                    std::ostringstream out;
                    out << "try: ";
                    out << actionNode->getName();
                    out << " unknown (";

                    out << std::fixed << std::setprecision(3);
                    out << relevance << ")";

                    if (!event.getSource().empty())
                        out << " [" << event.getSource() << "]";

                    if (ai->GetMaster())
                    {
                        ai->TellPlayerNoFacing(ai->GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
                    }
                    else
                    {
                        ai->GetBot()->Say(out.str(), (ai->GetBot()->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                    }
                }
                LogAction("A:%s - UNKNOWN", actionNode->getName().c_str());
            }
            else
            {
                bool isUseful = false;
                if (!isStunned || action->isUsefulWhenStunned())
                {
                    auto pmo2 = sPerformanceMonitor.start(PERF_MON_ACTION, "isUseful", ai);
                    isUseful = action->isUseful();
                    pmo2.reset();
                }

                if (isUseful)
                {
                    if (std::find(modifiedActions.begin(), modifiedActions.end(), action) == modifiedActions.end())
                    {
                        for (std::list<Multiplier*>::iterator i = multipliers.begin(); i != multipliers.end(); i++)
                        {
                            Multiplier* multiplier = *i;
                            relevance *= multiplier->GetValue(action);

                            action->setRelevance(relevance);
                            if (!relevance)
                            {
                                LogAction("Multiplier %s made action %s useless", multiplier->getName().c_str(), action->getName().c_str());
                                break;
                            }
                        }
                    }

                    ActionBasket* peekAction = queue.Peek();
                    if (relevance < oldRelevance && peekAction && peekAction->getRelevance() > relevance) //Relevance changed. Try again.
                    {
                        modifiedActions.push_back(action);
                        PushAgain(actionNode, relevance, event);
                        continue;
                    }

                    if (!skipPrerequisites)
                    {
                        LogAction("A:%s - PREREQ", action->getName().c_str());
                        if (MultiplyAndPush(actionNode->getPrerequisites(), relevance + 0.02, false, event, "prereq"))
                        {
                            PushAgain(actionNode, relevance + 0.01, event);
                            continue;
                        }
                    }

                    auto pmo3 = sPerformanceMonitor.start(PERF_MON_ACTION, "isPossible", ai);
                    bool isPossible = action->isPossible();
                    pmo3.reset();

                    if (isPossible && relevance)
                    {
                        auto pmo4 = sPerformanceMonitor.start(PERF_MON_ACTION, "Execute", ai);
                        actionExecuted = ListenAndExecute(action, event);
                        pmo4.reset();

#ifdef PLAYERBOT_ELUNA
                        // used by eluna    
                        if (Eluna* e = ai->GetBot()->GetEluna())
                            e->OnActionExecute(ai, action->getName(), actionExecuted);
#endif

                        if (actionExecuted)
                        {
                            LogAction("A:%s - OK", action->getName().c_str());
                            MultiplyAndPush(actionNode->getContinuers(), 0, false, event, "cont");
                            lastRelevance = relevance;
                            delete actionNode;
                            break;
                        }
                        else
                        {
                            LogAction("A:%s - FAILED", action->getName().c_str());
                            MultiplyAndPush(actionNode->getAlternatives(), relevance + 0.03, false, event, "alt");
                        }
                    }
                    else
                    {
                        if (sPlayerbotAIConfig.CanLogAction(ai,actionNode->getName(), false, ""))
                        {
                            std::ostringstream out;
                            out << "try: ";
                            out << action->getName();
                            out << " impossible (";

                            out << std::fixed << std::setprecision(3);
                            out << action->getRelevance() << ")";

                            if (!event.getSource().empty())
                                out << " [" << event.getSource() << "]";

        if (ai->GetMaster())
                            {
                                ai->TellPlayerNoFacing(ai->GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
                            }
                            else
                            {
                                ai->GetBot()->Say(out.str(), (ai->GetBot()->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                            }
                        }
                        LogAction("A:%s - IMPOSSIBLE", action->getName().c_str());
                        MultiplyAndPush(actionNode->getAlternatives(), relevance + 0.03, false, event, "alt");
                    }
                }
                else
                {
                    if (sPlayerbotAIConfig.CanLogAction(ai,actionNode->getName(), false, ""))
                    {
                        std::ostringstream out;
                        out << "try: ";
                        out << action->getName();
                        out << " useless (";

                        out << std::fixed << std::setprecision(3);
                        out << action->getRelevance() << ")";

                        if (!event.getSource().empty())
                            out << " [" << event.getSource() << "]";

        if (ai->GetMaster())
                        {
                            ai->TellPlayerNoFacing(ai->GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
                        }
                        else
                        {
                            ai->GetBot()->Say(out.str(), (ai->GetBot()->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                        }
                    }
                    lastRelevance = relevance;
                    LogAction("A:%s - USELESS", action->getName().c_str());
                }
            }
            delete actionNode;
        }
    }
    while (hasBasket && ++iterations <= iterationsPerTick);

    /*
    if (!basket)
    {
        lastRelevance = 0.0f;
        PushDefaultActions();
        if (queue.Peek() && depth < 2)
            return DoNextAction(unit, depth + 1, minimal, isStunned);
    }
    */

    // MEMORY FIX TEST
 /*   do {
        basket = queue.Peek();
        if (basket) {
            // NOTE: queue.Pop() deletes basket
            delete queue.Pop();
        }
    } while (basket);*/

    if (time(0) - currentTime > 1) {
        LogAction("too long execution");
    }

    if (!actionExecuted)
        LogAction("no actions executed");

    queue.RemoveExpired();

    inDoNextAction = wasInDoNextAction;
    if (!inDoNextAction && reinitPending)
    {
        reinitPending = false;
        Init();
    }

    return actionExecuted;
}

ActionNode* Engine::CreateActionNode(const std::string& name)
{
    ActionNode* actionNode = nullptr;
    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        Strategy* strategy = i->second;
        actionNode = strategy->GetAction(name);
        if (actionNode)
        {
            break;
        }
    }

    if (!actionNode)
    {
        actionNode = new ActionNode(name);
    }

    return actionNode;
}

bool Engine::MultiplyAndPush(NextAction** actions, float forceRelevance, bool skipPrerequisites, const Event& event, const char* pushType)
{
    bool pushed = false;
    if (actions)
    {
        for (int j=0; actions[j]; j++)
        {
            NextAction* nextAction = actions[j];
            if (nextAction)
            {
                ActionNode* actionNode = CreateActionNode(nextAction->getName());
                InitializeAction(actionNode);

                bool shouldPush = false;
                float k = nextAction->getRelevance();
                if (forceRelevance > 0.0f)
                {
                    k = forceRelevance;
                }
                else if (strcmp(pushType, "default") == 0)
                {
                    k -= 200.0f;
                    shouldPush = true;
                }
                else if (strcmp(pushType, "prereq") == 0 || strcmp(pushType, "alt") == 0 || strcmp(pushType, "again") == 0)
                {
                    k = forceRelevance;
                    shouldPush = true;
                }

                if (!shouldPush)
                {
                    shouldPush = k > 0.0f;
                }

                if (shouldPush)
                {
                    LogAction("PUSH:%s - %f (%s)", actionNode->getName().c_str(), k, pushType);
                    queue.Push(new ActionBasket(actionNode, k, skipPrerequisites, event));
                    pushed = true;
                }
                else
                {
                    delete actionNode;
                }

                delete nextAction;
            }
            else
                break;
        }
        delete[] actions;
    }
    return pushed;
}

ActionResult Engine::ExecuteAction(const std::string& name, Event& event)
{
    ActionResult actionResult = ACTION_RESULT_UNKNOWN;
    ActionNode* actionNode = CreateActionNode(name);
    if (actionNode)
    {
        auto pmo1 = sPerformanceMonitor.start(PERF_MON_ACTION, name, ai);
        Action* action = InitializeAction(actionNode);
        if (action)
        {
            auto pmo2 = sPerformanceMonitor.start(PERF_MON_ACTION, "isUseful", ai);
            bool isUseful = action->isUseful();
            pmo2.reset();
            
            if (isUseful)
            {
                auto pmo3 = sPerformanceMonitor.start(PERF_MON_ACTION, "isPossible", ai);
                bool isPossible = action->isPossible();
                pmo3.reset();

                if (isPossible)
                {
                    action->MakeVerbose(event.getOwner() != nullptr);
                    auto pmo4 = sPerformanceMonitor.start(PERF_MON_ACTION, "Execute", ai);
                    bool executionResult = ListenAndExecute(action, event);
                    pmo4.reset();

                    MultiplyAndPush(action->getContinuers(), 0.0f, false, event, "default");
                    actionResult = executionResult ? ACTION_RESULT_OK : ACTION_RESULT_FAILED;
                }
                else
                {
                    actionResult = ACTION_RESULT_IMPOSSIBLE;
                }
            }
            else
            {
                actionResult = ACTION_RESULT_USELESS;
            }
        }
        delete actionNode;
    }

    return actionResult;
}

bool Engine::CanExecuteAction(const std::string& name, bool isUseful, bool isPossible)
{
    bool result = true;
    ActionNode* actionNode = CreateActionNode(name);
    if (actionNode)
    {
        Action* action = InitializeAction(actionNode);
        if (action)
        {
            if (isUseful)
            {
                result &= action->isUseful();
            }

            if (isPossible)
            {
                result &= action->isPossible();
            }
        }

        delete actionNode;
    }

    return result;
}

void Engine::addStrategy(const std::string& name)
{
    uint64 const tokenBefore = StrategySetToken();

    // The second argument means "rebuild now", and initMode means the opposite
    // - hold rebuilds back until the bulk change is done - so passing one as
    // the other had it exactly backwards. Neither removal needs its own
    // rebuild: they belong to this add, which rebuilds once at the end.
    removeStrategy(name, false);

    Strategy* strategy = aiObjectContext->GetStrategy(name);
    if (strategy)
    {
        std::set<std::string> siblings = aiObjectContext->GetSiblingStrategy(name);
        for (std::set<std::string>::iterator i = siblings.begin(); i != siblings.end(); i++)
        {
            removeStrategy(*i, false);
        }

        LogAction("S:+%s", strategy->getName().c_str());
        if (strategies.insert(std::make_pair(strategy->getName(), strategy)).second)
            strategiesHash ^= StrategyNameHash(strategy->getName());
        strategy->OnStrategyAdded(state);
    }

    // Init() empties the action queue, so only pay it when the strategy set
    // actually moved. Re-adding a strategy the engine already carries used to
    // wipe the queue for nothing.
    if (!initMode && StrategySetToken() != tokenBefore)
    {
        Init();
    }
}

void Engine::addStrategies(std::string first, ...)
{
	addStrategy(first);

	va_list vl;
	va_start(vl, first);

	const char* cur;
	do
	{
		cur = va_arg(vl, const char*);
		if (cur)
			addStrategy(cur);
	}
	while (cur);

	va_end(vl);
}

bool Engine::removeStrategy(const std::string& name, bool init)
{
    std::map<std::string, Strategy*>::iterator i = strategies.find(name);
    if (i == strategies.end())
        return false;

    LogAction("S:-%s", name.c_str());

    // DETACH FIRST, notify second. OnStrategyRemoved is allowed to change the
    // strategy set - RpgStrategy's removes "rpg bg" - and that invalidates the
    // iterator we are holding. The old order then read i->first and erased a
    // node that no longer existed, which tears the tree apart on rebalance
    // (SIGABRT in std::_Rb_tree_rebalance_for_erase, reached through
    // ChangeStrategy from inside OnStrategyRemoved).
    //
    // Notifying afterwards also matches what the name promises: by the time a
    // strategy hears it was removed, it is removed. The map holds raw pointers
    // and does not own the strategies (removeAllStrategies only clear()s), so
    // the pointer stays good across the erase.
    Strategy* const removed = i->second;
    strategiesHash ^= StrategyNameHash(i->first);
    strategies.erase(i);

    if (removed)
        removed->OnStrategyRemoved(state);

    if (init)
    {
        Init();
    }
    
    return true;
}

void Engine::removeAllStrategies()
{
    strategies.clear();
    strategiesHash = 0;
    Init();
}

void Engine::toggleStrategy(const std::string& name)
{
    if (!removeStrategy(name, !initMode))
        addStrategy(name);
}

bool Engine::HasStrategy(const std::string& name)
{
    return strategies.find(name) != strategies.end();
}

Strategy* Engine::GetStrategy(const std::string& name) const
{
    auto i = strategies.find(name);
    if (i != strategies.end())
    {
        return i->second;
    }

    return nullptr;
}

void Engine::ProcessTriggers(bool minimal)
{
    for (std::list<TriggerNode*>::iterator i = triggers.begin(); i != triggers.end(); i++)
    {
        TriggerNode* node = *i;
        if (!node)
            continue;

        Trigger* trigger = node->getTrigger();
        if (!trigger)
        {
            trigger = aiObjectContext->GetTrigger(node->getName());
            node->setTrigger(trigger);
        }
        if (!trigger)
            continue;

        if (testMode || trigger->IsAlreadyTriggered() || trigger->needCheck())
        {
            if (minimal && node->getFirstRelevance() < 100)
                continue;
            auto pmo = sPerformanceMonitor.start(PERF_MON_TRIGGER, trigger->getName(), ai);
            Event event = trigger->Check();

#ifdef PLAYERBOT_ELUNA
            // used by eluna    
            if (Eluna* e = ai->GetBot()->GetEluna())
                e->OnTriggerCheck(ai, trigger->getName(), !event ? false : true);
#endif

            if (!event)
                continue;

            MultiplyAndPush(node->getHandlers(), 0.0f, false, event, "trigger");
            LogAction("T:%s", trigger->getName().c_str());
        }
    }

    for (std::list<TriggerNode*>::iterator i = triggers.begin(); i != triggers.end(); i++)
    {
        Trigger* trigger = (*i)->getTrigger();
        if (trigger) trigger->Reset();
    }
}

void Engine::PushDefaultActions()
{
    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        Strategy* strategy = i->second;
        MultiplyAndPush(strategy->getDefaultActions(state), 0.0f, false, Event(), "default");
    }
}

std::string Engine::ListStrategies()
{   
    std::string s;
    if (strategies.empty())
        return s;

    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        s.append(i->first);
        s.append(", ");
    }
    return s.substr(0, s.length() - 2);
}

std::list<std::string_view> Engine::GetStrategies()
{
    std::list<std::string_view> result;
    for (const auto& strategy : strategies)
    {
        result.push_back(strategy.first);
    }
    return result;
}

void Engine::PushAgain(ActionNode* actionNode, float relevance, const Event& event)
{
    NextAction** nextAction = new NextAction*[2];
    nextAction[0] = new NextAction(actionNode->getName(), relevance);
    nextAction[1] = NULL;
    MultiplyAndPush(nextAction, relevance, true, event, "again");
    delete actionNode;
}

bool Engine::ContainsStrategy(StrategyType type)
{
	for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
	{
		Strategy* strategy = i->second;
		if (strategy->GetType() & type)
			return true;
	}
	return false;
}

Action* Engine::InitializeAction(ActionNode* actionNode)
{
    Action* action = actionNode->getAction();
    if (!action)
    {
        action = aiObjectContext->GetAction(actionNode->getName());
        actionNode->setAction(action);
    }

    if (action)
    {
        action->SetReaction(false);
    }

    return action;
}

bool Engine::ListenAndExecute(Action* action, Event& event)
{
    bool actionExecuted = false;
    Action* prevExecutedAction = lastExecutedAction;
    if (actionExecutionListeners.Before(action, event))
    {
        ai->SetLastEvent(event);
        actionExecuted = actionExecutionListeners.AllowExecution(action, event) ? action->Execute(event) : true;
        if (actionExecuted)
        {
            ai->SetActionDuration(action);
            lastExecutedAction = action;
        }
    }

    std::string lastActionName = prevExecutedAction ? prevExecutedAction->getName() : "";
    if (sPlayerbotAIConfig.CanLogAction(ai, action->getName(), true, lastActionName))
    {
        std::ostringstream out;
        out << "do: ";
        out << action->getName();
        if (actionExecuted)
            out << " 1 (";
        else
            out << " 0 (";

        out << std::fixed << std::setprecision(2);
        out << action->getRelevance() << ")";

        if(!event.getSource().empty())
            out << " [" << event.getSource() << "]";

        if (actionExecuted)
        {
            const uint32 actionDuration = action->GetDuration();
            if (actionDuration > 0)
            {
                out << " (duration: " << ((float)actionDuration / static_cast<float>(IN_MILLISECONDS)) << "s)";
            }
        }

        if (ai->GetMaster())
        {
            ai->TellPlayerNoFacing(ai->GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
        }
        else
        {
            ai->GetBot()->Say(out.str(), (ai->GetBot()->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
        }
    }

    if (ai->HasStrategy("debug threat", BotState::BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        AiObjectContext* context = ai->GetAiObjectContext();

        float deltaThreat = LOG_AI_VALUE(float, "my threat::current target")->GetDelta(5.0f);

        float currentThreat = AI_VALUE2(float, "my threat", "current target");
        float tankThreat = AI_VALUE2(float, "tank threat", "current target");
        float relThreat = AI_VALUE2(uint8, "threat", "current target");

        out << "threat: " << int32(currentThreat)<< "+" << int32(deltaThreat) << " / " << int32(tankThreat) << " ||| " << relThreat;

        ai->TellPlayerNoFacing(ai->GetMaster(), out);
    }

    actionExecuted = actionExecutionListeners.OverrideResult(action, actionExecuted, event);
    actionExecutionListeners.After(action, actionExecuted, event);
    return actionExecuted;
}

void Engine::LogAction(const char* format, ...)
{
    char buf[1024];

    va_list ap;
    va_start(ap, format);
    vsprintf(buf, format, ap);
    va_end(ap);
    lastAction += "|";
    lastAction += buf;
    if (lastAction.size() > 512)
    {
        lastAction = lastAction.substr(512);
        size_t pos = lastAction.find("|");
        lastAction = (pos == std::string::npos ? "" : lastAction.substr(pos));
    }

    if (testMode)
    {
        FILE* file = fopen("test.log", "a");
        fprintf(file, "%s",buf);
        fprintf(file, "\n");
        fclose(file);
    }
    else
    {
        Player* bot = ai->GetBot();
        if (sPlayerbotAIConfig.logInGroupOnly && !bot->GetGroup())
            return;

        sLog.outDetail( "%s %s", bot->GetName(), buf);

        // BotActionLog tee: every PUSH/A/Tick line also lands in the bot's
        // per-bot file under logs/bots/ when AiPlayerbot.EnableActionLog=1.
        // Tag heuristic extracts the first colon-prefix from `buf` so the
        // per-bot log gets useful tags (PUSH / A / T / etc.) instead of
        // a single "ACTION".
        const char* tag = "ENGINE";
        if (strncmp(buf, "PUSH:", 5) == 0)             tag = "PUSH";
        else if (strncmp(buf, "A:", 2) == 0)           tag = "ACTION";
        else if (strncmp(buf, "T:", 2) == 0)           tag = "TRIGGER_REASON";
        else if (strncmp(buf, "--- AI Tick", 11) == 0) tag = "TICK";
        else if (strncmp(buf, "no actions", 10) == 0)  tag = "NO_ACTION";
        ai::botdiag::BotActionLog::Write(ai, tag, "%s", buf);
    }
}

void Engine::ChangeStrategy(const std::string& names)
{
    std::vector<std::string> splitted = split(names, ',');

    // Each entry would otherwise rebuild every strategy's triggers, although
    // only the set left at the end matters. Hold the rebuilds back for the
    // whole list and do one afterwards - the same thing
    // PlayerbotAI::ResetStrategies does around its bulk change.
    uint64 const tokenBefore = StrategySetToken();

    bool const wasInitMode = initMode;
    initMode = true;

    for (std::vector<std::string>::iterator i = splitted.begin(); i != splitted.end(); i++)
    {
        const char* name = i->c_str();
        switch (name[0])
        {
            case '+':
            {
                addStrategy(name+1);
                break;
            }
            case '-':
            {
                removeStrategy(name+1, false);
                break;
            }
            case '~':
            {
                toggleStrategy(name+1);
                break;
            }
        }
    }

    initMode = wasInitMode;

    // Caller is in a bulk change of its own - it will rebuild when it is done.
    //
    // The set-token guard is what keeps a no-op change cheap; it is no longer
    // what keeps a real one safe - Reset() now defers while DoNextAction owns
    // the queue. Before both, Init() -> Reset() drained `queue` outright, so a
    // ChangeStrategy issued from inside an action's Execute() destroyed every
    // basket DoNextAction had not popped yet and the walk exited on a null
    // Peek(). BGTactics
    // fires exactly that: `ai->ChangeStrategy("-buff", BOT_STATE_NON_COMBAT)`
    // (BattleGroundTactics.cpp:2716-2718) on every tick of a battleground in
    // progress, whether or not "buff" is still attached. In WSG that ran on the
    // relevance-70 `bg check flag` action and killed the rest of the tick, so
    // `bg move to objective` at relevance 1.0 was queued 23,908 times and
    // popped none.
    if (!initMode && StrategySetToken() != tokenBefore)
        Init();
}

void Engine::PrintStrategies(Player* requester, const std::string& engineType)
{
    std::string engineStrategies = engineType;
    engineStrategies.append(" Strategies: ");
    engineStrategies.append(ListStrategies());
    ai->TellPlayer(requester, engineStrategies, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, true);
}

void Engine::LogValues()
{
    if (testMode)
        return;

    Player* bot = ai->GetBot();
    if (sPlayerbotAIConfig.logInGroupOnly && !bot->GetGroup())
        return;

    std::string text = ai->GetAiObjectContext()->FormatValues();
    sLog.outDebug( "Values for %s: %s", bot->GetName(), text.c_str());
}
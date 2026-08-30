
#include "playerbot/playerbot.h"

#include "ReactionEngine.h"
#include <iomanip>

using namespace ai;

void Reaction::SetAction(Action* inAction)
{
    if (inAction)
    {
        SetDuration(inAction->GetDuration());
        action = inAction;
    }
}

bool Reaction::Update(uint32 elapsed)
{
    // Update remaining duration
    duration = duration > elapsed ? duration - elapsed : 0;

    // TO DO: Check if the reaction got interrupted (stun, knockback, ...)
    // ...

    // Return true when the reaction has finished
    return !IsActive();
}

ReactionEngine::ReactionEngine(PlayerbotAI* ai, AiObjectContext* factory, BotState engineState) 
: Engine(ai, factory, engineState)
, aiReactionUpdateDelay(0U)
{
    
}

bool ReactionEngine::FindReaction(bool isStunned)
{
    // Don't find a new reaction if the previous reaction is still running
    if(!IsReacting())
    {
        aiObjectContext->Update();

        ai->HandleCommands();

        // This will populate the queue to be processed with the reactions that can be triggered
        ProcessTriggers(false);

        ActionBasket* reactionItem = NULL;

        // Look for the best reaction (if any available)
        int iterations = 0;
        int iterationsPerTick = queue.Size() *  sPlayerbotAIConfig.iterationsPerTick;
        do 
        {
            // Get the best reaction in the queue (sorted by relevance)
            reactionItem = queue.Peek();
            if (reactionItem)
            {
                const bool skipReactionPrerequisites = reactionItem->isSkipPrerequisites();
                float reactionRelevance = reactionItem->getRelevance();
                const Event& reactionEvent = reactionItem->getEvent();

                // Extract the reaction from the queue (removed)
                ActionNode* reactionNode = queue.Pop(reactionItem);
                if (reactionNode)
                {
                    Action* reaction = InitializeAction(reactionNode);
                    if (reaction)
                    {
                        // Update the reaction relevance
                        reaction->setRelevance(reactionRelevance);

                        // Check if the reaction is useful
                        if (reaction->isUseful() && (!isStunned || reaction->isUsefulWhenStunned()))
                        {
                            // Process the multipliers
                            for (std::list<Multiplier*>::iterator i = multipliers.begin(); i != multipliers.end(); i++)
                            {
                                reactionRelevance *= (*i)->GetValue(reaction);
                                reaction->setRelevance(reactionRelevance);
                                if (reactionRelevance <= 0.0f)
                                {
                                    // Multiplier made reaction useless
                                    break;
                                }
                            }

                            // Process prerequisites
                            if (!skipReactionPrerequisites)
                            {
                                // Add the prerequisites to the queue with a slight higher relevance than this action to be processed in the next iteration
                                if (MultiplyAndPush(reactionNode->getPrerequisites(), reactionRelevance + 0.02, false, reactionEvent, "prereq"))
                                {
                                    // Add this reaction to the queue again to be processed after the prerequisite
                                    PushAgain(reactionNode, reactionRelevance + 0.01, reactionEvent);
                                    continue;
                                }
                            }

                            // Check if the reaction is possible
                            if ((reactionRelevance > 0.0f) && reaction->isPossible())
                            {
                                // Reaction found
                                incomingReaction.SetAction(reaction);
                                incomingReaction.SetEvent(reactionEvent);
                                delete reactionNode;
                                break;
                            }
                            else
                            {
                                // Add the alternative reactions to the queue
                                MultiplyAndPush(reactionNode->getAlternatives(), reactionRelevance + 0.03, false, reactionEvent, "alt");
                            }
                        }
                    }

                    // Delete the reaction node
                    delete reactionNode;
                }
            }
        }
        while (reactionItem && ++iterations <= iterationsPerTick);

        // Remove the expired reactions
        queue.RemoveExpired();

        return incomingReaction.IsValid();
    }

    return false;
}

bool ReactionEngine::StartReaction(bool minimal)
{
    // The action and its listeners may synchronously cause another AI update.
    // Keep the selected reaction claimed for the whole start sequence so a
    // nested update cannot execute it twice or fall through to the normal
    // engine while the outer reaction is still being started.
    if (reactionStartInProgress || !incomingReaction.IsValid())
        return false;

    reactionStartInProgress = true;

    if (incomingReaction.ShouldInterruptCast())
        ai->InterruptSpell();

    if (incomingReaction.ShouldInterruptMovement())
        ai->StopMoving();

    bool reactionExecuted = false;
    // Execute the incoming reaction.
    reactionExecuted = ListenAndExecute(incomingReaction.GetAction(), incomingReaction.GetEvent());
    if (reactionExecuted)
    {
        // Move the incoming reaction to the ongoing reaction
        ongoingReaction = incomingReaction;
    }

    // Remove the incoming reaction
    incomingReaction.Reset();

    // Preserve the existing backoff when an attempted reaction fails.
    // This is also needed when the reaction was selected and started by
    // PlayerbotAI in the same update. Always use a non-zero delay so a
    // failed reaction cannot be retried on every update when reactDelay is 0.
    if (!reactionExecuted && !IsReacting())
    {
        uint32 retryDelay = sPlayerbotAIConfig.reactDelay;
        if (!retryDelay)
            retryDelay = 1U;

        if (minimal)
        {
            if (retryDelay > 0xFFFFFFFFU / 10U)
                retryDelay = 0xFFFFFFFFU;
            else
                retryDelay *= 10U;
        }

        if (aiReactionUpdateDelay < retryDelay)
            aiReactionUpdateDelay = retryDelay;
    }

    reactionStartInProgress = false;

    return reactionExecuted;
}

void ReactionEngine::StopReaction()
{
    ongoingReaction.Reset();
    aiReactionUpdateDelay = 0U;

    // TO DO: Interrupt if the action is still running
    // ...
}

bool ReactionEngine::Update(uint32 elapsed, bool minimal, bool isStunned, bool& reactionFound)
{
    aiReactionUpdateDelay = aiReactionUpdateDelay > elapsed ? aiReactionUpdateDelay - elapsed : 0U;

    reactionFound = false;

    // A listener, interruption hook, or action can synchronously re-enter the
    // AI update. The outer reaction owns this update until its start sequence
    // has completed; in particular, do not find or start another reaction and
    // do not allow the normal engine to run from the nested update.
    if (reactionStartInProgress)
        return true;

    bool reactionFinished = false;
    bool reactionAttempted = false;

    // Can update reaction?
    if (CanUpdateAIReaction())
    {
        if (IsReacting())
        {
            if (ongoingReaction.Update(elapsed))
            {
                StopReaction();
                reactionFinished = true;
            }
        }
        else
            reactionFinished = true;

        if(reactionFinished)
        {
            if (HasIncomingReaction())
            {
                // Start the incoming reaction
                reactionAttempted = true;
                StartReaction(minimal);
            }
            else
            {
                // Look for an available reaction
                if (FindReaction(isStunned))
                {
                    reactionFound = true;
                }
            }
        }

        // Only add a reaction update delay if no reaction is pending or currently running
        if (!HasIncomingReaction() && !IsReacting())
        {
            if (aiReactionUpdateDelay < sPlayerbotAIConfig.reactDelay)
                aiReactionUpdateDelay = minimal ? sPlayerbotAIConfig.reactDelay * 10 : sPlayerbotAIConfig.reactDelay;
        }
    }

    // Return true if a reaction was attempted, is pending, or is currently
    // running. An attempted reaction consumes this update even when its
    // action fails, so the normal engine cannot run immediately afterward.
    return reactionAttempted || HasIncomingReaction() || IsReacting();
}

bool ReactionEngine::ListenAndExecute(Action* action, Event& event)
{
    bool actionExecuted = false;
    if (actionExecutionListeners.Before(action, event))
    {
        actionExecuted = actionExecutionListeners.AllowExecution(action, event) ? action->Execute(event) : true;
        if (actionExecuted)
        {
            if (!incomingReaction.GetAction()) //Prevent reset during action.
                incomingReaction.SetAction(action);
            ai->SetActionDuration(action);
        }
    }

    if (ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
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

        if (!event.getSource().empty())
            out << " [" << event.getSource() << "]";

        out << " [reaction]";

        if(actionExecuted)
            out << " (duration: " << ((float)incomingReaction.GetDuration() / static_cast<float>(IN_MILLISECONDS)) << "s)";

        ai->TellPlayerNoFacing(ai->GetMaster(), out);
    }

    actionExecuted = actionExecutionListeners.OverrideResult(action, actionExecuted, event);
    actionExecutionListeners.After(action, actionExecuted, event);
    return actionExecuted;
}

ai::Action* ReactionEngine::InitializeAction(ActionNode* actionNode)
{
    Action* action = actionNode->getAction();
    if (!action)
    {
        action = aiObjectContext->GetAction(actionNode->getName());
        actionNode->setAction(action);
    }

    if (action)
    {
        action->SetReaction(true);
    }

    return action;
}

void ReactionEngine::SetReactionDuration(const Action* action)
{
    if (action && (IsReacting() || HasIncomingReaction()))
    {
        if (ongoingReaction.GetAction() == action)
        {
            ongoingReaction.SetDuration(action->GetDuration());
        }
        else if (incomingReaction.GetAction() == action)
        {
            incomingReaction.SetDuration(action->GetDuration());
        }
    }
}

void ReactionEngine::Reset()
{
    ongoingReaction.Reset();
    incomingReaction.Reset();
    aiReactionUpdateDelay = 0U;
}

bool ReactionEngine::CanUpdateAIReaction() const
{
    Player* bot = ai->GetBot();
    return (aiReactionUpdateDelay < 100U) && 
            bot->IsInWorld() &&
           !bot->IsBeingTeleported();
}

const Reaction* ReactionEngine::GetReaction() const
{
    const Reaction* reaction = nullptr;
    if (ongoingReaction.IsValid())
    {
        reaction = &ongoingReaction;
    }
    else if (incomingReaction.IsValid())
    {
        reaction = &incomingReaction;
    }

    return reaction;
}

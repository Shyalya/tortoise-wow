#ifndef AI_CONTEXT_AUGMENT_H
#define AI_CONTEXT_AUGMENT_H

// Modules extend every bot's AI context through here.
//
// mod-playerbots keeps the strategy/action/trigger/value registries as shared
// statics, so its modules append once and every bot sees it. This tree builds
// the registries per bot in the class-context constructors, so a module has to
// be given each new context as it is born - that is the augmenter. Registering
// also walks the bots that already exist, which frees the module from caring
// whether it registered before or after the random-bot factory ran.
//
// Augmenters stay registered for the process lifetime; there is deliberately
// no unregister - a module cannot be unloaded from a static build anyway.

class PlayerbotAI;

namespace ai
{
    class AiObjectContext;
}

typedef void (*AiContextAugmenter)(PlayerbotAI* ai, ai::AiObjectContext* context);

void RegisterAiContextAugmenter(AiContextAugmenter fn);
void RunAiContextAugmenters(PlayerbotAI* ai, ai::AiObjectContext* context);

#endif

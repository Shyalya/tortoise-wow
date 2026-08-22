/*
 * mod-dungeon-clear - AiObjectContextAccess.h  (Tortoise port)
 *
 * Upstream this header robbed mod-playerbots' PRIVATE shared registries via
 * the explicit-instantiation access bypass, because that engine offers no
 * extension seam. This tree grew one instead: AiContextAugment registers a
 * callback that receives every bot context as it is built (and every context
 * that already exists), and the callback appends the DungeonClear contexts.
 *
 * OWNERSHIP: each bot context gets ITS OWN fresh instances, and the receiving
 * NamedObjectContextList takes ownership (its destructor deletes everything
 * that is not marked shared - these are not). Two reasons this must not be a
 * set of function-local statics handed to every bot:
 *   1. the list destructor would run `delete` on a static the first time any
 *      bot context is torn down (bot relogin) - glibc aborts with
 *      "free(): invalid pointer";
 *   2. NamedObjectContext caches created objects BY NAME inside the context
 *      instance, so one instance for all bots would hand every later bot the
 *      strategy/action/trigger objects bound to the FIRST bot's PlayerbotAI.
 * Per-bot instances are exactly how the regular class contexts behave.
 */

#ifndef MOD_DUNGEONCLEAR_AIOBJECTCONTEXTACCESS_H
#define MOD_DUNGEONCLEAR_AIOBJECTCONTEXTACCESS_H

#include "playerbot/AiContextAugment.h"
#include "Ai/Dungeon/DungeonClear/DungeonClearStrategyContext.h"
#include "Ai/Dungeon/DungeonClear/DungeonClearActionContext.h"
#include "Ai/Dungeon/DungeonClear/DungeonClearTriggerContext.h"
#include "Ai/Dungeon/DungeonClear/DungeonClearValueContext.h"

namespace dc_access
{
    inline void AugmentContext(PlayerbotAI* /*ai*/, ai::AiObjectContext* context)
    {
        context->AddShared(new DungeonClearStrategyContext());
        context->AddShared(new DungeonClearActionContext());
        context->AddShared(new DungeonClearTriggerContext());
        context->AddShared(new DungeonClearValueContext());
    }

    inline void RegisterDungeonClearContexts()
    {
        RegisterAiContextAugmenter(&AugmentContext);
    }
}

#endif

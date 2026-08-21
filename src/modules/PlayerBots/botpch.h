//add here most rarely modified headers to speed up debug build compilation
#include "Protocol/WorldSocket.h"
#include "Common.h"

// Core game systems
#include "Maps/MapManager.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Database/SQLStorages.h"
#include "Protocol/Opcodes.h"
#include "SharedDefines.h"
#include "Guild/GuildMgr.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"

// Heavy game headers (frequently used, rarely modified)
#include "Spells/Spell.h"
#include "Spells/SpellMgr.h"
#include "Spells/SpellAuras.h"
#include "WorldPacket.h"
#include "LootMgr.h"
#include "GossipDef.h"
#include "Chat/Chat.h"
#include "World.h"
#include "Objects/Unit.h"
#include "Movement/MotionMaster.h"
#include "Guild/Guild.h"
#include "Objects/Player.h"
#include "Group/Group.h"
#include "Database/DatabaseEnv.h"

// Grid system (used in many playerbot files)
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

// cmangos -> Penqle compatibility shim. Must come AFTER Penqle's core headers
// (so the shim's proxy methods can inline-call sSpellMgr.GetSpellEntry(...) etc.)
// and BEFORE the bot module's own headers (which reference the shim's typedefs
// like GuidSet, AreaTableEntry, GenericTransport).
#include "cmangos-compat-shim.h"

// Boost headers (used across multiple files)
#include <boost/algorithm/string.hpp>
#include <boost/functional/hash.hpp>
#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/filesystem.hpp>

// STL headers
#include <stack>
#include <iomanip>
#include <algorithm>
#include <functional>
#include <memory>
#include <regex>
#include <numeric>

// Typed access to the player slots this module owns. Replaces the
// Player::GetPlayerbotAI() / GetPlayerbotMgr() members the core used to carry.
#include "playerbot/BotSlots.h"

// Playerbot core
#include "playerbot/playerbot.h"

// Playerbot AI framework (included 40-60+ times each, rarely modified)
#include "playerbot/strategy/AiObject.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/NamedObjectContext.h"
#include "playerbot/strategy/AiObjectContext.h"

#pragma once

// Opt-in observability for the playerbots subsystem.
//
//   SC_LOG(fmt, ...)         -> sLog.outDetail("[BOT] ..."). Grep "\[BOT\]"
//                               in info.log to see all bot debug output.
//   SC_PHASE(tag, botName)   -> stamps a TLS marker that mangosd's unhandled-
//                               exception filter reads to identify which
//                               subsystem the crashing thread was in.
//
// Both gated at runtime on AiPlayerbot.EnableActionLog (default off). When
// off, each call is one branch on a global bool and the compiler typically
// folds it away.
//
// The SC_ prefix is a holdover from the project's internal name for the bot
// diagnostic harness ("SoloCommander"); kept as-is to avoid a churn rename
// across ~80 call sites. Read SC_ as "bot diag".

#include "Log.h"
class PlayerbotAI;

namespace ai { namespace botdiag {
    bool IsActionLogEnabled();
    void TraceBehavior(PlayerbotAI* ai, const char* reason, const char* detail, uint32 taxiPath = 0);
    extern thread_local const char* gLastPhaseTag;
    extern thread_local const char* gLastPhaseBotName;
}}

#define SC_LOG(fmt, ...) do { \
    if (ai::botdiag::IsActionLogEnabled()) \
        sLog.outDetail("[BOT] " fmt, ##__VA_ARGS__); \
} while (0)

#define SC_PHASE(tag, botName) do { \
    if (ai::botdiag::IsActionLogEnabled()) { \
        ai::botdiag::gLastPhaseTag     = (tag); \
        ai::botdiag::gLastPhaseBotName = (botName); \
    } \
} while (0)

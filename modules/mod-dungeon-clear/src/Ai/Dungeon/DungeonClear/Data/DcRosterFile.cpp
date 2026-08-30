/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRosterFile.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <unordered_set>

#include "Config.h"
#include "Log.h"

namespace
{
    // The order field is a uint8 and its mask bit is order-1, so anything
    // outside 1..32 would either wrap or address a bit that does not exist.
    // Rejected at read time rather than at use time: a bad line has to be
    // visible in the log the moment it is loaded, not three hours into a run
    // as a boss that never gets picked.
    constexpr uint32 DC_ORDER_MIN = 1;
    constexpr uint32 DC_ORDER_MAX = 32;

    struct Overlay
    {
        std::vector<uint32> credits;
        std::vector<DcBossOrderRow> order;
        std::unordered_set<uint32> dropped;
        // Maps whose order the file owns outright - see the header note on
        // per-map replacement.
        std::unordered_set<uint32> authoredMaps;
        bool loaded = false;
    };

    std::mutex& Lock()
    {
        static std::mutex instance;
        return instance;
    }

    Overlay& Data()
    {
        static Overlay instance;
        return instance;
    }

    uint32 ReadFile(Overlay& out, std::string* error)
    {
        out = Overlay();
        out.loaded = true;

        std::string const path = sConfig.GetStringDefault("DungeonClear.RosterFile", "");
        if (path.empty())
            return 0;              // no file configured: compiled baseline only

        std::ifstream in(path);
        if (!in.is_open())
        {
            if (error)
                *error = "cannot open " + path;
            return 0;
        }

        auto complain = [error](std::string const& what)
        {
            if (error && error->empty())
                *error = what;
        };

        uint32 accepted = 0;
        uint32 lineNo = 0;
        std::string line;
        while (std::getline(in, line))
        {
            ++lineNo;
            std::string::size_type const hash = line.find('#');
            if (hash != std::string::npos)
                line.erase(hash);

            std::istringstream ls(line);
            std::string directive;
            if (!(ls >> directive))
                continue;          // blank or comment-only

            if (directive == "credit")
            {
                uint32 entry = 0;
                uint32 onThisLine = 0;
                while (ls >> entry)
                {
                    if (!entry)
                        continue;
                    out.credits.push_back(entry);
                    ++onThisLine;
                }
                if (!onThisLine)
                    complain("credit without an entry on line " + std::to_string(lineNo));
                accepted += onThisLine;
            }
            else if (directive == "order")
            {
                uint32 mapId = 0;
                uint32 entry = 0;
                uint32 index = 0;
                if (!(ls >> mapId >> entry >> index))
                {
                    complain("order needs <mapId> <entry> <index> on line " + std::to_string(lineNo));
                    continue;
                }
                if (!mapId || mapId > 0xFFFF || !entry)
                {
                    complain("order with an impossible map or entry on line " + std::to_string(lineNo));
                    continue;
                }
                if (index < DC_ORDER_MIN || index > DC_ORDER_MAX)
                {
                    complain("order index out of 1..32 on line " + std::to_string(lineNo));
                    continue;
                }
                out.authoredMaps.insert(mapId);
                DcBossOrderRow row;
                row.mapId = static_cast<uint16>(mapId);
                row.entry = entry;
                row.order = static_cast<uint8>(index);
                out.order.push_back(row);
                ++accepted;
            }
            else if (directive == "drop")
            {
                uint32 entry = 0;
                uint32 onThisLine = 0;
                while (ls >> entry)
                {
                    if (!entry)
                        continue;
                    out.dropped.insert(entry);
                    ++onThisLine;
                }
                if (!onThisLine)
                    complain("drop without an entry on line " + std::to_string(lineNo));
                accepted += onThisLine;
            }
            else
            {
                complain("unknown directive '" + directive + "' on line " + std::to_string(lineNo));
            }
        }
        return accepted;
    }

    // Caller holds Lock().
    void EnsureLoaded()
    {
        if (Data().loaded)
            return;

        std::string err;
        uint32 const n = ReadFile(Data(), &err);
        if (!err.empty())
            LOG_INFO("playerbots.dungeonclear",
                     "[dungeon-clear] roster file rejected something: {}", err);
        if (n)
            LOG_INFO("playerbots.dungeonclear",
                     "[dungeon-clear] roster file: {} directives loaded", n);
    }
}

std::vector<uint32> DcRosterFile::CreditEntries()
{
    std::lock_guard<std::mutex> guard(Lock());
    EnsureLoaded();

    std::vector<uint32> out(std::begin(DC_BOSS_ENTRIES_1121), std::end(DC_BOSS_ENTRIES_1121));
    out.insert(out.end(), Data().credits.begin(), Data().credits.end());
    // An order line credits its own entry, exactly as the compiled order table
    // already does further down in BossSpawnIndex - so a boss the curated list
    // never carried needs one line, not two.
    for (DcBossOrderRow const& row : Data().order)
        out.push_back(row.entry);

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    // drop last, so it beats the compiled list, a credit line and an order
    // line alike - there is no way to accidentally re-credit a rare.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](uint32 e) { return Data().dropped.count(e) != 0; }),
              out.end());
    return out;
}

std::vector<DcBossOrderRow> DcRosterFile::OrderRows()
{
    std::lock_guard<std::mutex> guard(Lock());
    EnsureLoaded();

    std::vector<DcBossOrderRow> out;
    out.reserve(std::size(DC_BOSS_ORDER_1121) + Data().order.size());
    for (DcBossOrderRow const& row : DC_BOSS_ORDER_1121)
        if (!Data().authoredMaps.count(row.mapId))
            out.push_back(row);
    out.insert(out.end(), Data().order.begin(), Data().order.end());
    // A dropped entry must not linger here either: BossSpawnIndex treats an
    // order row as a credit of its own, so leaving it would put the rare
    // straight back into the roster.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](DcBossOrderRow const& r) { return Data().dropped.count(r.entry) != 0; }),
              out.end());
    return out;
}

uint32 DcRosterFile::Reload(std::string* error)
{
    std::lock_guard<std::mutex> guard(Lock());

    std::string err;
    uint32 const n = ReadFile(Data(), &err);
    if (error)
        *error = err;
    if (!err.empty())
        LOG_INFO("playerbots.dungeonclear",
                 "[dungeon-clear] roster file rejected something: {}", err);
    LOG_INFO("playerbots.dungeonclear",
             "[dungeon-clear] roster file re-read: {} directives", n);
    return n;
}

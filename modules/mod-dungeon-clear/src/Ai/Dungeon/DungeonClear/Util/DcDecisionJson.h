/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCDECISIONJSON_H
#define _PLAYERBOT_DCDECISIONJSON_H

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

// Tiny dependency-free flat-JSONL helper shared by the decision capture/replay
// IO of the orchestration replay harness (Tier 1 of the headless-sim plan).
//
// The first decision core (DungeonClearApproachIo) hand-rolled its own writer +
// flat parser inline. Rather than copy that boilerplate per new decision family
// (pull governor, engage arbiter, ...), this header lifts the format primitives
// into one place. The format is deliberately identical to the approach IO's: one
// self-contained, NON-nested JSON object per line, scalar values only, the one
// string value (the verdict) a bare identifier — so a plain split on ',' then
// ':' parses it with no third-party dep.
//
// Engine-free on purpose (stdlib only): the same code links into the live
// server's capture path AND the offline gtest runner, so a capture round-trips
// byte-for-byte with a replay.
namespace DcDecisionJson
{
    // Accumulates "key":value pairs into one flat JSONL object. Floats are
    // written at full round-trip precision so a replayed observation lands on the
    // same side of every threshold the live capture did. Chainable.
    class Writer
    {
    public:
        Writer() { _s.precision(9); _s << '{'; }

        Writer& Add(char const* key, bool v)
        {
            Sep();
            _s << '"' << key << "\":" << (v ? "true" : "false");
            return *this;
        }
        Writer& Add(char const* key, std::uint32_t v)
        {
            Sep();
            _s << '"' << key << "\":" << v;
            return *this;
        }
        Writer& Add(char const* key, std::uint64_t v)
        {
            Sep();
            _s << '"' << key << "\":" << v;
            return *this;
        }
        Writer& Add(char const* key, float v)
        {
            Sep();
            _s << '"' << key << "\":" << v;
            return *this;
        }
        // A bare-identifier string value (e.g. a verdict name). Not escaped — the
        // format only ever stores identifier tokens here.
        Writer& AddStr(char const* key, std::string_view v)
        {
            Sep();
            _s << '"' << key << "\":\"" << v << '"';
            return *this;
        }

        std::string Str()
        {
            _s << '}';
            return _s.str();
        }

    private:
        void Sep()
        {
            if (_first)
                _first = false;
            else
                _s << ',';
        }

        std::ostringstream _s;
        bool _first = true;
    };

    // Strip surrounding ASCII whitespace and a single pair of double quotes.
    inline std::string_view Trim(std::string_view s)
    {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                              s.front() == '\r' || s.front() == '\n'))
            s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                              s.back() == '\r' || s.back() == '\n'))
            s.remove_suffix(1);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        {
            s.remove_prefix(1);
            s.remove_suffix(1);
        }
        return s;
    }

    using Fields = std::unordered_map<std::string, std::string>;

    // Parse one flat JSONL object line into key->raw-value. Returns nullopt when
    // the line is blank, a comment, or not a single {...} object.
    inline std::optional<Fields> Parse(std::string const& line)
    {
        std::string_view body = Trim(line);
        if (body.empty() || body.front() == '#')
            return std::nullopt;
        if (body.size() < 2 || body.front() != '{' || body.back() != '}')
            return std::nullopt;
        body.remove_prefix(1);
        body.remove_suffix(1);

        Fields out;
        while (!body.empty())
        {
            std::size_t const comma = body.find(',');
            std::string_view pair =
                comma == std::string_view::npos ? body : body.substr(0, comma);
            std::size_t const colon = pair.find(':');
            if (colon != std::string_view::npos)
            {
                std::string_view key = Trim(pair.substr(0, colon));
                std::string_view val = Trim(pair.substr(colon + 1));
                out.emplace(std::string(key), std::string(val));
            }
            if (comma == std::string_view::npos)
                break;
            body.remove_prefix(comma + 1);
        }
        return out;
    }

    inline bool Has(Fields const& m, char const* key) { return m.find(key) != m.end(); }

    inline float GetF(Fields const& m, char const* key, float fallback)
    {
        auto const it = m.find(key);
        return it == m.end() ? fallback : std::strtof(it->second.c_str(), nullptr);
    }

    inline std::uint32_t GetU(Fields const& m, char const* key, std::uint32_t fallback)
    {
        auto const it = m.find(key);
        return it == m.end() ? fallback
                             : static_cast<std::uint32_t>(
                                   std::strtoul(it->second.c_str(), nullptr, 10));
    }

    inline std::uint64_t GetU64(Fields const& m, char const* key, std::uint64_t fallback)
    {
        auto const it = m.find(key);
        return it == m.end() ? fallback
                             : static_cast<std::uint64_t>(
                                   std::strtoull(it->second.c_str(), nullptr, 10));
    }

    inline bool GetB(Fields const& m, char const* key, bool fallback)
    {
        auto const it = m.find(key);
        if (it == m.end())
            return fallback;
        return it->second == "true" || it->second == "1";
    }

    inline std::string GetStr(Fields const& m, char const* key, std::string const& fallback)
    {
        auto const it = m.find(key);
        return it == m.end() ? fallback : it->second;
    }
}

#endif  // _PLAYERBOT_DCDECISIONJSON_H

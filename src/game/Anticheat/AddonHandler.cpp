/*
 * Copyright (C) 2017-2018 namreeb (legal@namreeb.org)
 *
 * This is private software and may not be shared under any circumstances,
 * absent permission of namreeb.
 */

#include "libanticheat.hpp"
#include "Config.hpp"
#include "WorldPacket.h"
#include "World.h"
#include "Util.h"
#include "Database/DatabaseImpl.h"
#include "Database/DatabaseEnv.h"
#include "AddonHandler.h"
#ifdef WIN32
#include "..\zlib\zlib.h"
#else
#include "zlib.h"
#endif

#include <memory>

namespace
{
bool FingerprintValid(uint32 fingerprint)
{
    // no bytes can equal 0x00, 0x01, or 0x02
    const uint8 *b = reinterpret_cast<uint8 *>(&fingerprint);
    for (auto i = 0u; i < sizeof(fingerprint); ++i)
        if (b[i] <= 0x02)
            return false;

    return true;
}

bool FingerprintExists(uint32 fingerprint)
{
    // if it is not valid, it can't exist!
    if (!FingerprintValid(fingerprint))
        return false;

    std::unique_ptr<QueryResult> result(LoginDatabase.PQuery("SELECT COUNT(*) FROM system_fingerprint_usage WHERE fingerprint = %u", fingerprint));

    return !!result->Fetch()[0].GetUInt32();
}

uint32 GenerateFingerprint()
{
    do
    {
        uint32 fingerprint = 0;

        // generate four random bytes between 0x03 and 0xFF
        for (auto i = 0u; i < sizeof(uint32); ++i)
            fingerprint |= static_cast<uint8>(urand(0x03, 0xFF)) << 8*i;

        // if the fingerprint already exists, repeat
        if (!sWorld.getConfig(CONFIG_BOOL_SEA_NETWORK))
        {
            if (FingerprintExists(fingerprint))
                continue;
        }

        // if we reach here, we are done

        return fingerprint;
    } while (true);
}

struct AddonInfo
{
    std::string name;
    uint8 flags;
    uint32 moduluscrc;
    uint32 urlcrc;
};

// each addon will give us one byte of the fingerprint.  this byte can not be 0x00, 0x01, or 0x02, as
// the client considers these as valid flags with behaviors we want to avoid (i.e. 0x02 = banned)
static constexpr char *sFingerprintAddons[] =
{
    "Blizzard_BindingUI",
    "Blizzard_InspectUI",
    "Blizzard_MacroUI",
    "Blizzard_RaidUI",
};

#ifdef ALLOW_TURTLE_ADDONS
bool IsTurtleSignedAddon(std::string const& addonName)
{
    return addonName == "Turtle_General" || addonName == "Turtle_GroupUI";
}
#endif
}

namespace Anticheat
{
bool SessionAnticheat::ReadAddonInfo(WorldPacket *authSession, WorldPacket &out)
{
    // broken addon packet, can't be received from real client
    if (!authSession || authSession->rpos() > authSession->size() || authSession->size() - authSession->rpos() < sizeof(uint32))
        return false;

    // have we already received this information?  if so, it must be some kind of hacker
    if (!!_fingerprint)
    {
        sLog.out(LOG_ANTICHEAT_BASIC,
            "ADDON: Received addon information when fingerprint is already known (0x%08x) account %u ip %s.  This may be an attempt to crash the server",
            _fingerprint, _session->GetAccountId(), _session->GetRemoteAddress().c_str());
        authSession->rpos(authSession->wpos());
        return false;
    }

    uint32 addonSize = authSession->read<uint32>();

    // empty addon packet, nothing process, can't be received from real client
    if (!addonSize)
        return false;

    if (addonSize > AddonInfoLimits::MAX_UNCOMPRESSED_SIZE)
    {
        sLog.out(LOG_ANTICHEAT_BASIC, "ADDON: Addon info too big, size %u.  Account %s (id %u) IP %s",
            addonSize, _session->GetUsername().c_str(), _session->GetAccountId(), _session->GetRemoteAddress().c_str());

        return false;
    }

    ByteBuffer clientAddonData;
    if (!DecompressAddonInfo(*authSession, addonSize, clientAddonData))
    {
        sLog.out(LOG_ANTICHEAT_BASIC, "ADDON: Addon information failed to compress.  Account %s (id %u) IP %s",
            _session->GetUsername().c_str(), _session->GetAccountId(), _session->GetRemoteAddress().c_str());

        return false;
    }

    static constexpr uint32 correctModulusCRC = 0x4C1C776D;

    static constexpr uint8 modulus[] =
    {
        0xC3, 0x5B, 0x50, 0x84, 0xB9, 0x3E, 0x32, 0x42, 0x8C, 0xD0, 0xC7, 0x48, 0xFA, 0x0E, 0x5D, 0x54,
        0x5A, 0xA3, 0x0E, 0x14, 0xBA, 0x9E, 0x0D, 0xB9, 0x5D, 0x8B, 0xEE, 0xB6, 0x84, 0x93, 0x45, 0x75,
        0xFF, 0x31, 0xFE, 0x2F, 0x64, 0x3F, 0x3D, 0x6D, 0x07, 0xD9, 0x44, 0x9B, 0x40, 0x85, 0x59, 0x34,
        0x4E, 0x10, 0xE1, 0xE7, 0x43, 0x69, 0xEF, 0x7C, 0x16, 0xFC, 0xB4, 0xED, 0x1B, 0x95, 0x28, 0xA8,
        0x23, 0x76, 0x51, 0x31, 0x57, 0x30, 0x2B, 0x79, 0x08, 0x50, 0x10, 0x1C, 0x4A, 0x1A, 0x2C, 0xC8,
        0x8B, 0x8F, 0x05, 0x2D, 0x22, 0x3D, 0xDB, 0x5A, 0x24, 0x7A, 0x0F, 0x13, 0x50, 0x37, 0x8F, 0x5A,
        0xCC, 0x9E, 0x04, 0x44, 0x0E, 0x87, 0x01, 0xD4, 0xA3, 0x15, 0x94, 0x16, 0x34, 0xC6, 0xC2, 0xC3,
        0xFB, 0x49, 0xFE, 0xE1, 0xF9, 0xDA, 0x8C, 0x50, 0x3C, 0xBE, 0x2C, 0xBB, 0x57, 0xED, 0x46, 0xB9,
        0xAD, 0x8B, 0xC6, 0xDF, 0x0E, 0xD6, 0x0F, 0xBE, 0x80, 0xB3, 0x8B, 0x1E, 0x77, 0xCF, 0xAD, 0x22,
        0xCF, 0xB7, 0x4B, 0xCF, 0xFB, 0xF0, 0x6B, 0x11, 0x45, 0x2D, 0x7A, 0x81, 0x18, 0xF2, 0x92, 0x7E,
        0x98, 0x56, 0x5D, 0x5E, 0x69, 0x72, 0x0A, 0x0D, 0x03, 0x0A, 0x85, 0xA2, 0x85, 0x9C, 0xCB, 0xFB,
        0x56, 0x6E, 0x8F, 0x44, 0xBB, 0x8F, 0x02, 0x22, 0x68, 0x63, 0x97, 0xBC, 0x85, 0xBA, 0xA8, 0xF7,
        0xB5, 0x40, 0x68, 0x3C, 0x77, 0x86, 0x6F, 0x4B, 0xD7, 0x88, 0xCA, 0x8A, 0xD7, 0xCE, 0x36, 0xF0,
        0x45, 0x6E, 0xD5, 0x64, 0x79, 0x0F, 0x17, 0xFC, 0x64, 0xDD, 0x10, 0x6F, 0xF3, 0xF5, 0xE0, 0xA6,
        0xC3, 0xFB, 0x1B, 0x8C, 0x29, 0xEF, 0x8E, 0xE5, 0x34, 0xCB, 0xD1, 0x2A, 0xCE, 0x79, 0xC3, 0x9A,
        0x0D, 0x36, 0xEA, 0x01, 0xE0, 0xAA, 0x91, 0x20, 0x54, 0xF0, 0x72, 0xD8, 0x1E, 0xC7, 0x89, 0xD2
    };
    
#ifdef ALLOW_TURTLE_ADDONS
    static constexpr uint8 turtleAddonPublicKey[] =
    {
        0xF1, 0x78, 0x32, 0x50, 0x77, 0x49, 0x34, 0x92, 0x79, 0x9E, 0x76, 0x7E, 0x1C, 0x91, 0x34, 0xB6,
        0xDC, 0x1C, 0xFF, 0x30, 0x83, 0xA5, 0x42, 0x7C, 0x94, 0x3A, 0x9A, 0x10, 0x1E, 0x85, 0x18, 0x85,
        0x2E, 0x39, 0xDC, 0x03, 0x36, 0x9B, 0xD3, 0x13, 0x34, 0xB3, 0xBB, 0x1D, 0x82, 0xAF, 0x3B, 0xE8,
        0x3F, 0x44, 0x06, 0x41, 0x3A, 0x51, 0x80, 0x5A, 0x44, 0xFD, 0x14, 0x74, 0x3E, 0x93, 0xB6, 0x68,
        0xFD, 0x2B, 0x6F, 0x4C, 0x26, 0x75, 0x6A, 0xF2, 0xDA, 0xD1, 0x6B, 0xA7, 0x10, 0xD7, 0x34, 0xF1,
        0x79, 0xE3, 0x29, 0x65, 0xF7, 0xB9, 0xC6, 0x4A, 0x3B, 0x27, 0x02, 0xAA, 0x21, 0x56, 0x60, 0xF7,
        0x55, 0x12, 0x16, 0x7D, 0x3D, 0x8D, 0x51, 0x8E, 0xEE, 0x1D, 0xFA, 0x81, 0xBB, 0xAE, 0x53, 0xF3,
        0x90, 0x0D, 0xE3, 0x4F, 0x08, 0x1C, 0x3E, 0x15, 0x69, 0x90, 0x62, 0x50, 0x0C, 0x58, 0x6B, 0xBB,
        0x33, 0xA8, 0x6B, 0xE8, 0xA9, 0x2F, 0xF5, 0xF9, 0xAE, 0x1A, 0xDA, 0x30, 0xAB, 0xC9, 0xCB, 0x08,
        0x71, 0x09, 0x28, 0x40, 0x66, 0xB8, 0xF6, 0xC5, 0x0F, 0x59, 0x62, 0x5E, 0xF8, 0x32, 0x3C, 0x4D,
        0xBC, 0xED, 0x8F, 0x3A, 0xE4, 0x4B, 0x14, 0x7A, 0xE9, 0x5D, 0x8E, 0x23, 0xFB, 0x41, 0x0D, 0xAA,
        0x91, 0x4C, 0x9B, 0xB6, 0xAA, 0x00, 0x6E, 0xB9, 0x75, 0x31, 0x48, 0x0A, 0x0E, 0xA2, 0xCB, 0xFD,
        0x79, 0x24, 0x91, 0x05, 0xB2, 0x25, 0xB3, 0x46, 0xAB, 0x1F, 0xA3, 0x24, 0xCE, 0x77, 0x5C, 0xF4,
        0xCB, 0x55, 0x98, 0xB9, 0x2C, 0x02, 0x90, 0xB0, 0xD4, 0x75, 0x45, 0x62, 0xDB, 0xAE, 0xD4, 0x00,
        0xA6, 0x3F, 0xB3, 0xDC, 0x77, 0xA5, 0x61, 0x0F, 0xC7, 0xF0, 0x97, 0x6F, 0xBC, 0x0A, 0xC5, 0x3A,
        0xC8, 0x39, 0x4D, 0xC1, 0x54, 0xD2, 0x21, 0xE2, 0x14, 0x71, 0x73, 0x46, 0xA5, 0x13, 0x97, 0xAB
    };
#endif

    static constexpr size_t fingerprintAddonCount = sizeof(sFingerprintAddons) / sizeof(sFingerprintAddons[0]);
    static_assert(fingerprintAddonCount == sizeof(uint32), "Bad size for fingerprint calculations");

    // flag bytes for each addon which contributes to the fingerprint
    uint8 fingerprintBytes[fingerprintAddonCount];

    memset(fingerprintBytes, 0, sizeof(fingerprintBytes));

    std::vector<AddonInfo> addonInfo;

    // first, read client addon info, determining if the packet is valid, and if so, whether a fingerprint is already present
    addonInfo.reserve(AddonInfoLimits::MAX_RECORDS);
    while (clientAddonData.rpos() < clientAddonData.size())
    {
        if (addonInfo.size() >= AddonInfoLimits::MAX_RECORDS)
            return false;

        AddonInfo info;

        if (!ReadAddonName(clientAddonData, info.name) || clientAddonData.size() - clientAddonData.rpos() < sizeof(uint8) + sizeof(uint32) + sizeof(uint32))
            return false;

        clientAddonData >> info.flags >> info.moduluscrc >> info.urlcrc;

        sLog.out(LOG_ANTICHEAT_DEBUG, "ADDON: %s flags 0x%02x modulus crc 0x%08x url crc 0x%08x",
            info.name.c_str(), info.flags, info.moduluscrc, info.urlcrc);

        for (auto i = 0u; i < fingerprintAddonCount; ++i)
        {
            if (info.name != sFingerprintAddons[i])
                continue;

            fingerprintBytes[i] = info.flags;
        }

        addonInfo.push_back(info);
    }

    bool fingerprintFound = false;
    for (auto i = 0u; i < fingerprintAddonCount; ++i)
    {
        // we should never have zeros together with a fingerprint
        if (!fingerprintBytes[i])
        {
            fingerprintFound = false;
            break;
        }

        // there has to be at least one byte which is neither zero nor 0x01 for it to be a fingerprint
        if (fingerprintBytes[i] != 0x01)
            fingerprintFound = true;
    }

    uint32 fingerprint;
    memcpy(&fingerprint, fingerprintBytes, sizeof(fingerprint));

    // if a fingerprint was found and is valid, use it
    if (fingerprintFound && FingerprintValid(fingerprint))
        sLog.out(LOG_ANTICHEAT_DEBUG, "ADDON: Found fingerprint: 0x%08x.  Account %s (id %u) IP %s",
            fingerprint, _session->GetUsername().c_str(), _session->GetAccountId(), _session->GetRemoteAddress().c_str());
    else
    {
        // if it was found, but we reach here, it must not have been valid?
        if (fingerprintFound)
        {
            sLog.out(LOG_ANTICHEAT_BASIC, "ADDON: Found invalid fingerprint 0x%08x.  Account %s (id %u) IP %s.  Generating new...",
                fingerprint, _session->GetUsername().c_str(), _session->GetAccountId(), _session->GetRemoteAddress().c_str());

            fingerprintFound = false;
        }

        fingerprint = GenerateFingerprint();
        sLog.out(LOG_ANTICHEAT_BASIC, "ADDON: Generated new fingerprint: 0x%08x.  Account %s (id %u) IP %s local IP %s",
            fingerprint, _session->GetUsername().c_str(), _session->GetAccountId(), _session->GetRemoteAddress().c_str(),
            _session->GetRemoteAddress().c_str());
    }

    _fingerprint = fingerprint;
    auto& sample = _session->_analyser->GetCurrentSample();
    sample.fingerprint = _fingerprint;
    sample.ipAddress = _session->GetRemoteAddress();

    if (!sWorld.getConfig(CONFIG_BOOL_SEA_NETWORK))
    {
        _session->_analyser->Enable();
        _session->_analyser->LoadFromDB();
    }

    sWorld.AddFingerprint(_fingerprint, _session->GetUsername());

    out.Initialize(SMSG_ADDON_INFO);

    for (auto const &addon : addonInfo)
    {
        out << static_cast<uint8>(2);

    #ifdef ALLOW_TURTLE_ADDONS
        if (IsTurtleSignedAddon(addon.name))
        {
            out << static_cast<uint8>(1);
            out << static_cast<uint8>(1);
            out.append(turtleAddonPublicKey, sizeof(turtleAddonPublicKey));
            out << static_cast<uint32>(0);
            out << static_cast<uint8>(0);
            continue;
        }
    #endif
        
        bool flagsWritten = false;

        // do we need to set a new fingerprint?
        if (!fingerprintFound)
            for (auto i = 0u; i < fingerprintAddonCount; ++i)
                // is this addon part of the fingerprint?  if so, write the appropriate portion of the fingerprint
                if (addon.name == sFingerprintAddons[i])
                {
                    // if the fingerprint needs to be written here, we must also send
                    // the modulus data so the fingerprint is saved to the disk 
                    out << *(reinterpret_cast<uint8 *>(&fingerprint) + i);

                    // true when modulus data follows
                    out << static_cast<uint8>(1);
                    out.append(modulus, sizeof(modulus));

                    // unknown field
                    out << static_cast<uint32>(0);

                    flagsWritten = true;
                    break;
                }

        // if we havent written the flags yet, because either we're not setting a
        // fingerprint or this addon isnt involved in it, send normal data...
        if (!flagsWritten)
        {
            out << addon.flags;

            // if there is data, verify it, and update if necessary
            if (!!addon.flags)
            {
                const uint8 sendData = addon.moduluscrc == correctModulusCRC ? 0 : 1;   // standard addon CRC

                out << sendData;

                if (sendData)
                    out.append(modulus, sizeof(modulus));

                if (!!addon.flags)
                    out << static_cast<uint32>(0);
            }
        }

        // never update the url
        out << static_cast<uint8>(0);
    }

    return true;
}
}

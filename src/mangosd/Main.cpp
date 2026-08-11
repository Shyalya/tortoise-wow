/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/// \addtogroup mangosd Mangos Daemon
/// @{
/// \file

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "Config/Config.h"
#include "Log.h"
#include "Master.h"
#include "SystemConfig.h"
#include "revision.h"
#include <openssl/opensslv.h>
#include <openssl/crypto.h>
#include <ace/Version.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <ace/Get_Opt.h>

#ifndef WIN32
#include "PosixDaemon.h"
#endif

DatabaseType WorldDatabase;                                 ///< Accessor to the world database
DatabaseType CharacterDatabase;                             ///< Accessor to the character database
DatabaseType LoginDatabase;                                 ///< Accessor to the realm/login database
DatabaseType LogsDatabase;                                  ///< Accessor to the Logs database

uint32 realmID;                                             ///< Id of the realm


const char* lsan_output_path = "log_path=leaks.txt";

//override LSAN options if found.
#ifdef ENABLE_LSAN
#ifdef __cplusplus
extern "C"
#endif
const char* __lsan_default_options()
{
    return lsan_output_path;
}
#endif


/// Print out the usage string for this program on the console.
void usage(const char *prog)
{
    sLog.outString("Usage: \n %s [<options>]\n"
        "    -v, --version            print version and exist\n\r"
        "    -c config_file           use config_file as configuration file\n\r"
        #ifdef WIN32
        "    Running as service functions:\n\r"
        "    -s run                   run as service\n\r"
        "    -s install               install service\n\r"
        "    -s uninstall             uninstall service\n\r"
        #else
        "    Running as daemon functions:\n\r"
        "    -s run                   run as daemon\n\r"
        "    -s stop                  stop daemon\n\r"
        #endif
        ,prog);
}

/// Launch the mangos server
extern int main(int argc, char **argv)
{
    ///- Command line parsing
    char const* cfg_file = _MANGOSD_CONFIG;


    char const *options = ":c:s:";

    thread_name("MainThread");

    ACE_Get_Opt cmd_opts(argc, argv, options);
    cmd_opts.long_option("version", 'v');

    char serviceDaemonMode = '\0';

    int option;
    while ((option = cmd_opts()) != EOF)
    {
        switch (option)
        {
            case 'c':
                cfg_file = cmd_opts.opt_arg();
                break;
            case 'v':
                printf("Core revision: %s\n", _FULLVERSION);
                return 0;
            case 's':
            {
                const char *mode = cmd_opts.opt_arg();

                if (!strcmp(mode, "run"))
                    serviceDaemonMode = 'r';
#ifdef WIN32
                else if (!strcmp(mode, "install"))
                    serviceDaemonMode = 'i';
                else if (!strcmp(mode, "uninstall"))
                    serviceDaemonMode = 'u';
#else
                else if (!strcmp(mode, "stop"))
                    serviceDaemonMode = 's';
#endif
                else
                {
                    sLog.outError("Runtime-Error: -%c unsupported argument %s", cmd_opts.opt_opt(), mode);
                    usage(argv[0]);
                    Log::WaitBeforeContinueIfNeed();
                    return 1;
                }
                break;
            }
            case ':':
                sLog.outError("Runtime-Error: -%c option requires an input argument", cmd_opts.opt_opt());
                usage(argv[0]);
                Log::WaitBeforeContinueIfNeed();
                return 1;
            default:
                sLog.outError("Runtime-Error: bad format of commandline arguments");
                usage(argv[0]);
                Log::WaitBeforeContinueIfNeed();
                return 1;
        }
    }

    if (!sConfig.SetSource(cfg_file))
    {
        sLog.outError("Could not find configuration file %s.", cfg_file);
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

#if defined(__GLIBC__)
    // glibc hands each thread its own malloc arena, up to eight per core - so 64
    // on an eight core machine, which is also roughly how many threads this
    // server runs. Every arena keeps what it frees, so a workload that allocates
    // hard and without pause, as a thousand bots do, scatters its freed memory
    // across all of them and returns very little to the system. The symptom is a
    // resident size that climbs steadily at a constant player count while no
    // single call site leaks and malloc_trim keeps giving back the same amount
    // however long the server has been up.
    //
    // Measured on 2026-08-10 with a thousand bots, arenas capped at four: after
    // nine and a half hours RSS was 9.9 GB against 13.4 GB before, and growth
    // fell from around 810 MB an hour to 483. The world tick did not suffer -
    // mean and 95th percentile came out slightly better, the median did not
    // move. Fewer arenas do mean more contention in the allocator, so this is
    // worth measuring rather than assuming on a different machine.
    //
    // Zero leaves glibc alone, which is the right default: this is worth setting
    // on a server carrying a large bot population and pointless on a small one.
    if (int const arenaMax = sConfig.GetIntDefault("Malloc.ArenaMax", 0))
    {
        if (mallopt(M_ARENA_MAX, arenaMax))
            sLog.outString("Malloc arenas limited to %d.", arenaMax);
        else
            sLog.outError("Could not limit malloc arenas to %d.", arenaMax);
    }
#endif

#ifndef WIN32                                               // posix daemon commands need apply after config read
    switch (serviceDaemonMode)
    {
    case 'r':
        startDaemon();
        break;
    case 's':
        stopDaemon();
        break;
    }
#endif

#define STR(s) #s
#define XSTR(s) STR(s)

    sLog.outInfo("Release: " _FULLVERSION);

    DETAIL_LOG("%s (Library: %s)", OPENSSL_VERSION_TEXT, SSLeay_version(SSLEAY_VERSION));
    if (SSLeay() < 0x009080bfL )
    {
        DETAIL_LOG("WARNING: Outdated version of OpenSSL lib. Logins to server may not work!");
        DETAIL_LOG("WARNING: Minimal required version [OpenSSL 0.9.8k]");
    }

    DETAIL_LOG("Using ACE: %s", ACE_VERSION);

    ///- and run the 'Master'
    /// \todo Why do we need this 'Master'? Can't all of this be in the Main as for Realmd?
    return sMaster.Run();

    // at sMaster return function exist with codes
    // 0 - normal shutdown
    // 1 - shutdown at error
    // 2 - restart command used, this code can be used by restarter for restart mangosd
}

/// @}

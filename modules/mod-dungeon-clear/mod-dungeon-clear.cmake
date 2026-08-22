# MSVC: force _USE_MATH_DEFINES onto the compiler command line.
#
# MSVC's <math.h> only defines M_PI (and the rest of the M_* family) when
# _USE_MATH_DEFINES is defined BEFORE math.h is first included; it is #pragma
# once, so setting the macro later has no effect. AzerothCore sets it in
# src/common/Define.h, which works for core translation units because they reach
# Define.h before any standard math header — but a module TU that includes
# <cmath> (directly, or via <algorithm>/<vector>/G3D) before the first core
# header does not, and then every M_PI in that TU is undeclared. That breaks the
# CORE's own headers, not just ours: Position.h::NormalizeOrientation calls
#   std::fmod(o, 2.0f * static_cast<float>(M_PI))
# so the reported "error C2065: 'M_PI': undeclared identifier" is immediately
# followed by the cascade "error C2661: 'fmod': no overloaded function takes 1
# arguments" (the second argument failed to compile, so the call is seen with
# one). Nothing we can do inside our own sources fixes a core header — a
# command-line define is the only ordering-proof place for it.
#
# This file is included from modules/CMakeLists.txt AFTER the targets are
# created, so both linkage modes can be handled here. PRIVATE: it changes how
# these sources compile, nothing downstream. Our own sources additionally avoid
# M_PI entirely (DC_PI in Util/DungeonClearTuning.h), so this is only needed for
# the core headers we include.
# Spelled as a raw /D option rather than target_compile_definitions, and with a
# trailing '=', for one reason: src/common/Define.h line 38 ALSO does
#   #define _USE_MATH_DEFINES
# with an empty replacement list. A bare -D gives the macro the value 1, which is
# a NON-identical redefinition, and MSVC then emits
#   warning C4005: '_USE_MATH_DEFINES': macro redefinition
# once per translation unit — hundreds of lines of noise that bury real
# diagnostics. '/D_USE_MATH_DEFINES=' defines it EMPTY, identical to Define.h's,
# so the redefinition is legal and silent. target_compile_definitions cannot
# express this: CMake escapes "NAME=" into -DNAME="" (verified), which is a value
# of "" and warns just the same.
if (MSVC)
    foreach (DC_MATH_TARGET modules mod_mod-dungeon-clear)
        if (TARGET ${DC_MATH_TARGET})
            target_compile_options(${DC_MATH_TARGET} PRIVATE /D_USE_MATH_DEFINES=)
        endif()
    endforeach()
endif()

if (BUILD_TESTING)
    function(define_dungeon_clear_tests)
        set(MOD_PATH "${CMAKE_SOURCE_DIR}/modules/mod-dungeon-clear")

        # Define our standalone test target
        add_executable(dungeon_clear_tests
            "${MOD_PATH}/t/TestDoorPolicy.cpp"
            "${MOD_PATH}/t/TestDungeonClearMath.cpp"
            "${MOD_PATH}/t/TestHealReposition.cpp"
            "${MOD_PATH}/t/TestCombatRegroup.cpp"
            "${MOD_PATH}/t/TestSmartRest.cpp"
            "${MOD_PATH}/t/TestPostCombatRez.cpp"
            "${MOD_PATH}/t/TestStrandedRecovery.cpp"
            "${MOD_PATH}/t/TestFightInPlace.cpp"
            "${MOD_PATH}/t/TestBossPullback.cpp"
            "${MOD_PATH}/t/TestScriptedPull.cpp"
            "${MOD_PATH}/t/TestSocialQuarantine.cpp"
            "${MOD_PATH}/t/TestSealedEncounter.cpp"
            "${MOD_PATH}/t/TestWaitAtBoss.cpp"
            "${MOD_PATH}/t/TestDungeonClearUtil.cpp"
            "${MOD_PATH}/t/TestDungeonClearApproach.cpp"
            "${MOD_PATH}/t/TestApproachDecisions.cpp"
            "${MOD_PATH}/t/TestDcProgressWatchdog.cpp"
            "${MOD_PATH}/t/TestPullDecisions.cpp"
            "${MOD_PATH}/t/TestScenarioDriver.cpp"
            "${MOD_PATH}/t/TestRoomAggro.cpp"
            "${MOD_PATH}/t/TestNavPenalty.cpp"
            "${MOD_PATH}/t/TestNeverTarget.cpp"
            "${MOD_PATH}/t/TestFactionEntrySwap.cpp"
            "${MOD_PATH}/t/TestDcHazard.cpp"
            "${MOD_PATH}/t/TestDcZoneLine.cpp"
            "${MOD_PATH}/t/TestBossRoster.cpp"
            "${MOD_PATH}/t/TestBossOrdering.cpp"
            "${MOD_PATH}/t/TestDifficultyGate.cpp"
            "${MOD_PATH}/t/TestEventRegistry.cpp"
            "${MOD_PATH}/t/TestDungeonEvent.cpp"
            "${MOD_PATH}/t/TestNavGeometry.cpp"
            "${MOD_PATH}/t/TestSplineWindow.cpp"
            "${MOD_PATH}/t/TestMechanarElevatorProbe.cpp"
            "${MOD_PATH}/t/TestRampartsLedgeProbe.cpp"
            "${MOD_PATH}/t/TestAzjolNerubRouteProbe.cpp"
            "${MOD_PATH}/t/TestStrategyGate.cpp"
            "${MOD_PATH}/t/TestRelevanceLadder.cpp"
            "${MOD_PATH}/t/TestSettingsRegistry.cpp"
            "${MOD_PATH}/t/TestTestRunVerdict.cpp"
            "${MOD_PATH}/t/TestTestDungeonRegistry.cpp"
            "${MOD_PATH}/t/TestTestGearTiers.cpp"
            "${MOD_PATH}/t/TestTestRunRecord.cpp"
            "${MOD_PATH}/t/TestTestRunSelect.cpp"
            "${MOD_PATH}/t/TestWatchHop.cpp"
            "${MOD_PATH}/t/TestTestRoster.cpp"
            "${MOD_PATH}/t/TestTestRunLiveJson.cpp"
            "${MOD_PATH}/t/TestWipeContext.cpp"
            "${MOD_PATH}/t/TestDcDiagSnapshot.cpp"
            "${MOD_PATH}/t/TestTestComp.cpp"
            "${MOD_PATH}/t/TestTestPlanSchedule.cpp"
            "${MOD_PATH}/t/TestTestPlanSummary.cpp"
            "${MOD_PATH}/t/NavHarness.cpp"
            "${MOD_PATH}/t/replay_decisions.cpp"
            "${MOD_PATH}/t/replay_pull.cpp"
            "${CMAKE_SOURCE_DIR}/src/test/mocks/TestMap.cpp"
        )

        # The replay runner reads the captured-decision fixtures from the source
        # tree (the test binary runs from the build dir). Pass the path in.
        # DC_MAPDATA_DIR points the Tier-2 navmesh geometry suite at a sliced
        # mmaps directory (produced by tools/slice_mapdata.py); the suite
        # GTEST_SKIPs every case when it is absent, so a checkout WITHOUT
        # client-derived map data (which is never committed) still builds and
        # passes the rest of the suite.
        target_compile_definitions(dungeon_clear_tests PRIVATE
            DC_FIXTURE_DIR="${MOD_PATH}/t/fixtures"
            DC_MAPDATA_DIR="${MOD_PATH}/t/fixtures/mapdata"
        )

        # Same MSVC math-macro ordering trap as the module sources above — the
        # test TUs include the core's Position.h too. Same empty-value spelling,
        # same reason (see the C4005 note above).
        if (MSVC)
            target_compile_options(dungeon_clear_tests PRIVATE /D_USE_MATH_DEFINES=)
        endif()

        # Link the necessary targets
        target_link_libraries(dungeon_clear_tests
            game
            gtest_main
            gmock_main
            game-interface
            modules
            scripts
        )

        # Include directories
        target_include_directories(dungeon_clear_tests PRIVATE
            "${MOD_PATH}/src"
            "${MOD_PATH}/src/Ai/Dungeon/DungeonClear/Util"
            "${CMAKE_SOURCE_DIR}/src/test/mocks"
        )

        # Place executable directly in the main build folder for easy execution
        set_target_properties(dungeon_clear_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
    endfunction()

    # Defer execution to the root directory's configuration end, after googletest has been fetched by the core
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL define_dungeon_clear_tests)
endif()

# ---------------------------------------------------------------------------
# Tortoise port: reach the vendored playerbots tree.
#
# Upstream this module sits next to mod-playerbots, both of them AzerothCore
# modules compiled into the same `modules` library, so its includes resolve by
# themselves. Here the bot tree is a separate library under
# src/modules/PlayerBots, so the paths and the link have to be stated.
#
# The directory list mirrors what the bot module puts on its own compile line:
# its root, plus the three Penqle paths its headers reach through
# transitively. AcCompat.h - the AzerothCore-to-Penqle name and type shim -
# lives with the module and is force-included ahead of everything, because the
# names it maps appear in the upstream headers themselves, not only in code we
# could edit.
# ---------------------------------------------------------------------------

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "POST_TARGETS")
  target_include_directories(modules
    PUBLIC
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/actions
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/triggers
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/values
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/generic
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/deathknight
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/druid
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/hunter
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/mage
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/paladin
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/priest
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/rogue
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/shaman
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/warlock
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/playerbot/strategy/warrior
      ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/ahbot
      ${CMAKE_SOURCE_DIR}/src/game/MapNodes
      ${CMAKE_SOURCE_DIR}/src/framework/Network
      ${CMAKE_SOURCE_DIR}/dep/recastnavigation
      ${CMAKE_CURRENT_LIST_DIR}/src
      ${CMAKE_CURRENT_LIST_DIR}/src/compat)

  target_link_libraries(modules PUBLIC playerbots)

  target_compile_options(modules PRIVATE
    -include ${CMAKE_CURRENT_LIST_DIR}/src/AcCompat.h)
endif()

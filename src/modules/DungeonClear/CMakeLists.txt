set(LIBRARY_NAME dungeonclear)

file(GLOB DC_ROOT_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
file(GLOB_RECURSE DC_SUB_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/Action/*.[ch]pp
  ${CMAKE_CURRENT_SOURCE_DIR}/Action/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Data/*.[ch]pp
  ${CMAKE_CURRENT_SOURCE_DIR}/Data/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Data/Events/*.[ch]pp
  ${CMAKE_CURRENT_SOURCE_DIR}/Multiplier/*.[ch]pp
  ${CMAKE_CURRENT_SOURCE_DIR}/Multiplier/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Settings/*.[ch]pp
  ${CMAKE_CURRENT_SOURCE_DIR}/Settings/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Strategy/*.[ch]pp
  ${CMAKE_CURRENT_SOURCE_DIR}/Strategy/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Trigger/*.[ch]pp
  ${CMAKE_CURRENT_SOURCE_DIR}/Trigger/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Util/*.[ch]pp
  ${CMAKE_CURRENT_SOURCE_DIR}/Util/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Value/*.[ch]pp
  ${CMAKE_CURRENT_SOURCE_DIR}/Value/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/TestRun/*.[ch]pp
  ${CMAKE_CURRENT_SOURCE_DIR}/TestRun/*.h
)

add_library(${LIBRARY_NAME} STATIC ${DC_ROOT_SRC} ${DC_SUB_SRC})

target_compile_definitions(${LIBRARY_NAME} PRIVATE
  ENABLE_DUNGEON_CLEAR
  ENABLE_PLAYERBOTS
  CMANGOS
  MANGOSBOT_ZERO
)

target_include_directories(${LIBRARY_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots
  ${CMAKE_SOURCE_DIR}/src/modules/PlayerBots/cmangos-compat-stubs
  ${CMAKE_SOURCE_DIR}/src/game
  ${CMAKE_SOURCE_DIR}/src/game/AI
  ${CMAKE_SOURCE_DIR}/src/game/AuctionHouse
  ${CMAKE_SOURCE_DIR}/src/game/Battlegrounds
  ${CMAKE_SOURCE_DIR}/src/game/Chat
  ${CMAKE_SOURCE_DIR}/src/game/Commands
  ${CMAKE_SOURCE_DIR}/src/game/Database
  ${CMAKE_SOURCE_DIR}/src/game/Group
  ${CMAKE_SOURCE_DIR}/src/game/Guild
  ${CMAKE_SOURCE_DIR}/src/game/Handlers
  ${CMAKE_SOURCE_DIR}/src/game/LFG
  ${CMAKE_SOURCE_DIR}/src/game/Mail
  ${CMAKE_SOURCE_DIR}/src/game/Maps
  ${CMAKE_SOURCE_DIR}/src/game/Maps/Pool
  ${CMAKE_SOURCE_DIR}/src/game/Movement
  ${CMAKE_SOURCE_DIR}/src/game/Movement/spline
  ${CMAKE_SOURCE_DIR}/src/game/Objects
  ${CMAKE_SOURCE_DIR}/src/game/OutdoorPvP
  ${CMAKE_SOURCE_DIR}/src/game/PacketBroadcast
  ${CMAKE_SOURCE_DIR}/src/game/Protocol
  ${CMAKE_SOURCE_DIR}/src/game/Spells
  ${CMAKE_SOURCE_DIR}/src/game/Threat
  ${CMAKE_SOURCE_DIR}/src/game/Transports
  ${CMAKE_SOURCE_DIR}/src/game/vmap
  ${CMAKE_SOURCE_DIR}/src/shared
  ${CMAKE_SOURCE_DIR}/src/shared/Config
  ${CMAKE_SOURCE_DIR}/src/shared/Database
  ${CMAKE_SOURCE_DIR}/src/shared/Log
  ${CMAKE_SOURCE_DIR}/src/shared/Util
  ${CMAKE_SOURCE_DIR}/src/framework
  ${CMAKE_SOURCE_DIR}/src/framework/Network
  ${CMAKE_SOURCE_DIR}/dep/include
  ${CMAKE_SOURCE_DIR}/dep/include/g3dlite
  ${CMAKE_SOURCE_DIR}/dep/recastnavigation/Detour/Include
  ${CMAKE_SOURCE_DIR}/dep/re2
  ${CMAKE_SOURCE_DIR}/dep/fmt/include
  ${CMAKE_SOURCE_DIR}/dep/fmt
  ${MYSQL_INCLUDE_DIR}
)

# Reuse playerbots PCH (same botpch.h) — avoids a second ~700MB gch and
# keeps includes consistent. Headers-only dependency: mangosd links both libs.
if(PCH)
  target_precompile_headers(${LIBRARY_NAME} REUSE_FROM playerbots)
endif()

# Do not PUBLIC-link playerbots here: that makes `ninja dungeonclear` rebuild
# the entire bots tree whenever AiObjectContext.h changes. mangosd already
# links playerbots + dungeonclear (--whole-archive).
target_link_libraries(${LIBRARY_NAME} PRIVATE shared)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/conf/dungeon_clear.conf.dist
  DESTINATION etc
  OPTIONAL
)

message(STATUS "DungeonClear module configured")

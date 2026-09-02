set(LIBRARY_NAME groupquestloot)

add_library(${LIBRARY_NAME} STATIC
  GroupQuestLoot.cpp
)

target_include_directories(${LIBRARY_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/src/game
  ${CMAKE_SOURCE_DIR}/src/game/AuctionHouse
  ${CMAKE_SOURCE_DIR}/src/game/Battlegrounds
  ${CMAKE_SOURCE_DIR}/src/game/Chat
  ${CMAKE_SOURCE_DIR}/src/game/Database
  ${CMAKE_SOURCE_DIR}/src/game/Group
  ${CMAKE_SOURCE_DIR}/src/game/Handlers
  ${CMAKE_SOURCE_DIR}/src/game/LFG
  ${CMAKE_SOURCE_DIR}/src/game/MapNodes
  ${CMAKE_SOURCE_DIR}/src/game/Maps
  ${CMAKE_SOURCE_DIR}/src/game/Maps/Pool
  ${CMAKE_SOURCE_DIR}/src/game/Movement
  ${CMAKE_SOURCE_DIR}/src/game/Movement/spline
  ${CMAKE_SOURCE_DIR}/src/game/Objects
  ${CMAKE_SOURCE_DIR}/src/game/Protocol
  ${CMAKE_SOURCE_DIR}/src/game/Spells
  ${CMAKE_SOURCE_DIR}/src/game/Threat
  ${CMAKE_SOURCE_DIR}/src/game/vmap
  ${CMAKE_SOURCE_DIR}/src/framework
  ${CMAKE_SOURCE_DIR}/src/framework/Network
  ${CMAKE_SOURCE_DIR}/src/shared
  ${CMAKE_SOURCE_DIR}/dep/include
  ${CMAKE_SOURCE_DIR}/dep/include/g3dlite
  ${CMAKE_SOURCE_DIR}/dep/recastnavigation/Detour
  ${CMAKE_SOURCE_DIR}/dep/recastnavigation
  ${CMAKE_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/src/shared
  ${MYSQL_INCLUDE_DIR}
  ${TBB_INCLUDE_DIRS}
  ${ACE_INCLUDE_DIR}
  ${OPENSSL_INCLUDE_DIR}
)

if(WIN32)
  target_include_directories(${LIBRARY_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/dep/include-windows
  )
endif()

message(STATUS "GroupQuestLoot module configured")

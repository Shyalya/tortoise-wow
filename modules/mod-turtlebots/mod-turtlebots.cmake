# Optional CMake hooks for mod-turtlebots.
#
# Most modules do not need custom CMake here. Source files under src/ are discovered automatically,
# conf/*.conf.dist files are copied automatically, and Addmod_turtlebotsScripts() is invoked by the
# generated module loader.
#
# This file is included twice:
#   TORTOISE_MODULE_CMAKE_PHASE=DISCOVERY
#   TORTOISE_MODULE_CMAKE_PHASE=POST_TARGETS
#
# Use TW_* helpers for compatibility with this core's module system. Do not use AzerothCore AC_* names.

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "DISCOVERY")
  # Example for legacy-style extra script sources:
  # TW_ADD_SCRIPTS("${CMAKE_CURRENT_LIST_DIR}/src/legacy")
  # TW_ADD_SCRIPT_LOADER(mod_turtlebots "legacy_loader.h")
endif()

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "POST_TARGETS")
  # mod-turtlebots feeds its residents into TortoiseBots' telemetry emitter, so it
  # needs that module's runtime/ headers on the include path. Both modules link
  # statically into mangosd, so the emitter symbols resolve at the final link.
  if(DEFINED TORTOISE_CURRENT_MODULE_TARGET AND NOT "${TORTOISE_CURRENT_MODULE_TARGET}" STREQUAL "")
    set(_TBOTS_TARGET "${TORTOISE_CURRENT_MODULE_TARGET}")
  elseif(TORTOISE_CURRENT_MODULE_LINKAGE STREQUAL "static")
    set(_TBOTS_TARGET modules)
  else()
    GetModuleProjectName("${TORTOISE_CURRENT_MODULE}" _TBOTS_TARGET)
  endif()
  if(TARGET "${_TBOTS_TARGET}")
    target_include_directories("${_TBOTS_TARGET}" PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../TortoiseBots/runtime")
  endif()
endif()

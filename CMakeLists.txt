# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#

cmake_minimum_required (VERSION 3.16)

project (ProjectAlice LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CMake's job pools are scoped to one build tree. Serialize compiler processes
# across all Project Alice checkouts on this machine as well, so two agents or
# IDEs cannot each start their own set of memory-heavy Clang processes.
option(ALICE_SERIALIZE_COMPILERS "Serialize Project Alice compiler processes machine-wide" ON)
if(UNIX AND ALICE_SERIALIZE_COMPILERS)
	find_package(Python3 COMPONENTS Interpreter REQUIRED)
	set(_ALICE_COMPILER_GATE "${PROJECT_SOURCE_DIR}/tools/serialize_compiler.py")
	set(_ALICE_C_COMPILER_LAUNCHER "${Python3_EXECUTABLE};${_ALICE_COMPILER_GATE}")
	if(CMAKE_C_COMPILER_LAUNCHER)
		list(APPEND _ALICE_C_COMPILER_LAUNCHER ${CMAKE_C_COMPILER_LAUNCHER})
	endif()
	set(CMAKE_C_COMPILER_LAUNCHER "${_ALICE_C_COMPILER_LAUNCHER}")
	set(_ALICE_CXX_COMPILER_LAUNCHER "${Python3_EXECUTABLE};${_ALICE_COMPILER_GATE}")
	if(CMAKE_CXX_COMPILER_LAUNCHER)
		list(APPEND _ALICE_CXX_COMPILER_LAUNCHER ${CMAKE_CXX_COMPILER_LAUNCHER})
	endif()
	set(CMAKE_CXX_COMPILER_LAUNCHER "${_ALICE_CXX_COMPILER_LAUNCHER}")
endif()

# Keep a small per-build cap too, including builds that opt out of the global
# compiler gate or use a generator without compiler launcher support.
if(CMAKE_GENERATOR STREQUAL "Ninja")
	set_property(GLOBAL APPEND PROPERTY JOB_POOLS alice_compile_pool=1 alice_link_pool=1)
	set(CMAKE_JOB_POOL_COMPILE alice_compile_pool)
	set(CMAKE_JOB_POOL_LINK alice_link_pool)
endif()

# A build without an explicit configuration is a player-facing build.  Keep
# Debug available for developers, but do not silently ship an unoptimised game.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Turn on compile_commands.json export for wider editor/IDE support.
# This option is ignored on anything but Unix makefiles/Ninja.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

if(WIN32)
	string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
	string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG})
endif()

list(APPEND ALICE_SOURCE_BLOB "${PROJECT_SOURCE_DIR}/src/main.cpp")

# Keep src/main.cpp as the checked-in source manifest for the legacy game
# implementation, but compile the listed files separately for the player
# executable. This makes a one-file edit rebuild only that translation unit
# and bounds compiler memory to one implementation file at a time.
file(GLOB_RECURSE ALICE_CPP_FILES CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/*.cpp")
file(STRINGS "${PROJECT_SOURCE_DIR}/src/main.cpp" _ALICE_MAIN_CPP_INCLUDES
	REGEX [=[^#include "[^"]+\.cpp"$]=])
set(ALICE_MAIN_TRANSLATION_UNITS)
foreach(_include_line IN LISTS _ALICE_MAIN_CPP_INCLUDES)
	string(REGEX REPLACE [=[^#include "([^"]+\.cpp)"$]=] [=[\1]=]
		_included_source "${_include_line}")
	get_filename_component(_included_basename "${_included_source}" NAME)
	if(_included_basename STREQUAL "common_types.cpp")
		# DataContainer provides this implementation through FetchContent below.
		continue()
	endif()

	set(_source_matches)
	if(EXISTS "${PROJECT_SOURCE_DIR}/src/${_included_source}")
		list(APPEND _source_matches "${PROJECT_SOURCE_DIR}/src/${_included_source}")
	else()
		foreach(_candidate IN LISTS ALICE_CPP_FILES)
			get_filename_component(_candidate_basename "${_candidate}" NAME)
			if(_candidate_basename STREQUAL "${_included_basename}")
				list(APPEND _source_matches "${_candidate}")
			endif()
		endforeach()
	endif()

	list(LENGTH _source_matches _source_match_count)
	if(NOT _source_match_count EQUAL 1)
		message(FATAL_ERROR
			"Expected one source for src/main.cpp include '${_included_source}', found ${_source_match_count}")
	endif()
	list(GET _source_matches 0 _translation_unit)
	if(_translation_unit IN_LIST ALICE_MAIN_TRANSLATION_UNITS)
		message(FATAL_ERROR "Duplicate src/main.cpp source: ${_translation_unit}")
	endif()
	list(APPEND ALICE_MAIN_TRANSLATION_UNITS "${_translation_unit}")
endforeach()

# src/main.cpp still hosts both runtimes. OUR TIME is the canonical runtime;
# the classic block contains the old Victoria 2 political and economic engines
# as well as its UI, map, and content pipeline. Keep a separate manifest and
# reject those engines if they ever cross the boundary.
file(STRINGS "${PROJECT_SOURCE_DIR}/src/main.cpp" _ALICE_MAIN_SOURCE_LINES)
set(_ALICE_IN_CLASSIC_GAME_BLOCK FALSE)
set(_ALICE_OUR_TIME_CPP_INCLUDES)
foreach(_source_line IN LISTS _ALICE_MAIN_SOURCE_LINES)
	if(_source_line STREQUAL "#ifndef INCREMENTAL")
		set(_ALICE_IN_CLASSIC_GAME_BLOCK TRUE)
	endif()
	if(NOT _ALICE_IN_CLASSIC_GAME_BLOCK AND _source_line MATCHES [=[^#include "[^"]+\.cpp"$]=])
		list(APPEND _ALICE_OUR_TIME_CPP_INCLUDES "${_source_line}")
	endif()
endforeach()

set(ALICE_OUR_TIME_TRANSLATION_UNITS)
set(_ALICE_OUR_TIME_FORBIDDEN_V2_SYSTEMS
	"nations.cpp"
	"diplomatic_crisis_dynamics.cpp"
	"politics.cpp"
	"economy.cpp"
	"economy_stats.cpp"
	"price_level.cpp"
	"market_clearing.cpp"
	"commodity_logistics.cpp"
	"investment_ranking.cpp"
	"market_access.cpp"
	"cargo_transit.cpp"
	"human_development.cpp"
	"world_trade_capacity.cpp"
	"economy_pops.cpp"
	"economy_trade_routes.cpp"
	"economy_government.cpp"
	"economy_production.cpp"
	"construction.cpp"
	"advanced_province_buildings.cpp"
	"demographics.cpp"
)
set(_ALICE_OUR_TIME_EXCLUDED_SOURCES
	"lua_alice_api.cpp"
	"system_state.cpp"
	"dcon_oos_reporter_generated.cpp"
	"firm_agency.cpp"
	"industrial_production.cpp"
	"payroll.cpp"
	"capital_projects.cpp"
)
foreach(_include_line IN LISTS _ALICE_OUR_TIME_CPP_INCLUDES)
	string(REGEX REPLACE [=[^#include "([^"]+\.cpp)"$]=] [=[\1]=]
		_included_source "${_include_line}")
	get_filename_component(_included_basename "${_included_source}" NAME)
	if(_included_basename IN_LIST _ALICE_OUR_TIME_FORBIDDEN_V2_SYSTEMS)
		message(FATAL_ERROR
			"Classic Victoria 2 system '${_included_source}' crossed into the OUR TIME runtime")
	endif()
	if(_included_basename STREQUAL "common_types.cpp"
		OR _included_source STREQUAL "compat/alice/legacy_bridge.cpp"
		OR _included_basename IN_LIST _ALICE_OUR_TIME_EXCLUDED_SOURCES)
		# DataContainer supplies the first implementation. The V2 import adapter,
		# UI-owned state implementation, Lua bridge, and factory/construction
		# adapters that still depend on V2 province/factory state stay out of the
		# standalone fiscal lab runtime.
		continue()
	endif()

	set(_source_matches)
	if(EXISTS "${PROJECT_SOURCE_DIR}/src/${_included_source}")
		list(APPEND _source_matches "${PROJECT_SOURCE_DIR}/src/${_included_source}")
	else()
		foreach(_candidate IN LISTS ALICE_CPP_FILES)
			get_filename_component(_candidate_basename "${_candidate}" NAME)
			if(_candidate_basename STREQUAL "${_included_basename}")
				list(APPEND _source_matches "${_candidate}")
			endif()
		endforeach()
	endif()

	list(LENGTH _source_matches _source_match_count)
	if(NOT _source_match_count EQUAL 1)
		message(FATAL_ERROR
			"Expected one OUR TIME source for src/main.cpp include '${_included_source}', found ${_source_match_count}")
	endif()
	list(GET _source_matches 0 _translation_unit)
	if(_translation_unit IN_LIST ALICE_OUR_TIME_TRANSLATION_UNITS)
		message(FATAL_ERROR "Duplicate OUR TIME source: ${_translation_unit}")
	endif()
	list(APPEND ALICE_OUR_TIME_TRANSLATION_UNITS "${_translation_unit}")
endforeach()
list(APPEND ALICE_OUR_TIME_TRANSLATION_UNITS
	"${PROJECT_SOURCE_DIR}/src/gamestate/sovereign_debt_crisis_lab.cpp")

list(APPEND ALICE_CORE_SOURCES_LIST
	"${PROJECT_SOURCE_DIR}/src/economy/social_entitlements.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/collective_labor.cpp"
	"${PROJECT_SOURCE_DIR}/src/nations/strategic_statecraft.cpp"
	"${PROJECT_SOURCE_DIR}/src/map/projections.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/alice_ui.cpp"
	"${PROJECT_SOURCE_DIR}/src/map/map_state.cpp"
	"${PROJECT_SOURCE_DIR}/src/map/map_data_loading.cpp"
	"${PROJECT_SOURCE_DIR}/src/map/map_borders.cpp"
	"${PROJECT_SOURCE_DIR}/src/map/map.cpp"
	"${PROJECT_SOURCE_DIR}/src/graphics/xac.cpp"	
	"${PROJECT_SOURCE_DIR}/src/graphics/stateless_render.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/graphics.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/lunasvg.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgelement.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svggeometryelement.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svglayoutstate.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgpaintelement.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgparser.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgproperty.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgrenderstate.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgtextelement.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-blend.c"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-canvas.c"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-font.c"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-ft-math.c"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-ft-raster.c"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-ft-stroker.c"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-matrix.c"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-paint.c"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-path.c"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-rasterize.c"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-surface.c"
	"${PROJECT_SOURCE_DIR}/src/gui/production.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/province_factories.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/factory_details.cpp"
)
set_source_files_properties(src/lunasvg/plutovg-blend.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/lunasvg/plutovg-canvas.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/lunasvg/plutovg-font.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/lunasvg/plutovg-ft-math.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/lunasvg/plutovg-ft-raster.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/lunasvg/plutovg-ft-stroker.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/lunasvg/plutovg-matrix.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/lunasvg/plutovg-paint.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/lunasvg/plutovg-path.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/lunasvg/plutovg-rasterize.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(src/lunasvg/plutovg-surface.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

list(APPEND ALICE_INCREMENTAL_SOURCES_LIST_AI
	"${PROJECT_SOURCE_DIR}/src/ai/ai.cpp"
	"${PROJECT_SOURCE_DIR}/src/ai/ai_campaign.cpp"
	"${PROJECT_SOURCE_DIR}/src/ai/ai_campaign_values.cpp"
	"${PROJECT_SOURCE_DIR}/src/ai/ai_focuses.cpp"
	"${PROJECT_SOURCE_DIR}/src/ai/ai_alliances.cpp"
	"${PROJECT_SOURCE_DIR}/src/ai/ai_economy.cpp"
)
set_source_files_properties(
	${ALICE_INCREMENTAL_SOURCES_LIST_AI}
	PROPERTIES UNITY_GROUP "bucket-AI"
)

list(APPEND ALICE_INCREMENTAL_SOURCES_LIST_ECONOMY
	"${PROJECT_SOURCE_DIR}/src/economy/economy_stats.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/economy.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/price_level.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/market_clearing.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/commodity_logistics.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/investment_ranking.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/market_access.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/foreign_exchange.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/cargo_transit.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/human_development.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/world_trade_capacity.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/economy_pops.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/economy_trade_routes.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/economy_government.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/economy_production.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/construction.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/advanced_province_buildings.cpp"
)
set_source_files_properties(
	${ALICE_INCREMENTAL_SOURCES_LIST_ECONOMY}
	PROPERTIES UNITY_GROUP "bucket-ECONOMY"
)

list(APPEND ALICE_INCREMENTAL_SOURCES_LIST_GUI_TOPBAR
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/military_subwindows/gui_leaders_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/military_subwindows/gui_stats_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/military_subwindows/gui_units_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/military_subwindows/gui_build_unit_large_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/politics_subwindows/gui_decision_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/politics_subwindows/gui_release_nation_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/production_subwindows/gui_factory_refit_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/gui_population_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/gui_production_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/gui_technology_window.cpp"
)
set_source_files_properties(
	${ALICE_INCREMENTAL_SOURCES_LIST_GUI_TOPBAR}
	PROPERTIES UNITY_GROUP "bucket-GUI-TOPBAR"
)

list(APPEND ALICE_INCREMENTAL_SOURCES_LIST_GUI_OTHER
	"${PROJECT_SOURCE_DIR}/src/gui/gui_message_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_chat_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_combat.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_tooltips.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_units.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_unit_panel_subwindow.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_diplomacy_request.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_lookup.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/gui_trade_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_leader_tooltip.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_modifier_tooltips.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/province_tiles/province_tiles.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_province_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_trigger_tooltips.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_error_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/map_tooltip.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/immediate_mode.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/economy_viewer.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/unit_tooltip.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_context_window.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/labour_details.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_common_elements.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_effect_tooltips.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/widgets/table.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_graphics.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_leaders.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/ui_state.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_element_base.cpp"
	"${PROJECT_SOURCE_DIR}/src/gui/gui_other.cpp"
)
set_source_files_properties(
	${ALICE_INCREMENTAL_SOURCES_LIST_GUI_OTHER}
	PROPERTIES UNITY_GROUP "bucket-GUI-OTHER"
)

list(APPEND ALICE_INCREMENTAL_SOURCES_LIST_SVG	
	"${PROJECT_SOURCE_DIR}/src/lunasvg/graphics.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/lunasvg.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgelement.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svggeometryelement.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svglayoutstate.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgpaintelement.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgparser.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgproperty.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgrenderstate.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/svgtextelement.cpp"
	"${PROJECT_SOURCE_DIR}/src/graphics/asvg.cpp"
)
set_source_files_properties(
	${ALICE_INCREMENTAL_SOURCES_LIST_SVG}
	PROPERTIES UNITY_GROUP "bucket-SVG_TOOLS"
)

list(APPEND ALICE_INCREMENTAL_SOURCES_LIST_SCRIPTING	
	"${PROJECT_SOURCE_DIR}/src/scripting/effects.cpp"
	"${PROJECT_SOURCE_DIR}/src/scripting/events.cpp"
	"${PROJECT_SOURCE_DIR}/src/scripting/triggers.cpp"
	"${PROJECT_SOURCE_DIR}/src/scripting/fif_triggers.cpp"
)
set_source_files_properties(
	${ALICE_INCREMENTAL_SOURCES_LIST_SCRIPTING}
	PROPERTIES UNITY_GROUP "bucket-SCRIPTING"
)

list(APPEND ALICE_INCREMENTAL_SOURCES_LIST_GAMESTATE	
	"${PROJECT_SOURCE_DIR}/src/gamestate/commands.cpp"
	"${PROJECT_SOURCE_DIR}/src/gamestate/diplomatic_messages.cpp"
	"${PROJECT_SOURCE_DIR}/src/gamestate/modifiers.cpp"
	"${PROJECT_SOURCE_DIR}/src/gamestate/notifications.cpp"
	"${PROJECT_SOURCE_DIR}/src/gamestate/serialization.cpp"
	"${PROJECT_SOURCE_DIR}/src/gamestate/game_scene.cpp"
	"${PROJECT_SOURCE_DIR}/src/gamerule/gamerule.cpp"
	"${PROJECT_SOURCE_DIR}/src/gamestate/uitemplate_serialization.cpp"
	"${PROJECT_SOURCE_DIR}/src/gamestate/user_interactions.cpp"
	"${PROJECT_SOURCE_DIR}/src/gamestate/create_windows.cpp"
)
set_source_files_properties(
	${ALICE_INCREMENTAL_SOURCES_LIST_GAMESTATE}
	PROPERTIES UNITY_GROUP "bucket-GAMESTATE"
)

list(APPEND ALICE_INCREMENTAL_SOURCES_LIST_RARELY_CHANGED	
	"${PROJECT_SOURCE_DIR}/src/network/SHA512.cpp"
	"${PROJECT_SOURCE_DIR}/src/network/network.cpp"
	"${PROJECT_SOURCE_DIR}/src/parsing/float_from_chars.cpp"
	"${PROJECT_SOURCE_DIR}/src/parsing/parsers.cpp"
	"${PROJECT_SOURCE_DIR}/src/platform_specific.cpp"
	"${PROJECT_SOURCE_DIR}/src/text/bmfont.cpp"
	"${PROJECT_SOURCE_DIR}/src/text/fonts.cpp"
	"${PROJECT_SOURCE_DIR}/src/text/text.cpp"
	"${PROJECT_SOURCE_DIR}/src/zstd/zstd.cpp"
	"${PROJECT_SOURCE_DIR}/src/network/pcp.cpp"
	"${PROJECT_SOURCE_DIR}/src/common_types/blake2.cpp"
	"${PROJECT_SOURCE_DIR}/src/common_types/date_interface.cpp"
	"${PROJECT_SOURCE_DIR}/src/common_types/prng.cpp"
	"${PROJECT_SOURCE_DIR}/src/common_types/debug_string_convertions.cpp"
)
set_source_files_properties(
	${ALICE_INCREMENTAL_SOURCES_LIST_RARELY_CHANGED}
	PROPERTIES UNITY_GROUP "bucket-GAMESTATE"
)

list(APPEND ALICE_INCREMENTAL_SOURCES_LIST
	${ALICE_CORE_SOURCES_LIST}
	${ALICE_INCREMENTAL_SOURCES_LIST_AI}
	"${PROJECT_SOURCE_DIR}/src/culture/culture.cpp"
	"${PROJECT_SOURCE_DIR}/src/culture/politics.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/demographics.cpp"
	${ALICE_INCREMENTAL_SOURCES_LIST_ECONOMY}
	"${PROJECT_SOURCE_DIR}/src/graphics/opengl_wrapper.cpp"
	"${PROJECT_SOURCE_DIR}/src/graphics/texture.cpp"
	${ALICE_INCREMENTAL_SOURCES_LIST_GUI_TOPBAR}
	${ALICE_INCREMENTAL_SOURCES_LIST_GUI_OTHER}
	${ALICE_INCREMENTAL_SOURCES_LIST_GAMESTATE}
	"${PROJECT_SOURCE_DIR}/src/map/map_modes.cpp"
	"${PROJECT_SOURCE_DIR}/src/military/military.cpp"
	"${PROJECT_SOURCE_DIR}/src/military/land_forces.cpp"
	"${PROJECT_SOURCE_DIR}/src/nations/nations.cpp"
	"${PROJECT_SOURCE_DIR}/src/nations/diplomatic_crisis_dynamics.cpp"
	"${PROJECT_SOURCE_DIR}/src/provinces/province.cpp"
	${ALICE_INCREMENTAL_SOURCES_LIST_RARELY_CHANGED}
	${ALICE_INCREMENTAL_SOURCES_LIST_SCRIPTING}
	${ALICE_INCREMENTAL_SOURCES_LIST_SVG}
)

if(WIN32)
add_executable(Alice WIN32
	${ALICE_SOURCE_BLOB}
	${ALICE_MAIN_TRANSLATION_UNITS}
	${ALICE_CORE_SOURCES_LIST}
	"${PROJECT_SOURCE_DIR}/src/alice.rc"
	${ASSET_FILES})
add_executable(AliceIncremental WIN32 EXCLUDE_FROM_ALL
	${ALICE_SOURCE_BLOB}
	${ALICE_INCREMENTAL_SOURCES_LIST}
	"${PROJECT_SOURCE_DIR}/src/alice.rc"
	${ASSET_FILES})
add_executable(AliceProfile WIN32 EXCLUDE_FROM_ALL
	${ALICE_SOURCE_BLOB}
	${ALICE_INCREMENTAL_SOURCES_LIST}
	"${PROJECT_SOURCE_DIR}/src/alice.rc"
	${ASSET_FILES})
else()
add_executable(Alice WIN32
	${ALICE_SOURCE_BLOB}
	${ALICE_MAIN_TRANSLATION_UNITS}
	${ALICE_CORE_SOURCES_LIST}
	${ASSET_FILES})
add_executable(AliceIncremental WIN32 EXCLUDE_FROM_ALL
	${ALICE_SOURCE_BLOB}
	${ALICE_INCREMENTAL_SOURCES_LIST}
	${ASSET_FILES})
endif()

set_target_properties(
	AliceIncremental
	PROPERTIES
	UNITY_BUILD_MODE GROUP
)

if(WIN32)
set_target_properties(
	AliceProfile
	PROPERTIES
	UNITY_BUILD_MODE GROUP
)
target_compile_definitions(AliceProfile PRIVATE INCREMENTAL=1)
target_compile_definitions(AliceProfile PRIVATE ALICE_NO_ENTRY_POINT=1)
target_compile_definitions(AliceProfile PRIVATE ALICE_PROFILE_ENTRY_POINT=1)
target_compile_definitions(AliceProfile PRIVATE DO_NOT_USE_LLVM=1)
endif()

target_compile_definitions(AliceIncremental PRIVATE INCREMENTAL=1)
target_compile_definitions(Alice PRIVATE GLM_ENABLE_EXPERIMENTAL ALICE_SPLIT_BUILD=1 DCON_TRAP_INVALID_STORE=1)
target_compile_definitions(AliceIncremental PRIVATE GLM_ENABLE_EXPERIMENTAL)
if(NOT WIN32)
	add_compile_definitions(PREFER_ONE_TBB)
endif()

# Include sub-projects.
add_subdirectory(dependencies EXCLUDE_FROM_ALL)
add_subdirectory(ParserGenerator EXCLUDE_FROM_ALL)
add_subdirectory(FifInterfaceGenerator EXCLUDE_FROM_ALL)

# The original amalgamation also contains DataContainer's common type
# implementation, which is supplied by the fetched dependency rather than
# the repository's src tree.
set(ALICE_DCON_COMMON_TYPES_SOURCE
	"${datacontainer_SOURCE_DIR}/CommonIncludes/common_types.cpp")
if(NOT EXISTS "${ALICE_DCON_COMMON_TYPES_SOURCE}")
	message(FATAL_ERROR "DataContainer common_types.cpp was not found: ${ALICE_DCON_COMMON_TYPES_SOURCE}")
endif()
list(APPEND ALICE_MAIN_TRANSLATION_UNITS "${ALICE_DCON_COMMON_TYPES_SOURCE}")
target_sources(Alice PRIVATE "${ALICE_DCON_COMMON_TYPES_SOURCE}")

add_library(AliceCommon INTERFACE)
target_compile_definitions(AliceCommon INTERFACE GLM_ENABLE_EXPERIMENTAL)
target_compile_definitions(AliceCommon INTERFACE "PROJECT_ROOT=\"${PROJECT_SOURCE_DIR}\"")
if(WIN32)
	# string(REPLACE "/GR" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
	# string(REPLACE "/W3" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
	# string(REPLACE "/MDd" "" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
	# string(REPLACE "/MD" "" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
	# string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
	# string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
	# string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})

	# To build Non-AVX version : change all "/arch:AVX2" to "/d2archSSE42"
	# -march=nehalem for clang release
	set(CMAKE_CXX_FLAGS "")
	set(CMAKE_CXX_FLAGS_DEBUG "")
	set(CMAKE_CXX_FLAGS_RELEASE "")
	if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	# All common flags for all Clang Windows builds
	set(CommonClangWinFlags /bigobj /wd4100 /wd4189 /wd4065 /wd4201 /wd4324 /GR- /W4 /permissive- /WX /GF /w34388 /w34389 -Wno-unused-macros -Wno-array-bounds-pointer-arithmetic -Wno-unused-lambda-capture -Wno-zero-length-array -Wno-switch-default -Wno-reserved-identifier -Wno-unused-parameter -Wno-unused-variable -Wno-unused-private-field /Z7 -Wno-invalid-offsetof -Wno-deprecated-volatile -Wno-missing-prototypes -Wno-implicit-int-float-conversion -Wno-unsafe-buffer-usage -Wno-float-equal -Wno-shadow-field-in-constructor -Wno-gnu-anonymous-struct -Wno-nested-anon-types -Wno-double-promotion -Wno-shadow-uncaptured-local -Wno-nonportable-system-include-path -Wno-format-nonliteral -Wno-shorten-64-to-32 -Wno-ctad-maybe-unsupported -Wno-implicit-int-conversion -Wno-disabled-macro-expansion -Wno-comma -Wno-cast-function-type-strict -Wno-cast-function-type -Wno-invalid-offsetof -Wno-microsoft-cast -Wno-unused-but-set-variable -Wno-padded -Wno-unique-object-duplication -Wno-nrvo -Wno-declaration-after-statement -Wno-undefined-func-template -Wno-bad-function-cast -Wno-unused-function -Wno-cast-qual -Wno-format-security -Wno-overriding-option -Wno-suggest-destructor-override)
		if(OPTIMIZE_MODE STREQUAL "On")
			message(STATUS "Optimizing with PGO data")
			target_compile_options(AliceCommon INTERFACE
				${CommonClangWinFlags}  /DNDEBUG /wd4530 /MT /O2 /Oi /sdl- /GS- /Gy /Gw /Zc:inline -Wno-profile-instr-missing -Wno-profile-instr-out-of-date -fprofile-instr-use=code.profdata)
			target_link_options(AliceCommon INTERFACE /OPT:REF /OPT:ICF /LTCG -fprofile-instr-use=code.profdata)
		elseif(PROFILE_MODE STREQUAL "On")
			message(STATUS "Compiling for PGO instrumentation")
			target_compile_options(AliceCommon INTERFACE
				${CommonClangWinFlags} /DNDEBUG /wd4530 /MT /O2 /Oi /sdl- /GS- /Gy /Gw /Zc:inline -fprofile-instr-generate)
			target_link_options(AliceCommon INTERFACE /DEBUG:FULL /OPT:REF /OPT:ICF /LTCG -fprofile-instr-generate)
		else()
			target_compile_options(AliceCommon INTERFACE
											${CommonClangWinFlags} -Wno-nontrivial-memcall
				$<$<CONFIG:Debug>:			/RTC1 /EHsc /MTd /Od>
				$<$<NOT:$<CONFIG:Debug>>: 	/DNDEBUG /wd4530 /MT /O2 /Oi /sdl- /GS- /Gy /Gw /Zc:inline>)
			target_link_options(AliceCommon INTERFACE
				$<$<CONFIG:Debug>: 			/DEBUG:FULL >
				$<$<NOT:$<CONFIG:Debug>>: 	/OPT:REF /OPT:ICF /LTCG>)
		endif()
		if(VECTOR_MODE STREQUAL "AVX512")
			target_compile_options(AliceCommon INTERFACE
			/arch:AVX512 )
		elseif(VECTOR_MODE STREQUAL "AVX2")
			target_compile_options(AliceCommon INTERFACE
			/arch:AVX2 )
		else()
			target_compile_options(AliceCommon INTERFACE
			-march=nehalem /clang:-fno-fast-math /clang:-ffp-model=precise /clang:-ffp-contract=off )
		endif()

	else()
		set_target_properties(AliceCommon PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR})
		# common MSVC flags for all builds
		set(CommonMSVCFlags /bigobj /wd4100 /wd4189 /wd4065 /wd4201 /wd4324 /GR- /W4 /permissive- /Zc:preprocessor /GF /w34388 /w34389 /WX)
		# /arch:AVX512
		# /d2archSSE42
		if(OPTIMIZE_MODE STREQUAL "On")
			message(STATUS "Optimizing with PGO data")
			target_compile_options(AliceCommon INTERFACE
				${CommonMSVCFlags} /wd4723  /d2archSSE42 /DNDEBUG /wd4530 /MT /O2 /Oi /GL /sdl- /GS- /Gy /Gw /Zc:inline)
			target_link_options(AliceCommon INTERFACE /OPT:REF /OPT:ICF /LTCG /USEPROFILE)
		elseif(PROFILE_MODE STREQUAL "On")
			message(STATUS "Compiling for PGO instrumentation")
			target_compile_options(AliceCommon INTERFACE
				 ${CommonMSVCFlags} /wd4723 /d2archSSE42 /Z7 /DNDEBUG /wd4530 /MT /O2 /Oi /GL /sdl- /GS- /Gy /Gw /Zc:inline)
			target_link_options(AliceCommon INTERFACE /DEBUG:FULL /OPT:REF /OPT:ICF /LTCG /GENPROFILE)
		else()
			message(STATUS "Normal, not PGO, build")
			target_compile_options(AliceCommon INTERFACE
				  ${CommonMSVCFlags} /Z7
				  $<$<CONFIG:Debug>: /EHsc /MTd /RTC1 /Od>
# for faster debug builds, replace /RTC1 /Od with /O1 -- should be able to use /Ox, but for some reason that crashes ZSTD
				  $<$<NOT:$<CONFIG:Debug>>: 	/DNDEBUG /wd4530 /MT /O2 /Oi /GL /sdl- /GS- /Gy /Gw /Zc:inline>)
			target_link_options(AliceCommon INTERFACE
				$<$<CONFIG:Debug>: 			/DEBUG:FULL >
				$<$<NOT:$<CONFIG:Debug>>: 	/DEBUG:FULL /OPT:REF /OPT:ICF /LTCG>)
		endif()

		if(VECTOR_MODE STREQUAL "AVX512")
			target_compile_options(AliceCommon INTERFACE
			/arch:AVX512 )
		elseif(VECTOR_MODE STREQUAL "AVX2")
			target_compile_options(AliceCommon INTERFACE
			/arch:AVX2 )
		else()
			# No /fp:precise here since from my testing, seperate executables generated with Clang and MSVC will not be deterministic between eachother anyway no matter the flags. Both builds must be compiled with clang to get some level of float determinism
			target_compile_options(AliceCommon INTERFACE
			/d2archSSE42 )
		endif()





	endif()
else() # GCC or CLANG
	if(APPLE)
		target_link_options(Alice INTERFACE -rdynamic)
		target_link_options(AliceIncremental INTERFACE -rdynamic)
		target_link_options(AliceCommon INTERFACE -rdynamic)
	else()
		target_link_options(Alice INTERFACE -rdynamic -Wl,-E)
		target_link_options(AliceIncremental INTERFACE -rdynamic -Wl,-E)
		target_link_options(AliceCommon INTERFACE -rdynamic -Wl,-E)
	endif()
	set(ALICE_X86_CPU FALSE)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
		set(ALICE_X86_CPU TRUE)
	endif()
	# See for FMA support: https://stackoverflow.com/questions/21001388/fma3-in-gcc-how-to-enable
	if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		target_compile_options(AliceCommon INTERFACE
			-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-variable -Wno-switch -Wdangling-else -Wno-unused-private-field -Wno-invalid-offsetof -Wno-unused-but-set-variable -Wno-microsoft-cast -Wno-declaration-after-statement  #-Werror
			$<$<CONFIG:Debug>:			-g -pipe>
			$<$<NOT:$<CONFIG:Debug>>: 	-O3 -pipe -fno-rtti -DNDEBUG>)
	else()
		target_compile_options(AliceCommon INTERFACE
										# -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-switch -Wno-unused-private-field -Wno-unused-but-set-variable -Wno-parentheses
			$<$<CONFIG:Debug>:			-g -pipe>
			$<$<NOT:$<CONFIG:Debug>>: 	-O3 -pipe -fno-rtti -DNDEBUG>)
	endif()

	if(VECTOR_MODE STREQUAL "AVX512" AND ALICE_X86_CPU)
		target_compile_options(AliceCommon INTERFACE
		-mavx512f -mavx512cd -mavx512bw -mavx512dq -mavx512vl -mfma )
	elseif(VECTOR_MODE STREQUAL "AVX2" AND ALICE_X86_CPU)
		target_compile_options(AliceCommon INTERFACE
		-mavx2 -mfma )
	elseif(ALICE_X86_CPU)
		target_compile_options(AliceCommon INTERFACE
		-march=nehalem -fno-fast-math -ffp-model=precise -ffp-contract=off )
	else()
		target_compile_options(AliceCommon INTERFACE
		-fno-fast-math -ffp-model=precise -ffp-contract=off )
	endif()


	target_link_libraries(AliceCommon INTERFACE dl)
endif()
target_link_libraries(AliceCommon INTERFACE dependency_DataContainer)
target_link_libraries(AliceCommon INTERFACE dependency_DataContainerLua)
target_link_libraries(AliceCommon INTERFACE dependency_DataContainerOOSReporter)
target_link_libraries(AliceCommon INTERFACE libglew_static)
target_link_libraries(AliceCommon INTERFACE stb_image)
target_link_libraries(AliceCommon INTERFACE freetype)
target_link_libraries(AliceCommon INTERFACE harfbuzz)
target_link_libraries(AliceCommon INTERFACE glm)
target_link_libraries(AliceCommon INTERFACE lib_luajit)
if (WIN32)
	target_link_libraries(AliceCommon INTERFACE ws2_32)
	target_link_libraries(AliceCommon INTERFACE wsock32)
	target_link_libraries(AliceCommon INTERFACE Imm32)
else()
	target_link_libraries(AliceCommon INTERFACE dependency_tbb)
	target_link_libraries(AliceCommon INTERFACE glfw)
	target_link_libraries(AliceCommon INTERFACE miniaudio)
	target_link_libraries(AliceCommon INTERFACE dependency_icu)
endif()
target_include_directories(AliceCommon INTERFACE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/src/ai
	${PROJECT_SOURCE_DIR}/src/common_types
	${PROJECT_SOURCE_DIR}/src/filesystem
	${PROJECT_SOURCE_DIR}/src/gamestate
	${PROJECT_SOURCE_DIR}/src/gui
	${PROJECT_SOURCE_DIR}/src/gui/province_tiles
	${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows
	${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/diplomacy_subwindows
	${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/production_subwindows
	${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/politics_subwindows
	${PROJECT_SOURCE_DIR}/src/gui/topbar_subwindows/military_subwindows
	${PROJECT_SOURCE_DIR}/src/graphics
	${PROJECT_SOURCE_DIR}/src/parsing
	${PROJECT_SOURCE_DIR}/src/window
	${PROJECT_SOURCE_DIR}/src/text
	${PROJECT_SOURCE_DIR}/src/sound
	${PROJECT_SOURCE_DIR}/src/map
	${PROJECT_SOURCE_DIR}/src/network
	${PROJECT_SOURCE_DIR}/src/nations
	${PROJECT_SOURCE_DIR}/src/gamerule
	${PROJECT_SOURCE_DIR}/src/provinces
	${PROJECT_SOURCE_DIR}/src/economy
	${PROJECT_SOURCE_DIR}/src/culture
	${PROJECT_SOURCE_DIR}/src/military
	${PROJECT_SOURCE_DIR}/src/scripting
	${PROJECT_SOURCE_DIR}/src/scripting/luajit
	${PROJECT_SOURCE_DIR}/src/zstd
	${PROJECT_SOURCE_DIR}/src/lunasvg
	"${glew_SOURCE_DIR}/include/GL"
	"ankerl")

target_link_libraries(Alice PRIVATE AliceCommon)
target_link_libraries(AliceIncremental PRIVATE AliceCommon)

# OUR TIME gets its own headless entry point and compiles only the simulation
# side of the source manifest. The existing Alice executable remains the
# compatibility runtime while this boundary is being migrated.
if(UNIX)
	find_package(Threads REQUIRED)
	add_library(AliceOurTimeCommon INTERFACE)
	get_target_property(_ALICE_COMMON_INCLUDE_DIRS AliceCommon INTERFACE_INCLUDE_DIRECTORIES)
	get_target_property(_ALICE_COMMON_DEFINITIONS AliceCommon INTERFACE_COMPILE_DEFINITIONS)
	if(_ALICE_COMMON_INCLUDE_DIRS)
		target_include_directories(AliceOurTimeCommon SYSTEM INTERFACE ${_ALICE_COMMON_INCLUDE_DIRS})
	endif()
	if(_ALICE_COMMON_DEFINITIONS)
		target_compile_definitions(AliceOurTimeCommon INTERFACE ${_ALICE_COMMON_DEFINITIONS})
	endif()
	# Read header paths from existing dependency targets without linking the UI,
	# sound, Lua, or renderer libraries into this executable.
	foreach(_dependency_target IN ITEMS
		dependency_DataContainer dependency_DataContainerLua dependency_DataContainerOOSReporter
		libglew_static stb_image freetype harfbuzz glm lib_luajit dependency_tbb glfw miniaudio dependency_icu)
		if(TARGET ${_dependency_target})
			get_target_property(_dependency_includes ${_dependency_target} INTERFACE_INCLUDE_DIRECTORIES)
			if(_dependency_includes)
				target_include_directories(AliceOurTimeCommon SYSTEM INTERFACE ${_dependency_includes})
			endif()
			get_target_property(_dependency_definitions ${_dependency_target} INTERFACE_COMPILE_DEFINITIONS)
			if(_dependency_definitions)
				target_compile_definitions(AliceOurTimeCommon INTERFACE ${_dependency_definitions})
			endif()
		endif()
	endforeach()
	target_compile_options(AliceOurTimeCommon INTERFACE
		$<$<CONFIG:Debug>:-g>
		"$<$<NOT:$<CONFIG:Debug>>:-O1;-fno-rtti;-DNDEBUG>"
		-fno-fast-math -ffp-contract=off)
	target_link_libraries(AliceOurTimeCommon INTERFACE dependency_DataContainer glm Threads::Threads)

	add_library(AliceOurTimeCore STATIC EXCLUDE_FROM_ALL
		${ALICE_OUR_TIME_TRANSLATION_UNITS}
		"${ALICE_DCON_COMMON_TYPES_SOURCE}")
	target_link_libraries(AliceOurTimeCore PUBLIC AliceOurTimeCommon)
	target_compile_definitions(AliceOurTimeCore PRIVATE
		GLM_ENABLE_EXPERIMENTAL ALICE_SPLIT_BUILD=1 DCON_TRAP_INVALID_STORE=1)
	if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(AliceOurTimeCore PRIVATE -ffunction-sections -fdata-sections)
	endif()

	add_executable(AliceOurTime EXCLUDE_FROM_ALL src/entry_point_our_time.cpp)
	target_link_libraries(AliceOurTime PRIVATE AliceOurTimeCore)
	if(APPLE)
		target_link_options(AliceOurTime PRIVATE -Wl,-dead_strip)
	else()
		target_link_options(AliceOurTime PRIVATE -Wl,--gc-sections)
	endif()
endif()

if (WIN32)
	target_link_libraries(AliceProfile PRIVATE AliceCommon)
	target_link_libraries(Alice PRIVATE ${PROJECT_SOURCE_DIR}/libs/LLVM-C.lib)
	target_link_libraries(AliceIncremental PRIVATE ${PROJECT_SOURCE_DIR}/libs/LLVM-C.lib)
	target_link_libraries(AliceProfile PRIVATE ${PROJECT_SOURCE_DIR}/libs/LLVM-C.lib)
else()
	target_link_libraries(Alice PRIVATE fmt::fmt)
	target_link_libraries(AliceIncremental PRIVATE fmt::fmt)
endif()

# System headers
target_precompile_headers(Alice
	PRIVATE <stddef.h>
	PRIVATE <stdint.h>
	PRIVATE <assert.h>
	PRIVATE <stdarg.h>
	PRIVATE <stdlib.h>
	PRIVATE <math.h>
	PRIVATE <signal.h>
	PRIVATE <ctype.h>
)
target_precompile_headers(AliceIncremental
	PRIVATE <stddef.h>
	PRIVATE <stdint.h>
	PRIVATE <assert.h>
	PRIVATE <stdarg.h>
	PRIVATE <stdlib.h>
	PRIVATE <math.h>
	PRIVATE <signal.h>
	PRIVATE <ctype.h>
)
# Standard C++ library headers
target_precompile_headers(Alice
	PRIVATE <vector>
	PRIVATE <queue>	
	PRIVATE <array>
	PRIVATE <optional>
	PRIVATE <memory>
	PRIVATE <variant>
	PRIVATE <string>
	PRIVATE <string_view>
	PRIVATE <charconv>
	PRIVATE <algorithm>
	PRIVATE <iterator>
	PRIVATE <functional>
	PRIVATE <atomic>
	PRIVATE <chrono>
	PRIVATE <tuple>
	PRIVATE <type_traits>
	PRIVATE <new>
	PRIVATE <limits>
	PRIVATE <iterator>
	PRIVATE <utility>
	PRIVATE <any>
	PRIVATE <shared_mutex>
)
target_precompile_headers(AliceIncremental
	PRIVATE <vector>
	PRIVATE <queue>		
	PRIVATE <array>
	PRIVATE <optional>
	PRIVATE <memory>
	PRIVATE <variant>
	PRIVATE <string>
	PRIVATE <string_view>
	PRIVATE <charconv>
	PRIVATE <algorithm>
	PRIVATE <iterator>
	PRIVATE <functional>
	PRIVATE <atomic>
	PRIVATE <chrono>
	PRIVATE <tuple>
	PRIVATE <type_traits>
	PRIVATE <new>
	PRIVATE <limits>
	PRIVATE <iterator>
	PRIVATE <utility>
	PRIVATE <shared_mutex>
)

# Aliases for C library
target_precompile_headers(Alice
	PRIVATE <cstdlib>
	PRIVATE <cstddef>
	PRIVATE <cstdint>
	PRIVATE <cassert>
	PRIVATE <cstdarg>
	PRIVATE <cmath>
)

if(TARGET AliceOurTimeCore)
	# Share the generated model schema once inside this target. Keep its PCH
	# independent of Alice so building OUR TIME never schedules the legacy app.
	target_precompile_headers(AliceOurTimeCore PRIVATE
		<vector>
		<string>
		<algorithm>
		<memory>
		<functional>
		[["dcon_generated.hpp"]]
	)
endif()
target_precompile_headers(AliceIncremental
	PRIVATE <cstdlib>
	PRIVATE <cstddef>
	PRIVATE <cstdint>
	PRIVATE <cassert>
	PRIVATE <cstdarg>
	PRIVATE <cmath>
)
# Alice provided headers
target_precompile_headers(Alice
	PRIVATE [["simple_fs.hpp"]]
	PRIVATE [["constants.hpp"]]
	PRIVATE [["unordered_dense.h"]]
	PRIVATE [["dcon_generated.hpp"]]
	PRIVATE [["container_types.hpp"]]
	PRIVATE [["parsers_declarations.hpp"]]
	PRIVATE [["cyto_any.hpp"]]
	PRIVATE [["date_interface.hpp"]]
	PRIVATE [["stb_image.h"]]
)
target_precompile_headers(AliceIncremental
	PRIVATE [["simple_fs.hpp"]]
	PRIVATE [["constants_dcon.hpp"]]
	PRIVATE [["unordered_dense.h"]]
	PRIVATE [["dcon_generated.hpp"]]
	PRIVATE [["dcon_generated_ids.hpp"]]
	PRIVATE [["container_types_dcon.hpp"]]
	PRIVATE [["container_types.hpp"]]
#	PRIVATE [["parsers_declarations.hpp"]]
	PRIVATE [["cyto_any.hpp"]]
#	PRIVATE [["gui_lookup.hpp"]]
#	PRIVATE [["gui_templates.hpp"]]
	PRIVATE [["date_interface.hpp"]]
	PRIVATE [["stb_image.h"]]
	PRIVATE [["glm/glm.hpp"]]
#	PRIVATE [["script_constants.hpp"]]
)

if(WIN32)
target_precompile_headers(AliceProfile REUSE_FROM AliceIncremental)
else()
	target_precompile_headers(Alice
		PRIVATE [["miniaudio.h"]])
	target_precompile_headers(AliceIncremental
		PRIVATE [["miniaudio.h"]])
endif()


# GENERATE CONTAINER
set(CONTAINER_PATH ${PROJECT_SOURCE_DIR}/src/gamestate/dcon_generated)

# The command to build the generated file
add_custom_command(
	OUTPUT ${CONTAINER_PATH}.hpp ${CONTAINER_PATH}_ids.hpp
	COMMAND DCONGENERATOR ${CONTAINER_PATH}.txt
	DEPENDS ${CONTAINER_PATH}.txt
	VERBATIM)

add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/src/gamestate/fif_dcon_generated.hpp
	COMMAND DCONINTERFACEGEN ${CONTAINER_PATH}.txt
	DEPENDS ${CONTAINER_PATH}.txt
	VERBATIM)

add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/src/gamestate/lua_dcon_generated.hpp
	COMMAND DataContainer-Lua alice alice_state_ptr->world. ${CONTAINER_PATH}.txt ${PROJECT_SOURCE_DIR}/src/gamestate/lua_dcon_generated.cpp ${PROJECT_SOURCE_DIR}/src/gamestate/lua_dcon_generated.hpp ${PROJECT_SOURCE_DIR}/assets/lua/engine
	DEPENDS ${CONTAINER_PATH}.txt
	DEPENDS DataContainer-Lua
	VERBATIM
)

add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/src/gamestate/dcon_oos_reporter_generated.hpp
	COMMAND DataContainer_OOS_Reporter alice ${CONTAINER_PATH}.txt dcon_oos_reporter_generated.cpp dcon_oos_reporter_generated.hpp ${PROJECT_SOURCE_DIR}/src/gamestate/
	DEPENDS ${CONTAINER_PATH}.txt
	DEPENDS DataContainer_OOS_Reporter
	VERBATIM
)

# Sets a dependency on the generated file
add_custom_target(GENERATE_CONTAINER DEPENDS ${CONTAINER_PATH}.hpp)
add_dependencies(Alice GENERATE_CONTAINER ParserGenerator)
add_dependencies(AliceIncremental GENERATE_CONTAINER ParserGenerator)
if(TARGET AliceOurTimeCore)
	add_dependencies(AliceOurTimeCore GENERATE_CONTAINER ParserGenerator)
endif()

add_custom_target(GENERATE_CONTAINERIFACE DEPENDS ${PROJECT_SOURCE_DIR}/src/gamestate/fif_dcon_generated.hpp)
add_dependencies(Alice GENERATE_CONTAINERIFACE)
add_dependencies(AliceIncremental GENERATE_CONTAINERIFACE)
if(TARGET AliceOurTimeCore)
	add_dependencies(AliceOurTimeCore GENERATE_CONTAINERIFACE)
endif()

add_custom_target(GENERATE_CONTAINER_LUA DEPENDS ${PROJECT_SOURCE_DIR}/src/gamestate/lua_dcon_generated.hpp)
add_dependencies(Alice GENERATE_CONTAINER_LUA)
add_dependencies(AliceIncremental GENERATE_CONTAINER_LUA)


add_custom_target(GENERATE_CONTAINER_OOS DEPENDS ${PROJECT_SOURCE_DIR}/src/gamestate/dcon_oos_reporter_generated.hpp)
add_dependencies(Alice GENERATE_CONTAINER_OOS)
add_dependencies(AliceIncremental GENERATE_CONTAINER_OOS)

if(WIN32)
add_dependencies(AliceProfile GENERATE_CONTAINER ParserGenerator)
add_dependencies(AliceProfile GENERATE_CONTAINERIFACE)
add_dependencies(AliceProfile GENERATE_CONTAINER_LUA)
add_dependencies(AliceProfile GENERATE_CONTAINER_OOS)
endif()

# The command to build the generated parsers file
add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/src/parsing/parser_defs_generated.hpp
	COMMAND ParserGenerator ${PROJECT_SOURCE_DIR}/src/parsing/parser_defs.txt
	DEPENDS ${PROJECT_SOURCE_DIR}/src/parsing/parser_defs.txt
	VERBATIM)

add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/src/parsing/tutorial_parser_defs_generated.hpp
	COMMAND ParserGenerator ${PROJECT_SOURCE_DIR}/src/parsing/tutorial_parser_defs.txt
	DEPENDS ${PROJECT_SOURCE_DIR}/src/parsing/tutorial_parser_defs.txt
	VERBATIM)

add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/src/parsing/gui_parser_defs_generated.hpp
	COMMAND ParserGenerator ${PROJECT_SOURCE_DIR}/src/parsing/gui_parser_defs.txt
	DEPENDS ${PROJECT_SOURCE_DIR}/src/parsing/gui_parser_defs.txt
	VERBATIM)

add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/src/parsing/save_parser_defs_generated.hpp
	COMMAND ParserGenerator ${PROJECT_SOURCE_DIR}/src/parsing/save_parser_defs.txt
	DEPENDS ${PROJECT_SOURCE_DIR}/src/parsing/save_parser_defs.txt
	VERBATIM)

add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/src/parsing/news_parser_defs_generated.hpp
	COMMAND ParserGenerator ${PROJECT_SOURCE_DIR}/src/parsing/news_parser_defs.txt
	DEPENDS ${PROJECT_SOURCE_DIR}/src/parsing/news_parser_defs.txt
	VERBATIM)

add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/src/parsing/effect_parser_defs_generated.hpp
	COMMAND ParserGenerator ${PROJECT_SOURCE_DIR}/src/parsing/effect_parser_defs.txt
	DEPENDS ${PROJECT_SOURCE_DIR}/src/parsing/effect_parser_defs.txt
	VERBATIM)

add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/src/parsing/trigger_parser_defs_generated.hpp
	COMMAND ParserGenerator ${PROJECT_SOURCE_DIR}/src/parsing/trigger_parser_defs.txt
	DEPENDS ${PROJECT_SOURCE_DIR}/src/parsing/trigger_parser_defs.txt
	VERBATIM)

add_custom_command(
  OUTPUT ${PROJECT_SOURCE_DIR}/src/text/font_defs_generated.hpp
  COMMAND ParserGenerator ${PROJECT_SOURCE_DIR}/src/text/font_defs.txt
  DEPENDS ${PROJECT_SOURCE_DIR}/src/text/font_defs.txt
  VERBATIM)

add_custom_command(
  OUTPUT ${PROJECT_SOURCE_DIR}/src/text/bmfont_defs_generated.hpp
  COMMAND ParserGenerator ${PROJECT_SOURCE_DIR}/src/text/bmfont_defs.txt
  DEPENDS ${PROJECT_SOURCE_DIR}/src/text/bmfont_defs.txt
  VERBATIM)

# Sets a dependency on the generated file
add_custom_target(GENERATE_PARSERS DEPENDS
	${PROJECT_SOURCE_DIR}/src/parsing/parser_defs_generated.hpp
	${PROJECT_SOURCE_DIR}/src/parsing/save_parser_defs_generated.hpp
	${PROJECT_SOURCE_DIR}/src/parsing/tutorial_parser_defs_generated.hpp
	${PROJECT_SOURCE_DIR}/src/parsing/gui_parser_defs_generated.hpp
	${PROJECT_SOURCE_DIR}/src/parsing/news_parser_defs_generated.hpp
	${PROJECT_SOURCE_DIR}/src/parsing/effect_parser_defs_generated.hpp
	${PROJECT_SOURCE_DIR}/src/parsing/trigger_parser_defs_generated.hpp
	${PROJECT_SOURCE_DIR}/src/text/bmfont_defs_generated.hpp
	${PROJECT_SOURCE_DIR}/src/text/font_defs_generated.hpp)
add_dependencies(Alice GENERATE_PARSERS)
add_dependencies(AliceIncremental GENERATE_PARSERS)
if(TARGET AliceOurTimeCore)
	add_dependencies(AliceOurTimeCore GENERATE_PARSERS)
endif()

if(WIN32)
add_dependencies(AliceProfile GENERATE_PARSERS)
endif()

if (BUILD_TESTING)
    enable_testing()
	add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()

if(WIN32)
	add_subdirectory(SaveEditor EXCLUDE_FROM_ALL)
	add_subdirectory(DbgAlice EXCLUDE_FROM_ALL)
endif()
add_subdirectory(Launcher)

add_dependencies(launch_alice GENERATE_CONTAINER ParserGenerator)
add_dependencies(launch_alice GENERATE_CONTAINERIFACE)
add_dependencies(launch_alice GENERATE_CONTAINER_LUA)
add_dependencies(launch_alice GENERATE_CONTAINER_OOS)
add_dependencies(launch_alice GENERATE_PARSERS)

# Installation
if(NOT WIN32)
	install(TARGETS Alice launch_alice
		RUNTIME DESTINATION bin
		LIBRARY DESTINATION lib
		ARCHIVE DESTINATION lib)
    install(FILES ${PROJECT_SOURCE_DIR}/src/alice.desktop DESTINATION /usr/share/applications)
    install(FILES ${PROJECT_SOURCE_DIR}/src/alice.png DESTINATION /usr/share/icons/hicolor/128x128/apps)
endif()

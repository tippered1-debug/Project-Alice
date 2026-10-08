#94723 <-- DO NOT REMOVE ... the file system tests are looking for this

function(alice_configure_test_compile target)
	# Test binaries do not ship; their compile cost matters more than optimized
	# test runtime.
	if(MSVC)
		target_compile_options(${target} PRIVATE /Od)
	else()
		target_compile_options(${target} PRIVATE -O0)
	endif()
endfunction()

set(ALICE_TEST_GROUP_SOURCES
	"${PROJECT_SOURCE_DIR}/tests/test_group_foundation.cpp"
	"${PROJECT_SOURCE_DIR}/tests/test_group_data.cpp"
	"${PROJECT_SOURCE_DIR}/tests/test_group_person_economy.cpp"
	"${PROJECT_SOURCE_DIR}/tests/test_group_person_modules.cpp"
	"${PROJECT_SOURCE_DIR}/tests/test_group_simulation.cpp"
	"${PROJECT_SOURCE_DIR}/tests/test_group_pathfinding.cpp"
	"${PROJECT_SOURCE_DIR}/tests/test_group_government.cpp")

# Compile the game implementation once as the same bounded translation units
# used by Alice. Keep the platform entry point out of Catch2's executables.
set(ALICE_TEST_ENGINE_SOURCES ${ALICE_MAIN_TRANSLATION_UNITS})
list(REMOVE_ITEM ALICE_TEST_ENGINE_SOURCES
	"${PROJECT_SOURCE_DIR}/src/platform_specific.cpp")
add_library(alice_test_engine OBJECT
	${ALICE_TEST_ENGINE_SOURCES}
	"${PROJECT_SOURCE_DIR}/tests/test_platform_specific.cpp")
target_link_libraries(alice_test_engine PRIVATE AliceCommon)
target_compile_definitions(alice_test_engine PRIVATE
	GLM_ENABLE_EXPERIMENTAL
	ALICE_SPLIT_BUILD=1
	DCON_TRAP_INVALID_STORE=1)
target_precompile_headers(alice_test_engine REUSE_FROM Alice)
add_dependencies(alice_test_engine
	GENERATE_CONTAINER
	GENERATE_CONTAINERIFACE
	GENERATE_CONTAINER_LUA
	GENERATE_CONTAINER_OOS
	GENERATE_PARSERS)

add_executable(tests_project
	"${PROJECT_SOURCE_DIR}/tests/test_main.cpp"
	"${PROJECT_SOURCE_DIR}/tests/test_support.cpp"
	${ALICE_TEST_GROUP_SOURCES}
	"${PROJECT_SOURCE_DIR}/src/gui/alice_ui.cpp"
	"${PROJECT_SOURCE_DIR}/src/map/map_state.cpp"
	"${PROJECT_SOURCE_DIR}/src/map/map_data_loading.cpp"
	"${PROJECT_SOURCE_DIR}/src/map/map_borders.cpp"
	"${PROJECT_SOURCE_DIR}/src/lunasvg/lunasvg.cpp"
	"${PROJECT_SOURCE_DIR}/src/map/projections.cpp"
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
	"${PROJECT_SOURCE_DIR}/src/alice.rc")

set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-blend.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-canvas.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-font.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-ft-math.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-ft-raster.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-ft-stroker.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-matrix.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-paint.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-path.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-rasterize.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/lunasvg/plutovg-surface.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)


target_link_libraries(tests_project PRIVATE AliceCommon alice_test_engine)

FetchContent_MakeAvailable(Catch2)

# Link to the desired libraries
target_link_libraries(tests_project
    PRIVATE
    Catch2::Catch2
)

if (WIN32)
	target_link_libraries(tests_project PRIVATE ${PROJECT_SOURCE_DIR}/libs/LLVM-C.lib)
else()
	target_link_libraries(tests_project PRIVATE fmt::fmt)
endif()

# GENERATE test parsers

# The command to build the generated testparsers file
add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/tests/test_parsers_generated.hpp
	COMMAND ParserGenerator ${PROJECT_SOURCE_DIR}/tests/test_parsers.txt
	DEPENDS ${PROJECT_SOURCE_DIR}/tests/test_parsers.txt
	VERBATIM)

# Sets a dependency on the generated file
add_custom_target(GENERATE_TEST_PARSERS DEPENDS ${PROJECT_SOURCE_DIR}/tests/test_parsers_generated.hpp)
add_dependencies(tests_project GENERATE_TEST_PARSERS)
add_dependencies(tests_project GENERATE_PARSERS)
add_dependencies(tests_project GENERATE_CONTAINER ParserGenerator)

target_precompile_headers(tests_project REUSE_FROM Alice)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
# Full scenario and calibration-sensitive tests are intentionally opt-in: the
# former need a generated tests_scenario.bin, while the latter are expected to
# move as the transformed economy is balanced. The default suite remains a
# fast structural check runnable from a clean checkout using the synthetic lab.
catch_discover_tests(tests_project
	TEST_SPEC "~[scenario-fixture]~[balance-fixture]~[asset-fixture]"
	WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/assets"
)

# add_custom_command(
#    TARGET tests_project
#     COMMENT "Run tests"
#     POST_BUILD
#     WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#     COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIGURATION> -R "^tests_project$" --output-on-failures
# )

# Independent canonical-consumer runner: legacy tests referring to removed APIs
# must not prevent the consumer hard-cut regressions from building and running.
get_target_property(CONSUMER_PURGE_SOURCES tests_project SOURCES)
list(REMOVE_ITEM CONSUMER_PURGE_SOURCES
	"${PROJECT_SOURCE_DIR}/tests/test_main.cpp"
	"${PROJECT_SOURCE_DIR}/tests/test_support.cpp"
	${ALICE_TEST_GROUP_SOURCES})
add_executable(consumer_purge_tests "${PROJECT_SOURCE_DIR}/tests/consumer_purge_main.cpp"
 "${PROJECT_SOURCE_DIR}/tests/legacy_consumer_purge_tests.cpp"
 "${PROJECT_SOURCE_DIR}/tests/canonical_construction_tests.cpp"
 "${PROJECT_SOURCE_DIR}/src/economy/collective_labor.cpp"
 "${PROJECT_SOURCE_DIR}/src/nations/strategic_statecraft.cpp" ${CONSUMER_PURGE_SOURCES})
alice_configure_test_compile(consumer_purge_tests)
target_link_libraries(consumer_purge_tests PRIVATE AliceCommon Catch2::Catch2)
target_link_libraries(consumer_purge_tests PRIVATE alice_test_engine)
if(WIN32)
 target_link_libraries(consumer_purge_tests PRIVATE ${PROJECT_SOURCE_DIR}/libs/LLVM-C.lib)
else()
 target_link_libraries(consumer_purge_tests PRIVATE fmt::fmt)
endif()
add_dependencies(consumer_purge_tests GENERATE_TEST_PARSERS GENERATE_PARSERS GENERATE_CONTAINER)
catch_discover_tests(consumer_purge_tests WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/assets")

# Focused labor runner avoids unrelated legacy test files in the consumer
# runner while exercising the same production code and primary fixture.
add_executable(collective_labor_focus_tests
	"${PROJECT_SOURCE_DIR}/tests/collective_labor_main.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/collective_labor.cpp"
	"${PROJECT_SOURCE_DIR}/src/economy/social_entitlements.cpp"
	"${PROJECT_SOURCE_DIR}/src/nations/strategic_statecraft.cpp"
	${CONSUMER_PURGE_SOURCES})
alice_configure_test_compile(collective_labor_focus_tests)
target_link_libraries(collective_labor_focus_tests PRIVATE AliceCommon Catch2::Catch2)
target_link_libraries(collective_labor_focus_tests PRIVATE alice_test_engine)
if(WIN32)
	target_link_libraries(collective_labor_focus_tests PRIVATE ${PROJECT_SOURCE_DIR}/libs/LLVM-C.lib)
else()
	target_link_libraries(collective_labor_focus_tests PRIVATE fmt::fmt)
endif()
add_dependencies(collective_labor_focus_tests GENERATE_TEST_PARSERS GENERATE_PARSERS GENERATE_CONTAINER)
catch_discover_tests(collective_labor_focus_tests WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/assets")

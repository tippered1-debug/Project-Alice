#pragma once

#ifndef CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_ENABLE_BENCHMARKING 1
#endif
#ifndef CATCH_CONFIG_DISABLE_EXCEPTIONS
#define CATCH_CONFIG_DISABLE_EXCEPTIONS 1
#endif
#ifndef DCON_TRAP_INVALID_STORE
#define DCON_TRAP_INVALID_STORE 1
#endif

#include "catch2/catch.hpp"
#include "dcon_generated.hpp"
#include "simple_fs.hpp"
#include "system_state.hpp"

#include <memory>

#ifndef RANGE
#define RANGE(x) (x), (x) + ((sizeof(x)) / sizeof((x)[0])) - 1
#endif
#ifndef RANGE_SZ
#define RANGE_SZ(x) (x), ((sizeof(x)) / sizeof((x)[0])) - 1
#endif

#ifndef NATIVE_SEP
#ifdef _WIN64
#define NATIVE_SEP "\\"
#else
#define NATIVE_SEP "/"
#endif
#endif

native_string testing_scenario_file();
std::unique_ptr<sys::state> load_testing_scenario_file(
	sys::network_mode_type mode = sys::network_mode_type::single_player);
std::unique_ptr<sys::state> load_testing_scenario_file_with_save(
	sys::network_mode_type mode = sys::network_mode_type::single_player,
	dcon::nation_id selected_nation = dcon::nation_id{});

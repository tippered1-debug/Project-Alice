#include "test_support.hpp"

#include "network.hpp"
#include "parsers.hpp"
#include "serialization.hpp"

#include <cstdlib>

native_string testing_scenario_file() {
	if(auto const* requested = std::getenv("ALICE_TEST_SCENARIO");
		requested && requested[0] != '\0')
		return simple_fs::utf8_to_native(requested);
	return NATIVE("tests_scenario.bin");
}

std::unique_ptr<sys::state> load_testing_scenario_file(sys::network_mode_type mode) {
	std::unique_ptr<sys::state> game_state = std::make_unique<sys::state>(); // too big for the stack

	game_state->network_mode = mode;
	game_state->user_settings.autosaves = sys::autosave_frequency::yearly;

	add_root(game_state->common_fs, NATIVE(".")); // for the moment this lets us find the shader files
	if(!sys::try_read_scenario_file(*game_state, testing_scenario_file())) {
		std::abort();
	} else {
		INFO("Scenario loaded");
	}

	return game_state;
}

std::unique_ptr<sys::state> load_testing_scenario_file_with_save(
	sys::network_mode_type mode, dcon::nation_id selected_nation) {
	std::unique_ptr<sys::state> game_state = std::make_unique<sys::state>(); // too big for the stack

	game_state->network_mode = mode;
	game_state->user_settings.autosaves = sys::autosave_frequency::yearly;

	add_root(game_state->common_fs, NATIVE(".")); // for the moment this lets us find the shader files
	if(!sys::try_read_scenario_and_save_file(*game_state, testing_scenario_file())) {
		// scenario making functions
		parsers::error_handler err("");
		game_state->load_scenario_data(err, sys::year_month_day{ 1836, 1, 1 });
		sys::write_scenario_file(*game_state, NATIVE("tests_scenario.bin"), 1);
		INFO("Wrote new scenario");
		std::abort();
	} else {
		INFO("Scenario loaded");
	}
	if(!selected_nation) {
		auto observer_nation = game_state->world.national_identity_get_nation_from_identity_holder(game_state->national_definitions.rebel_id);
		network::create_mp_player(*game_state, sys::player_name{ 'P', 'l', 'a', 'y', 'e', 'r' }, sys::player_password_raw{}, true, false, observer_nation);
		game_state->local_player_nation = observer_nation;
	} else {
		network::create_mp_player(*game_state, sys::player_name{ 'P', 'l', 'a', 'y', 'e', 'r' }, sys::player_password_raw{}, true, false, selected_nation);
		game_state->local_player_nation = selected_nation;
	}
	game_state->fill_unsaved_data();

	return game_state;
}

#pragma once

namespace simple_fs { class directory; }
namespace parsers {
	class error_handler;
	struct scenario_building_context;
}
namespace sys { class state; }

namespace actors::canonical_scenario {

// Imports the required canonical_runtime tables from the scenario's common
// directory. No firms, operators, owners, accounts, or debts are inferred.
bool load(sys::state&, simple_fs::directory const&, parsers::scenario_building_context&,
	parsers::error_handler&);

} // namespace actors::canonical_scenario

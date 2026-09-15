#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

#include <cstdint>
#include <optional>

namespace sys { class state; }

namespace governance::law {

enum class legal_instrument_kind : uint8_t { statute = 0, regulation = 1 };
enum class legal_status : uint8_t { draft = 0, enacted = 1, repealed = 2 };
enum class policy_rule_kind : uint8_t { public_debt_ceiling = 0, public_debt_issuance_prohibited = 1 };
enum class legal_action_kind : uint8_t { enactment = 0, repeal = 1 };

struct public_debt_policy {
	bool issuance_allowed = true;
	std::optional<float> ceiling;
};

dcon::legal_instrument_id create_draft_instrument(sys::state&, legal_instrument_kind,
	dcon::nation_id nation = {}, dcon::territorial_unit_id territorial_unit = {});
bool add_public_debt_ceiling_rule(sys::state&, dcon::legal_instrument_id, dcon::commodity_id, float);
bool add_public_debt_prohibition_rule(sys::state&, dcon::legal_instrument_id, dcon::commodity_id);
dcon::legal_action_id authorized_enact(sys::state&, dcon::person_id, dcon::legal_instrument_id,
	sys::date enacted_on, sys::date effective_from);
dcon::legal_action_id authorized_repeal(sys::state&, dcon::person_id, dcon::legal_instrument_id, sys::date);
bool instrument_is_effective(sys::state const&, dcon::legal_instrument_id, sys::date);
public_debt_policy public_debt_policy_for(sys::state const&, dcon::nation_id, dcon::commodity_id, sys::date);

} // namespace governance::law

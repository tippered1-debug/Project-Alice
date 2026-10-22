#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "dcon_generated_ids.hpp"

namespace sys {
class state;
}

// What a country would answer to a proposal, worked out by the same rules that
// decide it, but without recording anything: the interface may ask as often as
// it draws. The deciding functions in strategic_statecraft.hpp call these and
// then write their decision.
namespace nations::strategic_statecraft {

enum class forecast_factor : uint8_t {
	none = 0,
	common_threat = 1, // a rival they share
	strategic_value = 2, // power, reach and markets
	reliability = 3, // how well the proposer has kept its word
	proximity = 4,
	entanglement = 5, // wars and alliances the proposer would bring
	claim_conflict = 6, // cores one holds of the other
	infamy = 7,
	shared_enemy = 8,
	alliance = 9,
	market_gain = 10,
	security_cost = 11,
	relations = 12, // how the two governments stand with each other
	third_parties = 13 // the proposer's ties to the observer's rival, allies and their enemies
};

struct forecast {
	struct part {
		forecast_factor factor = forecast_factor::none;
		float weight = 0.0f; // its share of the value, signed
	};
	bool modelled = false; // false: the country is not decided by this model
	bool accepted = false;
	float value = 0.0f;
	float threshold = 0.0f;
	std::array<part, 10> parts{};
};

// A reason that refuses a forecast whatever else is in it, or none.
forecast_factor veto_of(forecast const& f);

// A deal: several things asked and several given at once, answered as one.
// What is asked is weighed by the rules that answer each request on its own,
// as its margin over what that answer needs; what is given sweetens the whole.
enum deal_item : uint16_t {
	deal_alliance = 1 << 0, // asked: a mutual alliance
	deal_access_for_us = 1 << 1, // asked: our forces may cross their land
	deal_free_trade = 1 << 2, // asked: free trade both ways
	deal_lift_their_embargo = 1 << 3, // asked: they lift their embargo on us
	deal_access_for_them = 1 << 4, // given: their forces may cross ours
	deal_subsidies = 1 << 5, // given: we pay them war subsidies
	deal_lift_our_embargo = 1 << 6, // given: we lift our embargo on them
};
inline constexpr uint16_t deal_asks = deal_alliance | deal_access_for_us | deal_free_trade | deal_lift_their_embargo;
inline constexpr uint16_t deal_gives = deal_access_for_them | deal_subsidies | deal_lift_our_embargo;
inline constexpr int32_t deal_item_count = 7;

struct deal_forecast {
	bool modelled = false;
	bool accepted = false;
	float value = 0.0f; // above zero the deal is taken
	forecast_factor veto = forecast_factor::none; // a reason that refuses the deal whatever is offered
	std::array<float, deal_item_count> item_value{}; // what each item adds or takes away, by bit
};

// What a refused deal costs the proposer in relations.
inline constexpr float deal_refusal_relation = -5.0f;

deal_forecast forecast_deal(sys::state& state, dcon::nation_id target, dcon::nation_id proposer, uint16_t gives, uint16_t asks);

forecast forecast_alliance(sys::state& state, dcon::nation_id target, dcon::nation_id proposer);
forecast forecast_military_access(sys::state& state, dcon::nation_id target, dcon::nation_id requester);
forecast forecast_free_trade(sys::state& state, dcon::nation_id target, dcon::nation_id proposer);

// ---- what one government can tell of another's intentions ----
//
// The forecasts above are the other side's own calculation. A government that
// asks itself how a proposal would be received does not have it: it knows what
// is public (relations, alliances, wars, claims, distance, its own record) and
// has to infer the rest (whom the other side counts its rival, what it makes
// of the proposer's power, what it values most). It infers it from its own
// beliefs, and the better it knows the other side the closer the inference
// comes to the real calculation. Nothing here is drawn at random.

// How well an observer reads a subject, from the sources it has. Each source
// is a share from 0 to 1; the quality combines them as independent channels.
struct reading {
	float quality = 0.0f;
	float contact = 0.0f; // standing ties: an alliance, a border, a war, rivalry, a great power's attention
	float intelligence = 0.0f; // the intelligence agency's latest report, by its confidence and age
	float embassy = 0.0f; // the latest report of our own envoys
	float openness = 0.0f; // how freely a friendly government talks to us
	int32_t intelligence_age = -1; // days since the agency's report was received; -1 when there is none
	int32_t embassy_age = -1;
};
reading read_intentions(sys::state& state, dcon::nation_id observer, dcon::nation_id subject);

enum class likelihood : uint8_t {
	unknown = 0, // the country is not decided by this model
	out_of_question = 1, // a public reason refuses it whatever is offered
	very_unlikely = 2,
	unlikely = 3,
	uncertain = 4,
	likely = 5,
	very_likely = 6
};

struct assessment {
	struct part {
		forecast_factor factor = forecast_factor::none;
		float weight = 0.0f; // as the observer reads it, signed
		bool inferred = false; // not public: the observer's reconstruction
	};
	bool modelled = false;
	likelihood verdict = likelihood::unknown;
	float margin = 0.0f; // the value as read, less what the answer needs
	float spread = 0.0f; // how far the reading may be off, either way
	forecast_factor veto = forecast_factor::none;
	reading knowledge;
	std::array<part, 10> parts{};
	std::array<float, deal_item_count> item_value{}; // in a deal: what each item adds or takes away as read, by bit
};

likelihood likelihood_of(float margin, float spread, forecast_factor veto);
assessment assess_alliance(sys::state& state, dcon::nation_id observer, dcon::nation_id target);
assessment assess_military_access(sys::state& state, dcon::nation_id observer, dcon::nation_id target);
assessment assess_free_trade(sys::state& state, dcon::nation_id observer, dcon::nation_id target);
// A deal as the observer reads it: what is asked by the assessments above, what is given by what it can tell of the other side's needs.
assessment assess_deal(sys::state& state, dcon::nation_id observer, dcon::nation_id target, uint16_t gives, uint16_t asks);

// How far the target trusts the observer to keep its word: its belief of the
// observer's reliability. It is made of the observer's own record with the
// target, so the observer knows it.
struct trust_record {
	float reliability = 0.5f;
	uint16_t kept = 0;
	uint16_t broken = 0;
	int32_t last_commitment_day = -1;
	uint16_t recent_wars = 0;
	int32_t last_war_day = -1;
};
trust_record trust_of(sys::state const& state, dcon::nation_id target, dcon::nation_id observer);

// Who else an alliance of proposer and partner concerns, and how their
// calculation of an alliance with the proposer would move, as the proposer
// reads it (the same reconstruction as assess_alliance). A move caused by a
// hidden rivalry is told only of countries the proposer reads well.
struct reaction {
	dcon::nation_id nation;
	forecast_factor cause = forecast_factor::none;
	float before = 0.0f; // their alliance value of the proposer as read, less what an alliance needs
	float after = 0.0f;
	bool public_reason = false;
};
std::vector<reaction> alliance_reactions(sys::state& state, dcon::nation_id proposer, dcon::nation_id partner, size_t most);

// A diplomatic mission sent by one government to another reports back on it:
// the envoys' report is a source of the reading above.
inline constexpr float mission_report_confidence = 0.6f;
inline constexpr int32_t mission_report_delay_days = 5;
void record_diplomatic_mission(sys::state& state, dcon::nation_id from, dcon::nation_id to);

} // namespace nations::strategic_statecraft

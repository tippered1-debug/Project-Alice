#include "economy/labor_relations.hpp"

#include "catch2/catch.hpp"

namespace labor_relations = economy::labor_relations;

TEST_CASE("union law creates durable membership without misclassifying every movement",
		"[economy][labor][membership]") {
	labor_relations::membership_inputs input;
	input.workers = 1'000.f;
	input.labor_movement_workers = 500.f;
	input.literacy = 0.8f;
	input.consciousness = 0.6f;

	input.regime = labor_relations::organization_regime::prohibited;
	auto const underground = labor_relations::estimate_membership_share(input);
	REQUIRE(underground <= Approx(0.15f));

	input.labor_movement_workers = 0.f;
	input.regime = labor_relations::organization_regime::recognized;
	auto const recognized = labor_relations::estimate_membership_share(input);
	input.regime = labor_relations::organization_regime::protected_right;
	auto const protected_right = labor_relations::estimate_membership_share(input);
	REQUIRE(recognized > underground);
	REQUIRE(protected_right > recognized);
	REQUIRE(protected_right <= Approx(0.85f));
}

TEST_CASE("labor organization comes from members literacy and legal leverage",
	"[economy][labor][organization]") {
	labor_relations::inputs input;
	input.enabled = true;
	input.workers = 1'000.f;
	input.organized_workers = 500.f;
	input.literacy = 0.8f;
	input.consciousness = 0.7f;
	input.employment = 1.f;
	input.life_needs_coverage = 1.f;
	input.legal_leverage = 1.f;
	auto const organized = labor_relations::calculate(input);
	REQUIRE(organized.membership_share == Approx(0.5f));
	REQUIRE(organized.organization > 0.f);
	REQUIRE(organized.strike_participation == Approx(0.f));
	REQUIRE(organized.labor_availability == Approx(1.f));

	input.legal_leverage = 0.f;
	auto const prohibited = labor_relations::calculate(input);
	REQUIRE(prohibited.organization < organized.organization);
}

TEST_CASE("strikes require organization hardship and militancy and remove labor",
	"[economy][labor][strike]") {
	labor_relations::inputs input;
	input.enabled = true;
	input.workers = 1'000.f;
	input.organized_workers = 1'000.f;
	input.literacy = 1.f;
	input.consciousness = 1.f;
	input.militancy = 1.f;
	input.employment = 0.f;
	input.life_needs_coverage = 0.f;
	input.legal_leverage = 1.f;
	auto const result = labor_relations::calculate(input);
	REQUIRE(result.hardship == Approx(1.f));
	REQUIRE(result.strike_participation == Approx(0.25f));
	REQUIRE(result.labor_availability == Approx(0.75f));
}

TEST_CASE("labor relations are a classic-rules no-op", "[economy][labor]") {
	auto const result = labor_relations::calculate({});
	REQUIRE_FALSE(result.enabled);
	REQUIRE(result.labor_availability == Approx(1.f));
}

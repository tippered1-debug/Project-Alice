#include "catch.hpp"

#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/money.hpp"
#include "governance/finance/finance.hpp"
#include "governance/finance/fiscal_ledger.hpp"
#include "governance/governance.hpp"
#include "system_state.hpp"

#include <memory>

namespace fiscal_ledger_tests {

namespace finance = ::governance::finance;
using ::economy::relations::transaction_kind;

struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::nation_id nation;
	dcon::institution_id ministry;
	dcon::institution_id works;
	dcon::institution_id households;
	dcon::monetary_account_id ministry_treasury;
	dcon::monetary_account_id works_treasury;
	dcon::economic_actor_id firm;
	dcon::monetary_account_id firm_account;
	dcon::economic_actor_id person;
	dcon::monetary_account_id person_account;

	fixture() {
		auto& s = *state;
		auto money = s.world.create_commodity();
		REQUIRE(money == ::economy::money);
		nation = s.world.create_nation();
		ministry = ::governance::create_institution(s, nation, ::governance::institution_kind::finance_ministry);
		works = ::governance::create_institution(s, nation, ::governance::institution_kind::public_works_ministry);
		households = ::governance::create_institution(s, nation, ::governance::institution_kind::household_sector);
		ministry_treasury = finance::open_treasury_account(s, ministry, money);
		works_treasury = finance::open_treasury_account(s, works, money);
		REQUIRE(ministry_treasury);
		REQUIRE(works_treasury);
		firm = s.world.create_economic_actor();
		s.world.economic_actor_set_kind(firm, uint8_t(::actors::ownership::actor_kind::company));
		firm_account = ::economy::accounts::open_account(s, firm, money);
		person = s.world.create_economic_actor();
		s.world.economic_actor_set_kind(person, uint8_t(::actors::ownership::actor_kind::person));
		person_account = ::economy::accounts::open_account(s, person, money);
		REQUIRE(::economy::accounts::bootstrap_set_balance(s, ministry_treasury, 100.0f));
		REQUIRE(::economy::accounts::bootstrap_set_balance(s, firm_account, 100.0f));
	}
	dcon::economic_actor_id actor(dcon::institution_id i) const {
		return ::governance::actor_for_institution(*state, i);
	}
};

TEST_CASE("fiscal_ledger_counts_what_crosses_the_perimeter_once", "[governance][finance][fiscal-ledger]") {
	fixture f;
	auto& s = *f.state;
	s.current_date = sys::date{ 10 };
	// money moved between two institutions of the state is not revenue or spending
	REQUIRE(::economy::accounts::transfer(s, f.ministry_treasury, f.works_treasury, 30.0f, transaction_kind::public_spending, s.current_date));
	// final spending is classified by the function of the institution that spends
	REQUIRE(::economy::accounts::transfer(s, f.works_treasury, f.firm_account, 20.0f, transaction_kind::purchase, s.current_date));
	// a payment to a person outside payroll is a transfer
	REQUIRE(::economy::accounts::transfer(s, f.ministry_treasury, f.person_account, 5.0f, transaction_kind::public_spending, s.current_date));
	// a dividend to the state as owner is state enterprise revenue; a loan received is financing
	REQUIRE(::economy::accounts::transfer(s, f.firm_account, f.ministry_treasury, 12.0f, transaction_kind::dividend, s.current_date));
	REQUIRE(::economy::accounts::transfer(s, f.firm_account, f.ministry_treasury, 25.0f, transaction_kind::loan_issuance, s.current_date));
	// taxes are posted by the fiscal API with the tax they are, once
	REQUIRE(::economy::accounts::transfer(s, f.firm_account, f.ministry_treasury, 7.0f, transaction_kind::tax_payment, s.current_date));
	finance::post_tax_collected(s, f.ministry, f.actor(f.households), finance::revenue_source::income_tax, 40.0f, ::economy::money);
	finance::post_tax_collected(s, f.ministry, f.actor(f.works), finance::revenue_source::dividend_tax, 3.0f, ::economy::money);
	finance::post_tax_collected(s, f.ministry, f.firm, finance::revenue_source::customs_duties, 7.0f, ::economy::money);
	finance::post_tax_assessed(s, f.ministry, f.firm, 9.0f, ::economy::money);
	finance::post_debt_service(s, f.ministry, 4.0f, 6.0f, ::economy::money);
	finance::close_fiscal_day(s, false);

	auto r = finance::report(s, f.nation, finance::fiscal_period::days_30, finance::fiscal_step::daily);
	REQUIRE(r.recorded_days == 1);
	REQUIRE(r.revenue[size_t(finance::revenue_source::income_tax)] == Approx(40.0f));
	REQUIRE(r.revenue[size_t(finance::revenue_source::customs_duties)] == Approx(7.0f));
	REQUIRE(r.revenue[size_t(finance::revenue_source::dividend_tax)] == Approx(0.0f)); // paid by the state to itself
	REQUIRE(r.revenue[size_t(finance::revenue_source::state_enterprises)] == Approx(12.0f));
	REQUIRE(r.revenue[size_t(finance::revenue_source::other)] == Approx(0.0f));
	REQUIRE(r.spending[size_t(finance::spending_function::infrastructure)] == Approx(20.0f));
	REQUIRE(r.spending[size_t(finance::spending_function::social_transfers)] == Approx(5.0f));
	REQUIRE(r.spending[size_t(finance::spending_function::administration)] == Approx(0.0f));
	REQUIRE(r.spending[size_t(finance::spending_function::debt_service)] == Approx(4.0f));
	REQUIRE(r.principal_repaid == Approx(6.0f));
	REQUIRE(r.borrowed == Approx(25.0f));
	REQUIRE(r.tax_assessed == Approx(9.0f));
	REQUIRE(r.revenue_total == Approx(59.0f));
	REQUIRE(r.spending_total == Approx(29.0f));
	REQUIRE(r.balance == Approx(30.0f));
	// cash at the close: every treasury but the household sector's pass-through
	REQUIRE(r.treasury == Approx(100.0f - 20.0f - 5.0f + 12.0f + 25.0f + 7.0f));
	REQUIRE(r.buckets.size() == 30);
	REQUIRE(r.buckets.back().recorded);
	REQUIRE_FALSE(r.buckets.front().recorded);
	REQUIRE_FALSE(r.previous_known);
}

TEST_CASE("fiscal_ledger_periods_compare_and_survive_a_save", "[governance][finance][fiscal-ledger]") {
	fixture f;
	auto& s = *f.state;
	for(int32_t day = 0; day < 70; ++day) {
		s.current_date = sys::date{ uint16_t(100 + day) };
		finance::post_tax_collected(s, f.ministry, f.firm, finance::revenue_source::income_tax, day < 40 ? 10.0f : 20.0f, ::economy::money);
		finance::close_fiscal_day(s, false);
	}
	// presimulated days leave nothing on the books
	finance::post_tax_collected(s, f.ministry, f.firm, finance::revenue_source::income_tax, 1000.0f, ::economy::money);
	finance::close_fiscal_day(s, true);

	auto r = finance::report(s, f.nation, finance::fiscal_period::days_30, finance::fiscal_step::daily);
	REQUIRE(r.recorded_days == 30);
	REQUIRE(r.revenue_total == Approx(600.0f));
	REQUIRE(r.previous_known);
	REQUIRE(r.previous_revenue == Approx(10.0f * 30.0f));
	auto weekly = finance::report(s, f.nation, finance::fiscal_period::days_90, finance::fiscal_step::weekly);
	REQUIRE(weekly.buckets.size() == 13);
	REQUIRE(weekly.revenue_total == Approx(40.0f * 10.0f + 30.0f * 20.0f));

	auto saved = finance::export_ledger(s);
	REQUIRE(finance::import_ledger(s, saved));
	auto restored = finance::report(s, f.nation, finance::fiscal_period::days_30, finance::fiscal_step::daily);
	REQUIRE(restored.revenue_total == Approx(r.revenue_total));
	REQUIRE(restored.previous_revenue == Approx(r.previous_revenue));
	REQUIRE(finance::export_ledger(s).size() == saved.size());
}

TEST_CASE("fiscal_ledger_credit_assessment_is_its_weakest_factor", "[governance][finance][fiscal-ledger]") {
	REQUIRE(finance::grade_of(0.96f, false) == finance::credit_grade::aaa);
	REQUIRE(finance::grade_of(0.55f, false) == finance::credit_grade::bb);
	REQUIRE(finance::grade_of(0.05f, false) == finance::credit_grade::d);
	REQUIRE(finance::grade_of(1.0f, true) == finance::credit_grade::d);
	fixture f;
	auto& s = *f.state;
	// fewer than thirty days of revenue: nothing to annualize
	for(int32_t day = 0; day < 10; ++day) {
		s.current_date = sys::date{ uint16_t(100 + day) };
		finance::post_tax_collected(s, f.ministry, f.firm, finance::revenue_source::income_tax, 10.0f, ::economy::money);
		finance::close_fiscal_day(s, false);
	}
	REQUIRE_FALSE(finance::assess_credit(s, f.nation).assessed);
	for(int32_t day = 10; day < 40; ++day) {
		s.current_date = sys::date{ uint16_t(100 + day) };
		finance::post_tax_collected(s, f.ministry, f.firm, finance::revenue_source::income_tax, 10.0f, ::economy::money);
		finance::close_fiscal_day(s, false);
	}
	auto credit = finance::assess_credit(s, f.nation);
	REQUIRE(credit.assessed);
	REQUIRE(credit.annual_revenue == Approx(3650.0f));
	// no debt and a surplus: every factor full
	REQUIRE(credit.score == Approx(1.0f));
	REQUIRE(credit.grade == finance::credit_grade::aaa);
}

} // namespace fiscal_ledger_tests

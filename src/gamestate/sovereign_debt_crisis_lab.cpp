#include "sovereign_debt_crisis_lab.hpp"

#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/consent/consent.hpp"
#include "economy/relations/relations.hpp"
#include "governance/finance/finance.hpp"
#include "governance/governance.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace sys::simulation::sovereign_debt_lab {
namespace {

constexpr uint16_t start_day = 100;
constexpr float opening_taxpayer_cash = 10000.0f;
constexpr float opening_bank_reserves = 2000.0f;
constexpr float opening_bank_deposits = 1750.0f;
constexpr float bond_principal = 600.0f;
constexpr float bond_rate = 0.05f;
constexpr uint32_t bond_tenor_days = 365;
constexpr float normal_daily_revenue = 12.0f;
constexpr float crisis_daily_revenue = 3.0f;
constexpr float normal_daily_spending = 10.0f;
constexpr float austerity_daily_spending = 2.0f;

struct handles {
	dcon::nation_id nation{};
	dcon::institution_id government{};
	dcon::office_id finance_office{};
	dcon::person_id minister{};
	dcon::commodity_id settlement{};
	dcon::monetary_account_id treasury{};
	dcon::economic_actor_id taxpayer{};
	dcon::person_id taxpayer_person{};
	dcon::monetary_account_id taxpayer_account{};
	dcon::economic_actor_id service_provider{};
	dcon::monetary_account_id service_account{};
	dcon::organization_id bank{};
	dcon::person_id bank_depositor{};
	dcon::deposit_account_id bank_deposit{};
	dcon::person_id bank_representative{};
	dcon::monetary_account_id bank_reserves{};
	dcon::obligation_id sovereign_bond{};
};

struct lab_world {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	handles h{};
	float initial_money = 0.0f;
};

std::string number(float value) {
	if(!std::isfinite(value)) return {};
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::fixed << std::setprecision(6) << value;
	return out.str();
}

float total_money(sys::state const& state) {
	float total = 0.0f;
	state.world.for_each_monetary_account([&](dcon::monetary_account_id account) {
		total += state.world.monetary_account_get_balance(account);
	});
	return total;
}

uint64_t checksum(run_output const& out) {
	uint64_t value = 1469598103934665603ull;
	auto fold = [&](std::string const& row) {
		for(unsigned char byte : row) {
			value ^= byte;
			value *= 1099511628211ull;
		}
	};
	for(auto const& row : out.timeseries_csv) fold(row);
	for(auto const& row : out.events_csv) fold(row);
	return value;
}

void add_event(run_output& out, uint32_t day, std::string const& kind,
	float quantity, float cash, std::string const& note) {
	std::ostringstream row;
	row.imbue(std::locale::classic());
	row << day << ',' << scenario_name(out.experiment) << ',' << kind << ','
		<< number(quantity) << ',' << number(cash) << ',' << note;
	out.events_csv.push_back(row.str());
}

lab_world initialize(config const& cfg) {
	lab_world world;
	auto& state = *world.state;
	auto& h = world.h;
	state.current_date = sys::date{start_day};
	state.game_seed = cfg.seed;
	state.inflation = 1.0f;

	h.nation = state.world.create_nation();
	h.government = governance::create_institution(state, h.nation,
		governance::institution_kind::central_government);
	h.finance_office = governance::create_office(state, h.government,
		governance::office_kind::finance_minister);
	h.minister = persons::create_person(state, sys::date{1});
	if(!h.government || !h.finance_office || !h.minister
		|| !persons::appoint_person(state, h.minister, h.finance_office, state.current_date))
		throw std::runtime_error("could not establish the finance ministry fixture");
	for(auto authority : {governance::authority_kind::levy_tax,
		governance::authority_kind::spend_public_funds,
		governance::authority_kind::issue_public_debt}) {
		if(!governance::grant_authority_to_office(state, h.finance_office, authority, h.nation))
			throw std::runtime_error("could not grant finance ministry authority");
	}

	h.settlement = state.world.create_commodity();
	h.treasury = governance::finance::open_treasury_account(state, h.government, h.settlement);
	h.taxpayer_person = persons::create_person(state, sys::date{1});
	h.taxpayer = persons::actor_for_person(state, h.taxpayer_person);
	h.taxpayer_account = economy::accounts::open_account(state, h.taxpayer, h.settlement);
	h.service_provider = state.world.create_economic_actor();
	h.service_account = economy::accounts::open_account(state, h.service_provider, h.settlement);
	if(!h.treasury || !h.taxpayer_account || !h.service_account
		|| !economy::accounts::bootstrap_set_balance(state, h.treasury, 0.0f)
		|| !economy::accounts::bootstrap_set_balance(state, h.taxpayer_account, opening_taxpayer_cash)
		|| !economy::accounts::bootstrap_set_balance(state, h.service_account, 0.0f))
		throw std::runtime_error("could not establish fiscal accounts");

	h.bank = economy::banking::create_bank(state);
	economy::banking::bank_policy bank_policy{};
	bank_policy.jurisdiction = h.nation;
	bank_policy.settlement = h.settlement;
	bank_policy.minimum_capital_ratio = 0.08f;
	bank_policy.liquidity_target = 0.05f;
	bank_policy.risk_appetite = 1.0f;
	bank_policy.max_single_borrower_exposure = 1.0f;
	bank_policy.reserve_requirement = 0.05f;
	bank_policy.capital_breach_grace_days = 30;
	if(!h.bank || !economy::banking::configure_bank_policy(state, h.bank, bank_policy))
		throw std::runtime_error("could not configure creditor bank");
	h.bank_reserves = economy::banking::open_reserve_account(state, h.bank, h.settlement);
	if(!h.bank_reserves || !economy::banking::bootstrap_set_reserve_balance(
		state, h.bank_reserves, opening_bank_reserves))
		throw std::runtime_error("could not fund creditor bank reserves");
	h.bank_depositor = persons::create_person(state, sys::date{1});
	auto depositor_actor = persons::actor_for_person(state, h.bank_depositor);
	state.world.economic_actor_set_canonical_id(depositor_actor,
		0xD3B7000000000000ULL + uint64_t(h.bank_depositor.index()));
	h.bank_deposit = economy::banking::open_deposit_account(state, h.bank,
		depositor_actor, h.settlement);
	if(!h.bank_deposit || !economy::banking::bootstrap_set_deposit_balance(
		state, h.bank_deposit, opening_bank_deposits))
		throw std::runtime_error("could not establish the bank's opening deposit liability");
	h.bank_representative = persons::create_person(state, sys::date{1});
	if(!h.bank_representative || !economy::consent::create_mandate(state, h.bank,
		h.bank_representative, economy::consent::decision_kind::invest, state.current_date))
		throw std::runtime_error("could not establish the bank investment mandate");

	const auto due_date = sys::date{uint16_t(state.current_date.to_raw_value() + bond_tenor_days)};
	auto bank_actor = actors::organizations::actor_for_organization(state, h.bank);
	auto government_actor = governance::actor_for_institution(state, h.government);
	auto proposal = economy::consent::create_proposal(state,
		economy::consent::proposal_kind::investment, government_actor, bank_actor,
		h.settlement, bond_principal, due_date, bond_rate, state.current_date);
	if(!proposal || !economy::consent::accept_proposal(state, proposal, bank_actor,
		h.bank_representative, state.current_date))
		throw std::runtime_error("bank did not accept the sovereign bond offer");
	auto issue = governance::finance::authorized_issue_public_debt_with_consent(state,
		h.minister, h.treasury, h.bank_reserves, bond_principal, due_date, bond_rate,
		state.current_date, proposal);
	if(!issue) throw std::runtime_error("authorized sovereign bond issuance failed");
	h.sovereign_bond = state.world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(issue);
	if(!h.sovereign_bond) throw std::runtime_error("sovereign issue did not create a debt claim");
	world.initial_money = total_money(state);
	return world;
}

float stress_net_worth(float base_net_worth, float defaulted_claim, float recovery_rate) {
	return base_net_worth - defaulted_claim * (1.0f - recovery_rate);
}

} // namespace

scenario parse_scenario(std::string const& value) {
	if(value == "baseline") return scenario::baseline;
	if(value == "revenue-shock" || value == "recession") return scenario::revenue_shock;
	if(value == "austerity") return scenario::austerity;
	throw std::invalid_argument("unknown sovereign-debt scenario: " + value);
}

std::string_view scenario_name(scenario value) noexcept {
	switch(value) {
	case scenario::baseline: return "baseline";
	case scenario::revenue_shock: return "revenue_shock";
	case scenario::austerity: return "austerity";
	}
	return "unknown";
}

run_output run(config const& cfg) {
	if(cfg.days == 0 || cfg.days > 50000 || (cfg.experiment != scenario::baseline
		&& (cfg.shock_day == 0 || cfg.shock_day >= cfg.days)))
		throw std::invalid_argument("sovereign-debt lab needs a 1..50000 day horizon and an in-horizon shock date");
	lab_world world = initialize(cfg);
	auto& state = *world.state;
	auto const& h = world.h;
	run_output out;
	out.experiment = cfg.experiment;
	out.seed = cfg.seed;
	out.initial_money = world.initial_money;
	out.timeseries_csv.push_back(
		"day,scenario,tax_assessed_cash,tax_collected_cash,planned_service_cash,service_paid_cash,service_shortfall_cash,interest_accrued_cash,debt_service_paid_cash,debt_principal_outstanding_cash,debt_interest_outstanding_cash,debt_overdue_cash,debt_defaulted_cash,treasury_cash_cash,bank_reserves_cash,bank_public_debt_assets_cash,bank_defaulted_debt_assets_cash,bank_net_worth_cash,bank_status,marked_net_worth_recovery_0_cash,marked_net_worth_recovery_25_cash,marked_net_worth_recovery_50_cash,marked_net_worth_recovery_75_cash,marked_net_worth_recovery_100_cash,money_conservation_error_cash");
	out.events_csv.push_back("day,scenario,event,quantity,cash,note");
	add_event(out, 0, "consented_sovereign_bond_issued", bond_principal, bond_principal,
		"bank reserves transferred to government treasury; 5pct annual rate; 365-day maturity");
	const auto base_date = state.current_date.to_raw_value();
	for(uint32_t day = 1; day <= cfg.days; ++day) {
		state.current_date = sys::date{uint16_t(base_date + day)};
		const bool crisis = cfg.experiment != scenario::baseline && day >= cfg.shock_day;
		const float revenue = crisis ? crisis_daily_revenue : normal_daily_revenue;
		const float planned_spending = crisis && cfg.experiment == scenario::austerity
			? austerity_daily_spending : normal_daily_spending;

		auto tax_action = governance::finance::authorized_assess_tax(state, h.minister,
			h.taxpayer, h.treasury, revenue, state.current_date, state.current_date);
		if(!tax_action) throw std::runtime_error("daily tax assessment was rejected");
		auto tax_obligation = state.world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(tax_action);
		auto tax_payment = governance::finance::pay_tax(state, tax_obligation,
			h.taxpayer_account, h.treasury, revenue, state.current_date);
		if(!tax_payment) throw std::runtime_error("daily tax collection was not settled");
		add_event(out, day, "tax_assessed_and_collected", revenue, revenue,
			"authorized tax assessment and linked account settlement");

		const float available_cash = economy::accounts::balance(state, h.treasury);
		const float service_paid = std::min(planned_spending, available_cash);
		if(service_paid > 0.0f && !governance::finance::authorized_spend(state, h.minister,
			h.treasury, h.service_account, service_paid, state.current_date))
			throw std::runtime_error("daily public service payment was rejected");
		const float service_shortfall = planned_spending - service_paid;
		out.total_public_service_paid += service_paid;
		out.total_public_service_shortfall += service_shortfall;
		if(service_paid > 0.0f) add_event(out, day, "public_service_funded", service_paid,
			service_paid, "fixture service provider receives an authorized government payment");

		auto const debt_status_before = state.world.obligation_get_status(h.sovereign_bond);
		auto debt_service = governance::finance::process_public_debt(state, h.government,
			state.current_date);
		if(debt_service.amount_paid > 0.0f) add_event(out, day, "sovereign_debt_service_paid",
			debt_service.amount_paid,
			debt_service.amount_paid, "real transaction linked to the sovereign obligation");
		auto const debt_status = state.world.obligation_get_status(h.sovereign_bond);
		if(debt_status_before != uint8_t(economy::relations::obligation_status::defaulted)
			&& debt_status == uint8_t(economy::relations::obligation_status::defaulted))
			add_event(out, day, "sovereign_payment_defaulted",
				economy::relations::total_due(state, h.sovereign_bond), 0.0f,
				"30-day statutory grace period expired; claim remains on creditor books at face value");

		economy::banking::update_bank_statuses(state, state.current_date);
		auto fiscal = governance::finance::fiscal_position_for(state, h.government, h.settlement);
		auto bank_sheet = economy::banking::bank_balance_sheet(state, h.bank, h.settlement);
		const float error = std::abs(total_money(state) - world.initial_money);
		out.maximum_money_conservation_error = std::max(out.maximum_money_conservation_error, error);
		const float adjusted[5] = {
			stress_net_worth(bank_sheet.net_worth, bank_sheet.defaulted_public_debt_assets, 0.0f),
			stress_net_worth(bank_sheet.net_worth, bank_sheet.defaulted_public_debt_assets, 0.25f),
			stress_net_worth(bank_sheet.net_worth, bank_sheet.defaulted_public_debt_assets, 0.50f),
			stress_net_worth(bank_sheet.net_worth, bank_sheet.defaulted_public_debt_assets, 0.75f),
			stress_net_worth(bank_sheet.net_worth, bank_sheet.defaulted_public_debt_assets, 1.0f)};
		std::ostringstream row;
		row.imbue(std::locale::classic());
		row << day << ',' << scenario_name(cfg.experiment) << ',' << number(revenue) << ','
			<< number(revenue) << ',' << number(planned_spending) << ',' << number(service_paid) << ','
			<< number(service_shortfall) << ',' << number(debt_service.interest_accrued) << ','
			<< number(debt_service.amount_paid) << ',' << number(fiscal.public_debt_principal_outstanding) << ','
			<< number(fiscal.public_debt_accrued_interest) << ',' << number(fiscal.public_debt_overdue) << ','
			<< number(fiscal.public_debt_defaulted) << ',' << number(fiscal.treasury_cash) << ','
			<< number(economy::accounts::balance(state, h.bank_reserves)) << ','
			<< number(bank_sheet.public_debt_assets) << ','
			<< number(bank_sheet.defaulted_public_debt_assets) << ',' << number(bank_sheet.net_worth) << ','
			<< uint32_t(economy::banking::status_of(state, h.bank)) << ','
			<< number(adjusted[0]) << ',' << number(adjusted[1]) << ',' << number(adjusted[2]) << ','
			<< number(adjusted[3]) << ',' << number(adjusted[4]) << ',' << number(error);
		out.timeseries_csv.push_back(row.str());
		out.ticks_completed = day;
	}

	auto const final_position = governance::finance::fiscal_position_for(state,
		h.government, h.settlement);
	auto const final_sheet = economy::banking::bank_balance_sheet(state, h.bank, h.settlement);
	out.final_debt_outstanding = final_position.public_debt_outstanding;
	out.final_defaulted_debt = final_position.public_debt_defaulted;
	out.debt_reached_default = state.world.obligation_get_status(h.sovereign_bond)
		== uint8_t(economy::relations::obligation_status::defaulted);
	out.final_bank_public_debt_assets = final_sheet.public_debt_assets;
	out.final_bank_defaulted_debt_assets = final_sheet.defaulted_public_debt_assets;
	out.final_bank_net_worth = final_sheet.net_worth;
	out.bank_net_worth_at_0pct_recovery = stress_net_worth(final_sheet.net_worth,
		final_sheet.defaulted_public_debt_assets, 0.0f);
	out.bank_net_worth_at_25pct_recovery = stress_net_worth(final_sheet.net_worth,
		final_sheet.defaulted_public_debt_assets, 0.25f);
	out.bank_net_worth_at_50pct_recovery = stress_net_worth(final_sheet.net_worth,
		final_sheet.defaulted_public_debt_assets, 0.50f);
	out.bank_net_worth_at_75pct_recovery = stress_net_worth(final_sheet.net_worth,
		final_sheet.defaulted_public_debt_assets, 0.75f);
	out.bank_net_worth_at_100pct_recovery = stress_net_worth(final_sheet.net_worth,
		final_sheet.defaulted_public_debt_assets, 1.0f);
	out.final_bank_status = uint8_t(economy::banking::status_of(state, h.bank));
	out.final_checksum = checksum(out);
	return out;
}

bool write_outputs(run_output const& out, std::string const& output_directory) {
	std::filesystem::create_directories(output_directory);
	auto write_rows = [](std::filesystem::path const& path, std::vector<std::string> const& rows) {
		std::ofstream file(path, std::ios::binary);
		if(!file) return false;
		for(auto const& row : rows) file << row << '\n';
		return bool(file);
	};
	if(!write_rows(std::filesystem::path(output_directory) / "timeseries.csv", out.timeseries_csv)
		|| !write_rows(std::filesystem::path(output_directory) / "events.csv", out.events_csv)) return false;
	std::ofstream metrics(std::filesystem::path(output_directory) / "summary.csv", std::ios::binary);
	if(!metrics) return false;
	metrics.imbue(std::locale::classic());
	metrics << "scenario,seed,days_completed,checksum,debt_defaulted,final_debt_outstanding_cash,final_defaulted_claim_cash,service_paid_cash,service_shortfall_cash,bank_claims_face_value_cash,bank_defaulted_claim_cash,bank_base_model_net_worth_cash,bank_marked_net_worth_recovery_0_cash,bank_marked_net_worth_recovery_25_cash,bank_marked_net_worth_recovery_50_cash,bank_marked_net_worth_recovery_75_cash,bank_marked_net_worth_recovery_100_cash,bank_status,money_initial_cash,money_conservation_error_cash\n";
	metrics << scenario_name(out.experiment) << ',' << out.seed << ',' << out.ticks_completed
		<< ',' << out.final_checksum << ',' << (out.debt_reached_default ? "true" : "false")
		<< ',' << number(out.final_debt_outstanding) << ',' << number(out.final_defaulted_debt)
		<< ',' << number(out.total_public_service_paid) << ',' << number(out.total_public_service_shortfall)
		<< ',' << number(out.final_bank_public_debt_assets) << ','
		<< number(out.final_bank_defaulted_debt_assets) << ',' << number(out.final_bank_net_worth)
		<< ',' << number(out.bank_net_worth_at_0pct_recovery) << ','
		<< number(out.bank_net_worth_at_25pct_recovery) << ','
		<< number(out.bank_net_worth_at_50pct_recovery) << ','
		<< number(out.bank_net_worth_at_75pct_recovery) << ','
		<< number(out.bank_net_worth_at_100pct_recovery) << ','
		<< uint32_t(out.final_bank_status) << ',' << number(out.initial_money) << ','
		<< number(out.maximum_money_conservation_error) << '\n';
	return bool(metrics);
}

} // namespace sys::simulation::sovereign_debt_lab

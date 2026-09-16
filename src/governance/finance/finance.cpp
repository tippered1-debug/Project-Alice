#include "finance.hpp"

#include "economy/accounts/accounts.hpp"
#include "economy/consent/consent.hpp"
#include "economy/relations/relations.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>

namespace governance::finance {

namespace {

using economy::relations::obligation_kind;
using economy::relations::obligation_status;
using economy::relations::transaction_kind;

bool valid_amount(float amount) { return std::isfinite(amount) && amount > 0.0f; }

bool live_obligation(sys::state const& state, dcon::obligation_id obligation) {
	if(!obligation || !state.world.obligation_is_valid(obligation)) return false;
	auto status = state.world.obligation_get_status(obligation);
	return status != uint8_t(obligation_status::paid)
		&& status != uint8_t(obligation_status::written_off)
		&& economy::relations::total_due(state, obligation) > 0.0f;
}

bool payable_obligation(sys::state const& state, dcon::obligation_id obligation) {
	return live_obligation(state, obligation)
		&& state.world.obligation_get_status(obligation) == uint8_t(obligation_status::active);
}

dcon::office_id office_for_tenure(sys::state const& state, dcon::office_tenure_id tenure) {
	return tenure ? state.world.office_tenure_get_office_from_office_tenure_office(tenure) : dcon::office_id{};
}

struct authority_context {
	dcon::institution_id institution{};
	dcon::office_id office{};
	dcon::commodity_id settlement{};
};

bool authority_for_treasury(sys::state const& state, dcon::person_id initiator,
	authority_kind kind, dcon::monetary_account_id treasury_account, sys::date date,
	authority_context& result) {
	if(!initiator || !state.world.person_is_valid(initiator) || !state.world.person_get_alive(initiator)
		|| !treasury_account || !state.world.monetary_account_is_valid(treasury_account)) return false;
	result.institution = treasury_institution_for(state, treasury_account);
	if(!result.institution) return false;
	auto nation = governance::nation_of(state, result.institution);
	if(!nation) return false;
	auto tenure = persons::authority_tenure_on_or_before(state, initiator, kind, nation, date);
	if(!tenure) return false;
	result.office = office_for_tenure(state, tenure);
	result.settlement = economy::accounts::settlement_of(state, treasury_account);
	return result.office && result.settlement;
}

dcon::fiscal_action_id record_action(sys::state& state, fiscal_action_kind kind,
	dcon::person_id initiator, dcon::office_id office, dcon::institution_id institution,
	dcon::monetary_account_id treasury_account, sys::date date,
	dcon::obligation_id obligation = {}, dcon::transaction_id transaction = {}) {
	auto action = state.world.create_fiscal_action();
	state.world.fiscal_action_set_kind(action, uint8_t(kind));
	state.world.fiscal_action_set_occurred_on(action, date);
	state.world.force_create_fiscal_action_initiator(action, initiator);
	state.world.force_create_fiscal_action_authorizing_office(action, office);
	state.world.force_create_fiscal_action_treasury_institution(action, institution);
	state.world.force_create_fiscal_action_treasury_account(action, treasury_account);
	if(obligation) state.world.force_create_fiscal_action_resulting_obligation(action, obligation);
	if(transaction) state.world.force_create_fiscal_action_resulting_transaction(action, transaction);
	return action;
}

bool valid_treasury_account(sys::state const& state, dcon::monetary_account_id account,
	dcon::institution_id institution, dcon::commodity_id settlement) {
	return account && state.world.monetary_account_is_valid(account)
		&& treasury_institution_for(state, account) == institution
		&& economy::accounts::owner_of(state, account) == governance::actor_for_institution(state, institution)
		&& economy::accounts::settlement_of(state, account) == settlement;
}

dcon::institution_id institution_for_actor(sys::state const& state, dcon::economic_actor_id actor) {
	return actor ? state.world.economic_actor_get_institution_from_institution_actor(actor) : dcon::institution_id{};
}

} // namespace

dcon::institution_id treasury_institution_for(sys::state const& state, dcon::monetary_account_id account) {
	return account ? state.world.monetary_account_get_institution_from_institution_treasury_account(account) : dcon::institution_id{};
}

dcon::monetary_account_id treasury_account_for(sys::state const& state, dcon::institution_id institution,
	dcon::commodity_id settlement) {
	if(!institution || !settlement) return {};
	dcon::monetary_account_id result{};
	state.world.institution_for_each_institution_treasury_account_as_institution(institution,
		[&](dcon::institution_treasury_account_id relation) {
			auto account = state.world.institution_treasury_account_get_monetary_account(relation);
			if(account && economy::accounts::settlement_of(state, account) == settlement) result = account;
		});
	return result;
}

dcon::monetary_account_id open_treasury_account(sys::state& state, dcon::institution_id institution,
	dcon::commodity_id settlement) {
	if(!institution || !state.world.institution_is_valid(institution) || !settlement
		|| !state.world.commodity_is_valid(settlement)) return {};
	auto actor = governance::actor_for_institution(state, institution);
	if(!actor) return {};
	if(auto existing = treasury_account_for(state, institution, settlement)) return existing;
	auto account = economy::accounts::open_account(state, actor, settlement);
	if(!account) return {};
	state.world.force_create_institution_treasury_account(account, institution);
	return account;
}

dcon::fiscal_action_id authorized_assess_tax(sys::state& state, dcon::person_id initiator,
	dcon::economic_actor_id taxpayer_actor, dcon::monetary_account_id treasury_account,
	float amount, sys::date due_date, sys::date date) {
	authority_context context{};
	if(!valid_amount(amount) || due_date < date || !taxpayer_actor || !state.world.economic_actor_is_valid(taxpayer_actor)
		|| !authority_for_treasury(state, initiator, authority_kind::levy_tax, treasury_account, date, context)) return {};
	auto creditor = governance::actor_for_institution(state, context.institution);
	auto obligation = economy::relations::create_obligation(state, taxpayer_actor, creditor, amount,
		context.settlement, date, due_date, 0.0f, obligation_kind::tax);
	if(!obligation) return {};
	return record_action(state, fiscal_action_kind::tax_assessment, initiator, context.office,
		context.institution, treasury_account, date, obligation);
}

dcon::transaction_id pay_tax(sys::state& state, dcon::obligation_id tax_obligation,
	dcon::monetary_account_id taxpayer_account, dcon::monetary_account_id treasury_account,
	float amount, sys::date date) {
	if(!valid_amount(amount) || !tax_obligation || !state.world.obligation_is_valid(tax_obligation)
		|| state.world.obligation_get_kind(tax_obligation) != uint8_t(obligation_kind::tax)
		|| !payable_obligation(state, tax_obligation)) return {};
	auto debtor = state.world.obligation_get_economic_actor_from_obligation_debtor(tax_obligation);
	auto creditor = state.world.obligation_get_economic_actor_from_obligation_creditor(tax_obligation);
	auto settlement = state.world.obligation_get_settlement_commodity(tax_obligation);
	auto institution = institution_for_actor(state, creditor);
	if(!institution || !valid_treasury_account(state, treasury_account, institution, settlement)
		|| economy::accounts::owner_of(state, taxpayer_account) != debtor
		|| economy::accounts::settlement_of(state, taxpayer_account) != settlement) return {};
	auto accepted = std::min(amount, economy::relations::total_due(state, tax_obligation));
	if(accepted <= 0.0f || economy::accounts::balance(state, taxpayer_account) < accepted) return {};
	auto transaction = economy::accounts::transfer(state, taxpayer_account, treasury_account, accepted,
		transaction_kind::tax_payment, date);
	if(!transaction || economy::relations::repay_obligation(state, tax_obligation, accepted) != accepted) return {};
	return transaction;
}

dcon::fiscal_action_id authorized_spend(sys::state& state, dcon::person_id initiator,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id recipient_account,
	float amount, sys::date date) {
	authority_context context{};
	if(!valid_amount(amount) || !authority_for_treasury(state, initiator, authority_kind::spend_public_funds,
		treasury_account, date, context) || !recipient_account || !state.world.monetary_account_is_valid(recipient_account)
		|| economy::accounts::settlement_of(state, recipient_account) != context.settlement
		|| economy::accounts::balance(state, treasury_account) < amount) return {};
	auto transaction = economy::accounts::transfer(state, treasury_account, recipient_account, amount,
		transaction_kind::public_spending, date);
	if(!transaction) return {};
	return record_action(state, fiscal_action_kind::public_spending, initiator, context.office,
		context.institution, treasury_account, date, {}, transaction);
}

namespace {
dcon::fiscal_action_id execute_public_debt_raw(sys::state& state, dcon::person_id initiator,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id investor_account,
	float principal, sys::date due_date, float annual_interest_rate, sys::date date) {
	authority_context context{};
	if(!valid_amount(principal) || due_date < date || !std::isfinite(annual_interest_rate) || annual_interest_rate < 0.0f
		|| !authority_for_treasury(state, initiator, authority_kind::issue_public_debt, treasury_account, date, context)
		|| !investor_account || !state.world.monetary_account_is_valid(investor_account)
		|| economy::accounts::settlement_of(state, investor_account) != context.settlement
		|| economy::accounts::balance(state, investor_account) < principal) return {};
	auto debtor = governance::actor_for_institution(state, context.institution);
	auto creditor = economy::accounts::owner_of(state, investor_account);
	if(!debtor || !creditor) return {};
	auto nation = governance::nation_of(state, context.institution);
	auto policy = governance::law::public_debt_policy_for(state, nation, context.settlement, date);
	if(!policy.issuance_allowed) return {};
	if(policy.ceiling && governance::finance::national_public_debt(state, nation, context.settlement) + principal > *policy.ceiling) return {};
	auto obligation = economy::relations::create_obligation(state, debtor, creditor, principal, context.settlement,
		date, due_date, annual_interest_rate, obligation_kind::public_debt);
	if(!obligation) return {};
	auto transaction = economy::accounts::transfer(state, investor_account, treasury_account, principal,
		transaction_kind::public_debt_issuance, date);
	if(!transaction) return {};
	return record_action(state, fiscal_action_kind::public_debt_issuance, initiator, context.office,
		context.institution, treasury_account, date, obligation, transaction);
}
}

dcon::fiscal_action_id authorized_issue_public_debt_with_consent(sys::state& state, dcon::person_id initiator,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id investor_account,
	float principal, sys::date due_date, float annual_interest_rate, sys::date date,
	dcon::economic_proposal_id investor_proposal) {
	if(!investor_proposal || !state.world.economic_proposal_is_valid(investor_proposal)
		|| state.world.economic_proposal_get_kind(investor_proposal) != uint8_t(economy::consent::proposal_kind::investment)
		|| !investor_account || !state.world.monetary_account_is_valid(investor_account)) return {};
	authority_context context{};
	if(!authority_for_treasury(state, initiator, authority_kind::issue_public_debt, treasury_account, date, context)) return {};
	auto issuer = governance::actor_for_institution(state, context.institution);
	auto investor = economy::accounts::owner_of(state, investor_account);
	if(!issuer || !investor
		|| state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_a(investor_proposal) != issuer
		|| state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_b(investor_proposal) != investor
		|| state.world.economic_proposal_get_settlement(investor_proposal) != context.settlement
		|| state.world.economic_proposal_get_amount(investor_proposal) != principal
		|| state.world.economic_proposal_get_due_date(investor_proposal) != due_date
		|| state.world.economic_proposal_get_annual_interest_rate(investor_proposal) != annual_interest_rate
		|| !economy::consent::proposal_fully_accepted(state, investor_proposal, date)) return {};
	// All consent and authority checks precede the internal atomic issuance primitive.
	auto action = execute_public_debt_raw(state, initiator, treasury_account, investor_account,
		principal, due_date, annual_interest_rate, date);
	if(!action || !economy::consent::mark_executed(state, investor_proposal)) return {};
	return action;
}

float accrue_public_debt_interest(sys::state& state, dcon::obligation_id obligation, uint32_t days) {
	if(!obligation || !state.world.obligation_is_valid(obligation)
		|| state.world.obligation_get_kind(obligation) != uint8_t(obligation_kind::public_debt)) return 0.0f;
	return economy::relations::accrue_interest(state, obligation, days);
}

dcon::transaction_id service_public_debt(sys::state& state, dcon::obligation_id obligation,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id holder_account,
	float amount, sys::date date) {
	if(!valid_amount(amount) || !obligation || !state.world.obligation_is_valid(obligation)
		|| state.world.obligation_get_kind(obligation) != uint8_t(obligation_kind::public_debt)
		|| !payable_obligation(state, obligation)) return {};
	auto debtor = state.world.obligation_get_economic_actor_from_obligation_debtor(obligation);
	auto creditor = state.world.obligation_get_economic_actor_from_obligation_creditor(obligation);
	auto settlement = state.world.obligation_get_settlement_commodity(obligation);
	auto institution = institution_for_actor(state, debtor);
	if(!institution || !valid_treasury_account(state, treasury_account, institution, settlement)
		|| economy::accounts::owner_of(state, holder_account) != creditor
		|| economy::accounts::settlement_of(state, holder_account) != settlement) return {};
	auto accepted = std::min(amount, economy::relations::total_due(state, obligation));
	if(accepted <= 0.0f || economy::accounts::balance(state, treasury_account) < accepted) return {};
	auto transaction = economy::accounts::transfer(state, treasury_account, holder_account, accepted,
		transaction_kind::public_debt_service, date);
	if(!transaction || economy::relations::repay_obligation(state, obligation, accepted) != accepted) return {};
	return transaction;
}

float public_debt_held_by(sys::state const& state, dcon::economic_actor_id holder, dcon::commodity_id settlement) {
	float total = 0.0f;
	if(!holder || !settlement) return total;
	state.world.economic_actor_for_each_obligation_creditor_as_economic_actor(holder,
		[&](dcon::obligation_creditor_id relation) {
			auto obligation = state.world.obligation_creditor_get_obligation(relation);
			if(obligation && state.world.obligation_get_kind(obligation) == uint8_t(obligation_kind::public_debt)
				&& state.world.obligation_get_settlement_commodity(obligation) == settlement
				&& live_obligation(state, obligation)) total += economy::relations::total_due(state, obligation);
		});
	return total;
}

fiscal_position fiscal_position_for(sys::state const& state, dcon::institution_id institution,
	dcon::commodity_id settlement) {
	fiscal_position result{};
	if(!institution || !settlement) return result;
	auto treasury = treasury_account_for(state, institution, settlement);
	if(treasury) result.treasury_cash = economy::accounts::balance(state, treasury);
	auto actor = governance::actor_for_institution(state, institution);
	if(!actor) return result;
	state.world.economic_actor_for_each_obligation_creditor_as_economic_actor(actor,
		[&](dcon::obligation_creditor_id relation) {
			auto obligation = state.world.obligation_creditor_get_obligation(relation);
			if(obligation && state.world.obligation_get_kind(obligation) == uint8_t(obligation_kind::tax)
				&& state.world.obligation_get_settlement_commodity(obligation) == settlement
				&& live_obligation(state, obligation)) result.tax_receivables += economy::relations::total_due(state, obligation);
		});
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(actor,
		[&](dcon::obligation_debtor_id relation) {
			auto obligation = state.world.obligation_debtor_get_obligation(relation);
			if(obligation && state.world.obligation_get_kind(obligation) == uint8_t(obligation_kind::public_debt)
				&& state.world.obligation_get_settlement_commodity(obligation) == settlement
				&& live_obligation(state, obligation)) result.public_debt_outstanding += economy::relations::total_due(state, obligation);
		});
	result.net_financial_position = result.treasury_cash + result.tax_receivables - result.public_debt_outstanding;
	return result;
}

float national_public_debt(sys::state const& state, dcon::nation_id nation, dcon::commodity_id settlement) {
	float total = 0.0f;
	for(auto institution : governance::institutions_of(state, nation))
		total += fiscal_position_for(state, institution, settlement).public_debt_outstanding;
	return total;
}

} // namespace governance::finance

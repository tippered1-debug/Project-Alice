#include "finance.hpp"

#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/consent/consent.hpp"
#include "economy/relations/relations.hpp"
#include "economy/wallets.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/offices.hpp"
#include "governance/public_administration.hpp"
#include "persons/exact_population.hpp"
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

struct authority_context {
	dcon::institution_id institution{};
	dcon::office_id office{};
	dcon::commodity_id settlement{};
};

bool authority_for_treasury(sys::state const& state, dcon::person_id initiator,
	authority_kind kind, dcon::monetary_account_id treasury_account, sys::date date,
	authority_context& result) {
	if(!initiator || !state.world.person_is_valid(initiator) || !persons::alive(state, initiator) || !treasury_account || !state.world.monetary_account_is_valid(treasury_account)) return false;
	result.institution = treasury_institution_for(state, treasury_account);
	if(!result.institution || !governance::nation_of(state, result.institution)) return false;
	// The person must exercise, on the date, an office holding the power over
	// the jurisdiction of the treasury's institution.
	auto exercised = governance::offices::exercising(state, initiator, kind, governance::jurisdiction_of(state, result.institution), date);
	if(!exercised) return false;
	result.office = exercised.office;
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
	if(initiator) state.world.force_create_fiscal_action_initiator(action, initiator);
	if(office) state.world.force_create_fiscal_action_authorizing_office(action, office);
	if(institution) state.world.force_create_fiscal_action_treasury_institution(action, institution);
	if(treasury_account) state.world.force_create_fiscal_action_treasury_account(action, treasury_account);
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
			// Construction subaccounts retain fiscal ownership/authority, but are
			// earmarked project cash, never the institution's spendable treasury.
			bool project_account = false;
			if(account) state.world.monetary_account_for_each_capital_project_account_as_monetary_account(account, [&](auto) { project_account = true; });
			if(account && !project_account
				&& economy::accounts::settlement_of(state, account) == settlement) result = account;
		});
	return result;
}

dcon::monetary_account_id open_treasury_account(sys::state& state, dcon::institution_id institution,
	dcon::commodity_id settlement) {
	if(!institution || !state.world.institution_is_valid(institution) || !settlement || !state.world.commodity_is_valid(settlement)) return {};
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
	if(!valid_amount(amount) || due_date < date || !taxpayer_actor || !state.world.economic_actor_is_valid(taxpayer_actor) || !authority_for_treasury(state, initiator, authority_kind::levy_tax, treasury_account, date, context)) return {};
	auto creditor = governance::actor_for_institution(state, context.institution);
	auto obligation = economy::relations::create_obligation(state, taxpayer_actor, creditor, amount,
		context.settlement, date, due_date, 0.0f, obligation_kind::tax);
	if(!obligation) return {};
	return record_action(state, fiscal_action_kind::tax_assessment, initiator, context.office,
		context.institution, treasury_account, date, obligation);
}

dcon::fiscal_action_id authorized_assess_tax_by_institution(sys::state& state,
	dcon::institution_id authority, dcon::economic_actor_id taxpayer_actor,
	dcon::monetary_account_id treasury_account, float amount,
	sys::date due_date, sys::date date) {
	if(!authority || !state.world.institution_is_valid(authority) || !valid_amount(amount) || due_date < date || !taxpayer_actor || !state.world.economic_actor_is_valid(taxpayer_actor) || !treasury_account || !state.world.monetary_account_is_valid(treasury_account) || treasury_institution_for(state, treasury_account) != authority) return {};
	auto settlement = economy::accounts::settlement_of(state, treasury_account);
	if(!governance::nation_of(state, authority) || !settlement || !governance::acts_administratively(state, authority,
		governance::authority_kind::levy_tax, governance::jurisdiction_of(state, authority), date)) return {};
	auto creditor = governance::actor_for_institution(state, authority);
	if(!creditor || creditor == taxpayer_actor || economy::accounts::owner_of(state, treasury_account) != creditor) return {};
	dcon::obligation_id rolling_obligation{};
	dcon::fiscal_action_id assessment_action{};
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(taxpayer_actor,
		[&](dcon::obligation_debtor_id relation) {
			auto candidate = state.world.obligation_debtor_get_obligation(relation);
			if(rolling_obligation || !candidate || state.world.obligation_get_kind(candidate) != uint8_t(obligation_kind::tax) || state.world.obligation_get_economic_actor_from_obligation_creditor(candidate) != creditor || state.world.obligation_get_settlement_commodity(candidate) != settlement) return;
			rolling_obligation = candidate;
		});
	if(rolling_obligation) {
		auto original_principal = state.world.obligation_get_original_principal(rolling_obligation) + amount;
		auto principal_outstanding = state.world.obligation_get_principal_outstanding(rolling_obligation) + amount;
		if(!std::isfinite(original_principal) || !std::isfinite(principal_outstanding)) return {};
		state.world.obligation_set_original_principal(rolling_obligation,
			original_principal);
		state.world.obligation_set_principal_outstanding(rolling_obligation, principal_outstanding);
		state.world.obligation_set_due_date(rolling_obligation, due_date);
		state.world.obligation_set_status(rolling_obligation, uint8_t(obligation_status::active));
		state.world.obligation_for_each_fiscal_action_resulting_obligation_as_obligation(rolling_obligation,
			[&](dcon::fiscal_action_resulting_obligation_id relation) {
				if(!assessment_action) assessment_action = state.world.fiscal_action_resulting_obligation_get_fiscal_action(relation);
			});
		if(assessment_action) {
			state.world.fiscal_action_set_occurred_on(assessment_action, date);
			return assessment_action;
		}
		return record_action(state, fiscal_action_kind::tax_assessment, {}, {}, authority,
			treasury_account, date, rolling_obligation);
	}
	auto obligation = economy::relations::create_obligation(state, taxpayer_actor, creditor, amount,
		settlement, date, due_date, 0.0f, obligation_kind::tax);
	if(!obligation) return {};
	return record_action(state, fiscal_action_kind::tax_assessment, {}, {}, authority,
		treasury_account, date, obligation);
}

dcon::transaction_id pay_tax(sys::state& state, dcon::obligation_id tax_obligation,
	dcon::monetary_account_id taxpayer_account, dcon::monetary_account_id treasury_account,
	float amount, sys::date date) {
	if(!valid_amount(amount) || !tax_obligation || !state.world.obligation_is_valid(tax_obligation) || state.world.obligation_get_kind(tax_obligation) != uint8_t(obligation_kind::tax) || !payable_obligation(state, tax_obligation)) return {};
	auto debtor = state.world.obligation_get_economic_actor_from_obligation_debtor(tax_obligation);
	auto creditor = state.world.obligation_get_economic_actor_from_obligation_creditor(tax_obligation);
	auto settlement = state.world.obligation_get_settlement_commodity(tax_obligation);
	auto institution = institution_for_actor(state, creditor);
	if(!institution || !valid_treasury_account(state, treasury_account, institution, settlement) || economy::accounts::owner_of(state, taxpayer_account) != debtor || economy::accounts::settlement_of(state, taxpayer_account) != settlement) return {};
	auto accepted = std::min(amount, economy::relations::total_due(state, tax_obligation));
	if(accepted <= 0.0f || economy::accounts::balance(state, taxpayer_account) < accepted) return {};
	auto transaction = economy::accounts::transfer(state, taxpayer_account, treasury_account, accepted,
		transaction_kind::tax_payment, date);
	if(!transaction || economy::relations::repay_obligation(state, tax_obligation, accepted) != accepted) return {};
	return transaction;
}

economy::exact_person_economy::transfer_result pay_tax(sys::state& state,
	dcon::obligation_id tax_obligation, economy::exact_person_economy::account_ref taxpayer_account,
	dcon::monetary_account_id treasury_account, float amount, sys::date date) {
	using economy::exact_person_economy::account_kind;
	using economy::exact_person_economy::account_ref;
	using economy::exact_person_economy::transfer_result;
	if(taxpayer_account.kind == account_kind::dcon) {
		auto transaction = pay_tax(state, tax_obligation, taxpayer_account.dcon_account,
			treasury_account, amount, date);
		return { bool(transaction), 0, transaction };
	}
	if(taxpayer_account.kind != account_kind::exact || !valid_amount(amount)
		|| !economy::exact_person_economy::account_exists(state, taxpayer_account)
		|| !tax_obligation || !state.world.obligation_is_valid(tax_obligation)
		|| state.world.obligation_get_kind(tax_obligation) != uint8_t(obligation_kind::tax)
		|| !payable_obligation(state, tax_obligation)) return {};
	auto debtor = state.world.obligation_get_economic_actor_from_obligation_debtor(tax_obligation);
	auto creditor = state.world.obligation_get_economic_actor_from_obligation_creditor(tax_obligation);
	auto settlement = state.world.obligation_get_settlement_commodity(tax_obligation);
	auto key = economy::exact_person_economy::owner_of(state, taxpayer_account);
	auto profile = persons::exact_population::profile_for_person(state, key);
	auto taxpayer_actor = profile ? persons::actor_for_person(state, profile) : dcon::economic_actor_id{};
	auto institution = institution_for_actor(state, creditor);
	if(!taxpayer_actor || taxpayer_actor != debtor || !institution
		|| !valid_treasury_account(state, treasury_account, institution, settlement)
		|| economy::exact_person_economy::settlement_of(state, taxpayer_account) != settlement) return {};
	auto accepted = std::min(amount, economy::relations::total_due(state, tax_obligation));
	if(accepted <= 0.0f || economy::exact_person_economy::balance(state, taxpayer_account) < accepted) return {};
	auto transfer = economy::exact_person_economy::transfer_with_result(state, taxpayer_account,
		account_ref::from_dcon(treasury_account), accepted, transaction_kind::tax_payment, date);
	if(!transfer.success || economy::relations::repay_obligation(state, tax_obligation, accepted) != accepted) return {};
	return transfer;
}

policy_tax_result assess_and_collect_topic_tax(sys::state& state, policy::topic_id topic,
	dcon::nation_id jurisdiction, dcon::economic_actor_id taxpayer_actor, float taxable_value,
	economy::exact_person_economy::account_ref payer_wallet, dcon::deposit_account_id payer_deposit,
	sys::date date) {
	policy_tax_result result{};
	if(!jurisdiction || !taxpayer_actor || !state.world.economic_actor_is_valid(taxpayer_actor)
		|| !std::isfinite(taxable_value) || taxable_value <= 0.0f) return result;
	auto policy_value = law::effective_topic(state, governance::national(jurisdiction), topic, date);
	auto rate = policy_value ? std::get_if<float>(&*policy_value) : nullptr;
	if(!rate || !std::isfinite(*rate) || *rate <= 0.0f) return result;
	result.rate = std::clamp(*rate, 0.0f, 1.0f);
	result.assessed = taxable_value * result.rate;
	if(!std::isfinite(result.assessed) || result.assessed <= 1.0e-6f) {
		result = {};
		return result;
	}
	dcon::commodity_id settlement{};
	if(payer_deposit) {
		if(!state.world.deposit_account_is_valid(payer_deposit)
			|| state.world.deposit_account_get_economic_actor_from_deposit_account_owner(payer_deposit) != taxpayer_actor) return {};
		settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(payer_deposit);
	} else if(payer_wallet.kind == economy::exact_person_economy::account_kind::dcon) {
		if(economy::accounts::owner_of(state, payer_wallet.dcon_account) != taxpayer_actor) return {};
		settlement = economy::accounts::settlement_of(state, payer_wallet.dcon_account);
	} else if(payer_wallet.kind == economy::exact_person_economy::account_kind::exact) {
		auto key = economy::exact_person_economy::owner_of(state, payer_wallet);
		auto profile = persons::exact_population::profile_for_person(state, key);
		if(!profile || persons::actor_for_person(state, profile) != taxpayer_actor) return {};
		settlement = economy::exact_person_economy::settlement_of(state, payer_wallet);
	} else {
		return {};
	}
	if(!settlement || !state.world.commodity_is_valid(settlement)) return {};
	auto authority = public_administration::tax_authority_for(state, jurisdiction);
	auto treasury = authority ? open_treasury_account(state, authority, settlement) : dcon::monetary_account_id{};
	if(!authority || !treasury) return {};
	result.assessment = authorized_assess_tax_by_institution(state, authority, taxpayer_actor,
		treasury, result.assessed, date, date);
	if(!result.assessment) {
		result = {};
		return result;
	}
	result.obligation = state.world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(result.assessment);
	if(!result.obligation) return result;
	if(payer_deposit) {
		if(!state.world.deposit_account_is_valid(payer_deposit)
			|| state.world.deposit_account_get_commodity_from_deposit_account_settlement(payer_deposit) != settlement) return result;
		auto due = std::min(economy::relations::total_due(state, result.obligation),
			economy::banking::deposit_balance(state, payer_deposit));
		auto bank = state.world.deposit_account_get_organization_from_deposit_account_bank(payer_deposit);
		auto reserve = economy::banking::reserve_account_for(state, bank, settlement);
		if(reserve) due = std::min(due, economy::accounts::balance(state, reserve));
		due = std::min(due, result.assessed);
		if(due > 1.0e-6f) {
			result.transaction = economy::banking::pay_from_deposit(state, payer_deposit, {},
				economy::exact_person_economy::account_ref::from_dcon(treasury), due,
				transaction_kind::tax_payment, date);
			if(result.transaction && economy::relations::repay_obligation(state, result.obligation, due) == due)
				result.collected = due;
		}
	} else if(payer_wallet) {
		auto due = std::min({result.assessed, economy::relations::total_due(state, result.obligation),
			economy::wallets::spendable(state, payer_wallet)});
		if(due > 1.0e-6f) {
			auto transfer = pay_tax(state, result.obligation, payer_wallet, treasury, due, date);
			if(transfer.success) {
				result.transaction = transfer.dcon_transaction_id;
				result.exact_transaction_id = transfer.exact_transaction_id;
				result.collected = due;
			}
		}
	}
	return result;
}

dcon::fiscal_action_id authorized_spend(sys::state& state, dcon::person_id initiator,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id recipient_account,
	float amount, sys::date date) {
	authority_context context{};
	if(!valid_amount(amount) || !authority_for_treasury(state, initiator, authority_kind::spend_public_funds,
		treasury_account, date, context) || !recipient_account || !state.world.monetary_account_is_valid(recipient_account) || economy::accounts::settlement_of(state, recipient_account) != context.settlement || economy::accounts::balance(state, treasury_account) < amount) return {};
	auto transaction = economy::accounts::transfer(state, treasury_account, recipient_account, amount,
		transaction_kind::public_spending, date);
	if(!transaction) return {};
	return record_action(state, fiscal_action_kind::public_spending, initiator, context.office,
		context.institution, treasury_account, date, {}, transaction);
}

dcon::fiscal_action_id authorized_spend_by_institution(sys::state& state,
	dcon::institution_id authority, dcon::monetary_account_id treasury_account,
	dcon::monetary_account_id recipient_account, float amount, sys::date date) {
	if(!authority || !state.world.institution_is_valid(authority) || !valid_amount(amount) || !treasury_account || !state.world.monetary_account_is_valid(treasury_account) || treasury_institution_for(state, treasury_account) != authority || !recipient_account || !state.world.monetary_account_is_valid(recipient_account)) return {};
	auto nation = governance::nation_of(state, authority);
	auto settlement = economy::accounts::settlement_of(state, treasury_account);
	auto recipient_institution = institution_for_actor(state, economy::accounts::owner_of(state, recipient_account));
	if(!nation || !settlement || !governance::acts_administratively(state, authority,
		governance::authority_kind::spend_public_funds, governance::jurisdiction_of(state, authority), date) || economy::accounts::settlement_of(state, recipient_account) != settlement || !recipient_institution || governance::nation_of(state, recipient_institution) != nation || treasury_institution_for(state, recipient_account) != recipient_institution || economy::accounts::balance(state, treasury_account) < amount) return {};
	auto transaction = economy::accounts::transfer(state, treasury_account, recipient_account,
		amount, transaction_kind::public_spending, date);
	if(!transaction) return {};
	return record_action(state, fiscal_action_kind::public_spending, {}, {}, authority,
		treasury_account, date, {}, transaction);
}

dcon::fiscal_action_id authorized_spend_exact_person_by_institution(sys::state& state,
	dcon::institution_id authority, dcon::monetary_account_id treasury_account,
	economy::exact_person_economy::person_key recipient,
	economy::exact_person_economy::account_ref recipient_account, float amount, sys::date date) {
	if(!authority || !state.world.institution_is_valid(authority) || !valid_amount(amount)
		|| recipient_account.kind != economy::exact_person_economy::account_kind::exact
		|| !economy::exact_person_economy::account_exists(state, recipient_account)
		|| economy::exact_person_economy::owner_of(state, recipient_account) != recipient
		|| !treasury_account || !state.world.monetary_account_is_valid(treasury_account)
		|| treasury_institution_for(state, treasury_account) != authority
		|| economy::accounts::settlement_of(state, treasury_account)
			!= economy::exact_person_economy::settlement_of(state, recipient_account)
		|| !governance::acts_administratively(state, authority,
			governance::authority_kind::spend_public_funds, governance::jurisdiction_of(state, authority), date)
		|| economy::accounts::balance(state, treasury_account) < amount) return {};
	auto transfer = economy::exact_person_economy::transfer_with_result(state,
		economy::exact_person_economy::account_ref::from_dcon(treasury_account), recipient_account,
		amount, transaction_kind::public_spending, date);
	if(!transfer.success) return {};
	auto action = record_action(state, fiscal_action_kind::public_spending, {}, {}, authority,
		treasury_account, date);
	if(action) state.world.fiscal_action_set_exact_transaction_id(action, transfer.exact_transaction_id);
	return action;
}

dcon::fiscal_action_id authorized_allocate(sys::state& state, dcon::institution_id allocator,
	dcon::institution_id recipient, dcon::commodity_id settlement, float amount, sys::date date) {
	if(!allocator || !recipient || allocator == recipient || !settlement || !valid_amount(amount)) return {};
	auto scope = governance::jurisdiction_of(state, allocator);
	if(!governance::acts_administratively(state, allocator, governance::authority_kind::appropriate, scope, date)
		|| !governance::acts_administratively(state, allocator, governance::authority_kind::spend_public_funds, scope, date)) return {};
	bool appropriated = false;
	for(auto const& entry : governance::law::appropriations(state, scope, date))
		if(entry.institution == recipient && entry.share > 0.0f) appropriated = true;
	if(!appropriated) return {};
	auto source = treasury_account_for(state, allocator, settlement);
	auto destination = treasury_account_for(state, recipient, settlement);
	if(!source || !destination) return {};
	return authorized_spend_by_institution(state, allocator, source, destination, amount, date);
}

namespace {
dcon::fiscal_action_id execute_public_debt_raw(sys::state& state, dcon::person_id initiator,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id investor_account,
	float principal, sys::date due_date, float annual_interest_rate, sys::date date) {
	authority_context context{};
	if(!valid_amount(principal) || due_date < date || !std::isfinite(annual_interest_rate) || annual_interest_rate < 0.0f || !authority_for_treasury(state, initiator, authority_kind::issue_public_debt, treasury_account, date, context) || !investor_account || !state.world.monetary_account_is_valid(investor_account) || economy::accounts::settlement_of(state, investor_account) != context.settlement || economy::accounts::balance(state, investor_account) < principal) return {};
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
	if(!investor_proposal || !state.world.economic_proposal_is_valid(investor_proposal) || state.world.economic_proposal_get_kind(investor_proposal) != uint8_t(economy::consent::proposal_kind::investment) || !investor_account || !state.world.monetary_account_is_valid(investor_account)) return {};
	authority_context context{};
	if(!authority_for_treasury(state, initiator, authority_kind::issue_public_debt, treasury_account, date, context)) return {};
	auto issuer = governance::actor_for_institution(state, context.institution);
	auto investor = economy::accounts::owner_of(state, investor_account);
	if(!issuer || !investor || state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_a(investor_proposal) != issuer || state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_b(investor_proposal) != investor || state.world.economic_proposal_get_settlement(investor_proposal) != context.settlement || state.world.economic_proposal_get_amount(investor_proposal) != principal || state.world.economic_proposal_get_due_date(investor_proposal) != due_date || state.world.economic_proposal_get_annual_interest_rate(investor_proposal) != annual_interest_rate || !economy::consent::proposal_fully_accepted(state, investor_proposal, date)) return {};
	// All consent and authority checks precede the internal atomic issuance primitive.
	auto action = execute_public_debt_raw(state, initiator, treasury_account, investor_account,
		principal, due_date, annual_interest_rate, date);
	if(!action || !economy::consent::mark_executed(state, investor_proposal)) return {};
	return action;
}

float accrue_public_debt_interest(sys::state& state, dcon::obligation_id obligation, uint32_t days) {
	if(!obligation || !state.world.obligation_is_valid(obligation) || state.world.obligation_get_kind(obligation) != uint8_t(obligation_kind::public_debt)) return 0.0f;
	return economy::relations::accrue_interest(state, obligation, days);
}

dcon::transaction_id service_public_debt(sys::state& state, dcon::obligation_id obligation,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id holder_account,
	float amount, sys::date date) {
	if(!valid_amount(amount) || !obligation || !state.world.obligation_is_valid(obligation) || state.world.obligation_get_kind(obligation) != uint8_t(obligation_kind::public_debt) || !payable_obligation(state, obligation)) return {};
	auto debtor = state.world.obligation_get_economic_actor_from_obligation_debtor(obligation);
	auto creditor = state.world.obligation_get_economic_actor_from_obligation_creditor(obligation);
	auto settlement = state.world.obligation_get_settlement_commodity(obligation);
	auto institution = institution_for_actor(state, debtor);
	if(!institution || !valid_treasury_account(state, treasury_account, institution, settlement) || economy::accounts::owner_of(state, holder_account) != creditor || economy::accounts::settlement_of(state, holder_account) != settlement) return {};
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
			if(obligation && state.world.obligation_get_kind(obligation) == uint8_t(obligation_kind::public_debt) && state.world.obligation_get_settlement_commodity(obligation) == settlement && live_obligation(state, obligation)) total += economy::relations::total_due(state, obligation);
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
			if(obligation && state.world.obligation_get_kind(obligation) == uint8_t(obligation_kind::tax) && state.world.obligation_get_settlement_commodity(obligation) == settlement && live_obligation(state, obligation)) result.tax_receivables += economy::relations::total_due(state, obligation);
		});
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(actor,
		[&](dcon::obligation_debtor_id relation) {
			auto obligation = state.world.obligation_debtor_get_obligation(relation);
			if(obligation && state.world.obligation_get_kind(obligation) == uint8_t(obligation_kind::public_debt) && state.world.obligation_get_settlement_commodity(obligation) == settlement && live_obligation(state, obligation)) result.public_debt_outstanding += economy::relations::total_due(state, obligation);
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

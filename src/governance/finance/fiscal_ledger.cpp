#include "fiscal_ledger.hpp"

#include "finance.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/money.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/policy.hpp"
#include "governance/public_administration.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace governance::finance {

struct fiscal_ledger_store {
	struct book {
		std::vector<fiscal_day> days; // closed days, oldest first
		std::vector<fiscal_day> months; // oldest first; the last one may still be running
		fiscal_day open{}; // the day being recorded
		bill_notice bill;
		debt_notice debt;
	};
	std::vector<book> books; // by nation index
	// The interface reads the books from its own thread while the game closes
	// days; whatever reshapes the vectors, and every reading of them, holds this.
	mutable std::mutex guard;
};

namespace {

using economy::relations::obligation_kind;
using economy::relations::obligation_status;
using economy::relations::transaction_kind;

bool ledger_usable(float amount) { return std::isfinite(amount) && amount > 0.0f; }
float ledger_finite_or_zero(float value) { return std::isfinite(value) ? value : 0.0f; }

fiscal_ledger_store& ledger_store_of(sys::state& state) {
	if(!state.fiscal_ledger) state.fiscal_ledger = std::make_shared<fiscal_ledger_store>();
	return *state.fiscal_ledger;
}

fiscal_ledger_store::book* ledger_book_of(sys::state& state, dcon::nation_id nation) {
	if(!nation) return nullptr;
	auto& store = ledger_store_of(state);
	auto const index = size_t(nation.index());
	if(store.books.size() <= index) {
		std::lock_guard lock{ store.guard };
		store.books.resize(index + 1);
	}
	auto& book = store.books[index];
	book.open.nation = int32_t(index);
	book.open.span = uint8_t(fiscal_span::open);
	return &book;
}

fiscal_ledger_store::book const* ledger_book_of(sys::state const& state, dcon::nation_id nation) {
	if(!nation || !state.fiscal_ledger) return nullptr;
	auto const index = size_t(nation.index());
	return index < state.fiscal_ledger->books.size() ? &state.fiscal_ledger->books[index] : nullptr;
}

dcon::institution_id ledger_institution_of(sys::state const& state, dcon::economic_actor_id actor) {
	return actor && state.world.economic_actor_is_valid(actor)
		? state.world.economic_actor_get_institution_from_institution_actor(actor) : dcon::institution_id{};
}

dcon::institution_id ledger_public_side(sys::state const& state, dcon::economic_actor_id actor) {
	auto institution = ledger_institution_of(state, actor);
	return institution && in_fiscal_perimeter(state, institution) ? institution : dcon::institution_id{};
}

bool ledger_is_person(sys::state const& state, dcon::economic_actor_id actor) {
	if(!actor) return true; // an exact person's own account
	auto kind = actors::ownership::actor_kind(state.world.economic_actor_get_kind(actor));
	return kind == actors::ownership::actor_kind::person || kind == actors::ownership::actor_kind::household;
}

void ledger_add_inflow(fiscal_day& day, transaction_kind kind, float amount) {
	switch(kind) {
	case transaction_kind::dividend:
		day.revenue[size_t(revenue_source::state_enterprises)] += amount;
		return;
	case transaction_kind::public_debt_issuance:
	case transaction_kind::loan_issuance:
		day.borrowed += amount;
		return;
	case transaction_kind::repayment:
	case transaction_kind::deposit_placement:
	case transaction_kind::withdrawal:
	case transaction_kind::currency_exchange:
	case transaction_kind::equity_contribution:
		day.financial_net += amount;
		return;
	default:
		day.revenue[size_t(revenue_source::other)] += amount;
		return;
	}
}

void ledger_add_outflow(sys::state const& state, fiscal_day& day, dcon::institution_id spender,
	dcon::economic_actor_id payee, transaction_kind kind, float amount) {
	switch(kind) {
	case transaction_kind::interest:
		day.spending[size_t(spending_function::debt_service)] += amount;
		return;
	case transaction_kind::repayment:
		day.principal_repaid += amount;
		return;
	case transaction_kind::loan_issuance:
	case transaction_kind::public_debt_issuance:
	case transaction_kind::deposit_placement:
	case transaction_kind::withdrawal:
	case transaction_kind::currency_exchange:
	case transaction_kind::equity_contribution:
	case transaction_kind::dividend:
		day.financial_net -= amount;
		return;
	case transaction_kind::public_spending:
		// Spending paid to a person outside payroll is a transfer: benefits, not services bought.
		if(ledger_is_person(state, payee) && !ledger_institution_of(state, payee)) {
			day.spending[size_t(spending_function::social_transfers)] += amount;
			return;
		}
		day.spending[size_t(function_of(state, spender))] += amount;
		return;
	default:
		day.spending[size_t(function_of(state, spender))] += amount;
		return;
	}
}

void ledger_add_flows(fiscal_day& into, fiscal_day const& from) {
	for(size_t i = 0; i < revenue_source_count; ++i) into.revenue[i] += from.revenue[i];
	for(size_t i = 0; i < spending_function_count; ++i) into.spending[i] += from.spending[i];
	into.tax_assessed += from.tax_assessed;
	into.borrowed += from.borrowed;
	into.principal_repaid += from.principal_repaid;
	into.financial_net += from.financial_net;
}

void ledger_reset_flows(fiscal_day& day) {
	auto nation = day.nation;
	day = fiscal_day{};
	day.nation = nation;
	day.span = uint8_t(fiscal_span::open);
}

sys::date ledger_date_of(int32_t raw) {
	return raw > 0 ? sys::date{ sys::date::value_base_t(raw - 1) } : sys::date{};
}

int32_t ledger_month_key(sys::state const& state, sys::date date) {
	auto ymd = date.to_ymd(state.start_date);
	ymd.day = 1;
	return sys::date{ ymd, state.start_date }.to_raw_value();
}

float ledger_score_between(float value, float good, float poor) {
	if(!std::isfinite(value)) return 0.0f;
	if(good == poor) return value <= good ? 1.0f : 0.0f;
	auto t = (value - good) / (poor - good);
	return std::clamp(1.0f - t, 0.0f, 1.0f);
}

std::vector<public_debt_line> ledger_debt_lines(sys::state const& state, dcon::nation_id nation) {
	std::vector<public_debt_line> lines;
	for(auto institution : governance::institutions_of(state, nation)) {
		if(!in_fiscal_perimeter(state, institution)) continue;
		auto actor = governance::actor_for_institution(state, institution);
		if(!actor) continue;
		state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(actor, [&](dcon::obligation_debtor_id relation) {
			auto obligation = state.world.obligation_debtor_get_obligation(relation);
			if(!obligation || state.world.obligation_get_kind(obligation) != uint8_t(obligation_kind::public_debt)
				|| state.world.obligation_get_settlement_commodity(obligation) != economy::money) return;
			auto status = state.world.obligation_get_status(obligation);
			if(status == uint8_t(obligation_status::paid) || status == uint8_t(obligation_status::written_off)) return;
			if(economy::relations::total_due(state, obligation) <= 1.0e-6f) return;
			public_debt_line line;
			line.obligation = obligation;
			line.debtor = institution;
			line.holder = state.world.obligation_get_economic_actor_from_obligation_creditor(obligation);
			line.principal = ledger_finite_or_zero(state.world.obligation_get_principal_outstanding(obligation));
			line.interest_due = ledger_finite_or_zero(state.world.obligation_get_accrued_interest(obligation));
			line.annual_rate = ledger_finite_or_zero(state.world.obligation_get_annual_interest_rate(obligation));
			line.issued = state.world.obligation_get_creation_date(obligation);
			line.due = state.world.obligation_get_due_date(obligation);
			line.defaulted = status == uint8_t(obligation_status::defaulted);
			line.overdue = !line.defaulted && line.due && line.due <= state.current_date;
			lines.push_back(line);
		});
	}
	std::sort(lines.begin(), lines.end(), [](public_debt_line const& a, public_debt_line const& b) {
		if(a.due != b.due) return a.due < b.due;
		return a.obligation.index() < b.obligation.index();
	});
	return lines;
}

} // namespace

float revenue_total(fiscal_day const& day) {
	float total = 0.0f;
	for(auto value : day.revenue) total += value;
	return total;
}

float spending_total(fiscal_day const& day) {
	float total = 0.0f;
	for(auto value : day.spending) total += value;
	return total;
}

bool in_fiscal_perimeter(sys::state const& state, dcon::institution_id institution) {
	return institution && state.world.institution_is_valid(institution)
		&& governance::nation_of(state, institution)
		&& governance::kind_of(state, institution) != governance::institution_kind::household_sector;
}

spending_function function_of(sys::state const& state, dcon::institution_id institution) {
	using governance::institution_kind;
	using governance::service_kind;
	auto kind = governance::kind_of(state, institution);
	auto service = service_kind(state.world.institution_get_service(institution));
	if(service == service_kind::defense || kind == institution_kind::military_command)
		return spending_function::military;
	if(service == service_kind::education || kind == institution_kind::education_ministry)
		return spending_function::education;
	if(service == service_kind::policing || service == service_kind::intelligence
		|| service == service_kind::counterintelligence || service == service_kind::justice
		|| kind == institution_kind::interior_ministry || kind == institution_kind::court
		|| kind == institution_kind::prosecutor)
		return spending_function::public_order;
	if(service == service_kind::construction || kind == institution_kind::public_works_ministry)
		return spending_function::infrastructure;
	return spending_function::administration;
}

revenue_source source_of_topic(policy::topic_id topic) {
	switch(topic) {
	case policy::topic_id::income_tax: return revenue_source::income_tax;
	case policy::topic_id::dividend_tax: return revenue_source::dividend_tax;
	case policy::topic_id::resource_royalty: return revenue_source::resource_royalties;
	case policy::topic_id::import_tariff:
	case policy::topic_id::export_tariff: return revenue_source::customs_duties;
	default: return revenue_source::other;
	}
}

void observe_transfer(sys::state& state, dcon::economic_actor_id payer, dcon::economic_actor_id payee,
	float amount, dcon::commodity_id settlement, transaction_kind kind) {
	if(!ledger_usable(amount) || settlement != economy::money) return;
	// Taxes and debt service are posted by the fiscal API, which knows the tax and the split.
	if(kind == transaction_kind::tax_payment || kind == transaction_kind::public_debt_service) return;
	auto from = ledger_public_side(state, payer);
	auto to = ledger_public_side(state, payee);
	if(!from && !to) return;
	auto from_nation = from ? governance::nation_of(state, from) : dcon::nation_id{};
	auto to_nation = to ? governance::nation_of(state, to) : dcon::nation_id{};
	if(from && to && from_nation == to_nation) return; // within one state
	if(to_nation)
		if(auto* book = ledger_book_of(state, to_nation)) ledger_add_inflow(book->open, kind, amount);
	if(from_nation)
		if(auto* book = ledger_book_of(state, from_nation)) ledger_add_outflow(state, book->open, from, payee, kind, amount);
}

void post_tax_collected(sys::state& state, dcon::institution_id creditor, dcon::economic_actor_id payer,
	revenue_source source, float amount, dcon::commodity_id settlement) {
	if(!ledger_usable(amount) || settlement != economy::money || !in_fiscal_perimeter(state, creditor)) return;
	auto nation = governance::nation_of(state, creditor);
	if(auto paying = ledger_public_side(state, payer); paying && governance::nation_of(state, paying) == nation) return;
	if(auto* book = ledger_book_of(state, nation)) book->open.revenue[size_t(source)] += amount;
}

void post_tax_assessed(sys::state& state, dcon::institution_id creditor, dcon::economic_actor_id taxpayer,
	float amount, dcon::commodity_id settlement) {
	if(!ledger_usable(amount) || settlement != economy::money || !in_fiscal_perimeter(state, creditor)) return;
	auto nation = governance::nation_of(state, creditor);
	if(auto paying = ledger_public_side(state, taxpayer); paying && governance::nation_of(state, paying) == nation) return;
	if(auto* book = ledger_book_of(state, nation)) book->open.tax_assessed += amount;
}

void post_debt_service(sys::state& state, dcon::institution_id debtor, float interest, float principal,
	dcon::commodity_id settlement) {
	if(settlement != economy::money || !in_fiscal_perimeter(state, debtor)) return;
	auto* book = ledger_book_of(state, governance::nation_of(state, debtor));
	if(!book) return;
	if(ledger_usable(interest)) book->open.spending[size_t(spending_function::debt_service)] += interest;
	if(ledger_usable(principal)) book->open.principal_repaid += principal;
}

void close_fiscal_day(sys::state& state, bool presimulation) {
	auto& store = ledger_store_of(state);
	// size the books first, so that the loop below never resizes them under the lock
	state.world.for_each_nation([&](dcon::nation_id nation) {
		if(state.world.nation_get_owned_province_count(nation) > 0) (void)ledger_book_of(state, nation);
	});
	std::lock_guard lock{ store.guard };
	auto today = state.current_date;
	auto key = ledger_month_key(state, today);
	state.world.for_each_nation([&](dcon::nation_id nation) {
		auto const index = size_t(nation.index());
		// A country with no land and no books has nothing to record; one with books keeps them.
		auto const known = index < store.books.size();
		if(!known && state.world.nation_get_owned_province_count(nation) == 0) return;
		auto* book = &store.books[index];
		if(presimulation) {
			ledger_reset_flows(book->open);
			return;
		}
		auto day = book->open;
		day.day = today.to_raw_value();
		day.span = uint8_t(fiscal_span::day);
		day.treasury = governance::public_administration::treasury_cash(state, nation);
		auto ministry = governance::find_institution(state, nation, governance::institution_kind::finance_ministry);
		day.ministry = ministry ? economy::accounts::balance(state, treasury_account_for(state, ministry, economy::money)) : 0.0f;
		day.debt = national_public_debt(state, nation, economy::money);
		// A day closed twice (a second update on one date) replaces nothing: its flows join the record.
		if(!book->days.empty() && book->days.back().day == day.day) {
			ledger_add_flows(book->days.back(), day);
			book->days.back().treasury = day.treasury;
			book->days.back().ministry = day.ministry;
			book->days.back().debt = day.debt;
		} else {
			book->days.push_back(day);
			if(book->days.size() > kept_days) book->days.erase(book->days.begin(), book->days.begin() + (book->days.size() - kept_days));
		}
		if(book->months.empty() || book->months.back().day != key) {
			fiscal_day month{};
			month.nation = int32_t(index);
			month.day = key;
			month.span = uint8_t(fiscal_span::month);
			book->months.push_back(month);
			if(book->months.size() > kept_months) book->months.erase(book->months.begin(), book->months.begin() + (book->months.size() - kept_months));
		}
		auto& month = book->months.back();
		ledger_add_flows(month, day);
		month.treasury = day.treasury;
		month.ministry = day.ministry;
		month.debt = day.debt;
		ledger_reset_flows(book->open);
	});
}

std::vector<fiscal_day> export_ledger(sys::state const& state) {
	std::vector<fiscal_day> result;
	if(!state.fiscal_ledger) return result;
	std::lock_guard lock{ state.fiscal_ledger->guard };
	for(auto const& book : state.fiscal_ledger->books) {
		result.insert(result.end(), book.days.begin(), book.days.end());
		result.insert(result.end(), book.months.begin(), book.months.end());
		auto open = book.open;
		if(revenue_total(open) != 0.0f || spending_total(open) != 0.0f || open.tax_assessed != 0.0f
			|| open.borrowed != 0.0f || open.principal_repaid != 0.0f || open.financial_net != 0.0f)
			result.push_back(open);
	}
	return result;
}

bool import_ledger(sys::state& state, std::vector<fiscal_day> const& records) {
	clear_ledger(state);
	auto& store = ledger_store_of(state);
	auto const nations = size_t(state.world.nation_size());
	for(auto record : records) {
		if(record.nation < 0 || size_t(record.nation) >= nations) return false;
		for(auto& value : record.revenue) value = ledger_finite_or_zero(value);
		for(auto& value : record.spending) value = ledger_finite_or_zero(value);
		if(store.books.size() <= size_t(record.nation)) store.books.resize(size_t(record.nation) + 1);
		auto& book = store.books[size_t(record.nation)];
		switch(fiscal_span(record.span)) {
		case fiscal_span::day:
			if(!book.days.empty() && book.days.back().day >= record.day) return false;
			book.days.push_back(record);
			break;
		case fiscal_span::month:
			if(!book.months.empty() && book.months.back().day >= record.day) return false;
			book.months.push_back(record);
			break;
		case fiscal_span::open:
			book.open = record;
			break;
		default:
			return false;
		}
	}
	for(size_t i = 0; i < store.books.size(); ++i) {
		store.books[i].open.nation = int32_t(i);
		store.books[i].open.span = uint8_t(fiscal_span::open);
	}
	return true;
}

void clear_ledger(sys::state& state) {
	state.fiscal_ledger.reset();
}

bool step_allowed(fiscal_period period, fiscal_step step) {
	switch(period) {
	case fiscal_period::days_30: return step != fiscal_step::monthly;
	case fiscal_period::days_90: return true;
	case fiscal_period::months_12: return step != fiscal_step::daily;
	}
	return false;
}

fiscal_step default_step(fiscal_period period) {
	return period == fiscal_period::months_12 ? fiscal_step::monthly : fiscal_step::daily;
}

credit_grade grade_of(float score, bool defaulted) {
	if(defaulted) return credit_grade::d;
	constexpr float bounds[] = { 0.95f, 0.85f, 0.75f, 0.62f, 0.50f, 0.38f, 0.25f, 0.10f };
	for(size_t i = 0; i < std::size(bounds); ++i)
		if(score >= bounds[i]) return credit_grade(uint8_t(i));
	return credit_grade::d;
}

credit_assessment assess_credit(sys::state const& state, dcon::nation_id nation) {
	credit_assessment result;
	std::unique_lock<std::mutex> lock;
	if(state.fiscal_ledger) lock = std::unique_lock{ state.fiscal_ledger->guard };
	result.debt_to_revenue.good = 0.6f;
	result.debt_to_revenue.poor = 3.0f;
	result.interest_to_revenue.good = 0.05f;
	result.interest_to_revenue.poor = 0.30f;
	result.liquidity.good = 1.0f;
	result.liquidity.poor = 0.0f;
	result.deficit.good = 0.0f;
	result.deficit.poor = -0.5f;
	result.record.good = 1.0f;
	result.record.poor = 0.0f;
	auto const* book = ledger_book_of(state, nation);
	if(!book || book->days.size() < size_t(credit_minimum_days)) return result;
	auto const recorded = std::min(book->days.size(), size_t(365));
	float revenue = 0.0f, spending = 0.0f;
	for(size_t i = book->days.size() - recorded; i < book->days.size(); ++i) {
		revenue += revenue_total(book->days[i]);
		spending += spending_total(book->days[i]);
	}
	if(!(revenue > 0.0f)) return result;
	result.annual_revenue = revenue * 365.0f / float(recorded);
	auto lines = ledger_debt_lines(state, nation);
	float debt = 0.0f, annual_interest = 0.0f, due_soon = 0.0f;
	bool defaulted = false, overdue = false;
	auto const horizon = state.current_date + credit_liquidity_days;
	for(auto const& line : lines) {
		debt += line.principal + line.interest_due;
		annual_interest += line.principal * line.annual_rate;
		if(line.due && line.due <= horizon) due_soon += line.principal + line.interest_due;
		defaulted = defaulted || line.defaulted;
		overdue = overdue || line.overdue;
	}
	auto treasury = governance::public_administration::treasury_cash(state, nation);
	result.debt_to_revenue.value = debt / result.annual_revenue;
	result.debt_to_revenue.score = ledger_score_between(result.debt_to_revenue.value, result.debt_to_revenue.good, result.debt_to_revenue.poor);
	result.interest_to_revenue.value = annual_interest / result.annual_revenue;
	result.interest_to_revenue.score = ledger_score_between(result.interest_to_revenue.value, result.interest_to_revenue.good, result.interest_to_revenue.poor);
	result.liquidity.value = due_soon > 0.0f ? treasury / due_soon : 1.0f;
	result.liquidity.score = std::clamp(result.liquidity.value, 0.0f, 1.0f);
	result.deficit.value = (revenue - spending) / revenue;
	result.deficit.score = ledger_score_between(-result.deficit.value, -result.deficit.good, -result.deficit.poor);
	result.record.value = defaulted ? 0.0f : (overdue ? 0.4f : 1.0f);
	result.record.score = result.record.value;
	result.score = std::min({ result.debt_to_revenue.score, result.interest_to_revenue.score,
		result.liquidity.score, result.deficit.score, result.record.score });
	result.grade = grade_of(result.score, defaulted);
	result.assessed = true;
	return result;
}

fiscal_report report(sys::state const& state, dcon::nation_id nation, fiscal_period period, fiscal_step step) {
	fiscal_report r;
	r.nation = nation;
	r.period = period;
	r.step = step_allowed(period, step) ? step : default_step(period);
	r.span_days = period == fiscal_period::days_30 ? 30 : (period == fiscal_period::days_90 ? 90 : 365);
	if(!nation) return r;

	// ---- what is there now ----
	r.treasury = governance::public_administration::treasury_cash(state, nation);
	if(auto ministry = governance::find_institution(state, nation, governance::institution_kind::finance_ministry))
		r.ministry = economy::accounts::balance(state, treasury_account_for(state, ministry, economy::money));
	for(auto institution : governance::institutions_of(state, nation)) {
		if(!in_fiscal_perimeter(state, institution)) continue;
		auto position = fiscal_position_for(state, institution, economy::money);
		r.tax_receivables += position.tax_receivables;
		r.public_debt += position.public_debt_outstanding;
		r.debt_principal += position.public_debt_principal_outstanding;
		r.debt_accrued_interest += position.public_debt_accrued_interest;
		r.debt_overdue += position.public_debt_overdue;
		r.debt_defaulted += position.public_debt_defaulted;
	}
	r.obligations = ledger_debt_lines(state, nation);
	for(auto const& line : r.obligations) {
		r.annual_interest += line.principal * line.annual_rate;
		if(line.due && (!r.next_payment || line.due < r.next_payment)) r.next_payment = line.due;
	}
	for(auto const& line : r.obligations)
		if(line.due == r.next_payment) r.next_payment_amount += line.principal + line.interest_due;
	r.average_rate = r.debt_principal > 0.0f ? r.annual_interest / r.debt_principal : 0.0f;
	auto debt_policy = governance::law::public_debt_policy_for(state, nation, economy::money, state.current_date);
	r.issuance_allowed = debt_policy.issuance_allowed;
	r.debt_ceiling = debt_policy.ceiling;
	r.credit = assess_credit(state, nation);

	policy::position law;
	if(policy::current(state, nation, state.current_date, law)) {
		r.law_in_force = true;
		auto number = [&](policy::topic_id topic) {
			auto value = policy::value_or(law, topic);
			auto v = std::get_if<float>(&value);
			return v ? *v : 0.0f;
		};
		r.base_rate = number(policy::topic_id::income_tax);
		r.progressivity = number(policy::topic_id::progressivity);
		for(uint8_t bucket = 0; bucket < 3; ++bucket)
			r.stratum_rates[bucket] = governance::law::effective_amount(state, governance::national(nation),
				governance::law::policy_rule_kind::income_tax_rate, state.current_date, bucket).value_or(0.0f);
		std::array<float, 4> raw{ number(policy::topic_id::education_appropriation), number(policy::topic_id::policing_appropriation),
			number(policy::topic_id::public_works_appropriation), number(policy::topic_id::local_government_appropriation) };
		float sum = 0.0f;
		for(auto v : raw) sum += std::max(0.0f, v);
		for(size_t i = 0; i < raw.size(); ++i) r.priorities[i] = sum > 0.0f ? std::max(0.0f, raw[i]) / sum : 0.0f;
	}
	r.next_bill = state.world.nation_get_next_policy_bill_date(nation);

	// ---- the books ----
	std::unique_lock<std::mutex> lock;
	if(state.fiscal_ledger) lock = std::unique_lock{ state.fiscal_ledger->guard };
	auto const* book = ledger_book_of(state, nation);
	if(!book || book->days.empty()) return r;
	auto const& days = book->days;
	r.first_recorded = days.front().day;
	auto const last = days.back().day;
	r.period_last = last;
	r.period_first = last - r.span_days + 1;

	auto find_day = [&](int32_t raw) -> fiscal_day const* {
		auto it = std::lower_bound(days.begin(), days.end(), raw, [](fiscal_day const& d, int32_t v) { return d.day < v; });
		return it != days.end() && it->day == raw ? &*it : nullptr;
	};

	if(period == fiscal_period::months_12) {
		// The year is its last twelve months, the running one included.
		auto const& months = book->months;
		auto const count = std::min<size_t>(months.size(), 12);
		auto const begin = months.size() - count;
		for(size_t i = begin; i < months.size(); ++i) {
			auto const& m = months[i];
			for(size_t k = 0; k < revenue_source_count; ++k) r.revenue[k] += m.revenue[k];
			for(size_t k = 0; k < spending_function_count; ++k) r.spending[k] += m.spending[k];
			r.tax_assessed += m.tax_assessed;
			r.borrowed += m.borrowed;
			r.principal_repaid += m.principal_repaid;
			r.financial_net += m.financial_net;
		}
		if(count > 0) r.period_first = std::max(months[begin].day, r.first_recorded);
		r.recorded_days = 0;
		for(auto const& d : days) if(d.day >= r.period_first) ++r.recorded_days;
		if(months.size() >= 24) {
			r.previous_known = true;
			for(size_t i = months.size() - 24; i < months.size() - 12; ++i) {
				r.previous_revenue += revenue_total(months[i]);
				r.previous_spending += spending_total(months[i]);
			}
		}
	} else {
		for(auto const& d : days) {
			if(d.day < r.period_first) continue;
			++r.recorded_days;
			for(size_t k = 0; k < revenue_source_count; ++k) r.revenue[k] += d.revenue[k];
			for(size_t k = 0; k < spending_function_count; ++k) r.spending[k] += d.spending[k];
			r.tax_assessed += d.tax_assessed;
			r.borrowed += d.borrowed;
			r.principal_repaid += d.principal_repaid;
			r.financial_net += d.financial_net;
		}
		auto const previous_first = r.period_first - r.span_days;
		if(r.first_recorded <= previous_first) {
			r.previous_known = true;
			for(auto const& d : days) {
				if(d.day < previous_first || d.day >= r.period_first) continue;
				r.previous_revenue += revenue_total(d);
				r.previous_spending += spending_total(d);
			}
		}
	}
	for(auto v : r.revenue) r.revenue_total += v;
	for(auto v : r.spending) r.spending_total += v;
	r.balance = r.revenue_total - r.spending_total;

	// The change of cash a day: from the close before the period (or its first close) to the last.
	{
		fiscal_day const* before = nullptr;
		fiscal_day const* first_in = nullptr;
		for(auto const& d : days) {
			if(d.day < r.period_first) before = &d;
			else if(!first_in) first_in = &d;
		}
		auto const* opening = before ? before : first_in;
		r.treasury_opening = opening ? opening->treasury : days.back().treasury;
		auto const elapsed = opening ? float(last - opening->day) : 0.0f;
		r.treasury_per_day = elapsed > 0.0f ? (days.back().treasury - r.treasury_opening) / elapsed : 0.0f;
	}

	// ---- the buckets of the chart and of the sparklines ----
	auto add_day_bucket = [&](int32_t first, int32_t last_day) {
		fiscal_bucket b;
		b.first_day = first;
		b.last_day = last_day;
		for(int32_t d = first; d <= last_day; ++d) {
			if(auto const* day = find_day(d)) {
				b.recorded = true;
				b.revenue += revenue_total(*day);
				b.spending += spending_total(*day);
				b.treasury = day->treasury;
				b.debt = day->debt;
			}
		}
		b.balance = b.revenue - b.spending;
		r.buckets.push_back(b);
	};
	if(r.step == fiscal_step::monthly) {
		auto const& months = book->months;
		auto const wanted = period == fiscal_period::months_12 ? size_t(12) : size_t(3);
		// Months before the books opened are shown empty, so the year keeps its shape.
		std::vector<int32_t> keys;
		auto cursor = ledger_date_of(months.empty() ? last : months.back().day);
		for(size_t i = 0; i < wanted; ++i) {
			keys.push_back(ledger_month_key(state, cursor));
			cursor = ledger_date_of(keys.back()) - 1;
		}
		std::reverse(keys.begin(), keys.end());
		for(size_t i = 0; i < keys.size(); ++i) {
			fiscal_bucket b;
			b.first_day = keys[i];
			b.last_day = i + 1 < keys.size() ? keys[i + 1] - 1 : last;
			for(auto const& m : months) {
				if(m.day != keys[i]) continue;
				b.recorded = true;
				b.revenue = revenue_total(m);
				b.spending = spending_total(m);
				b.treasury = m.treasury;
				b.debt = m.debt;
			}
			b.balance = b.revenue - b.spending;
			r.buckets.push_back(b);
		}
	} else {
		auto const length = r.step == fiscal_step::weekly ? 7 : 1;
		auto const count = (r.span_days + length - 1) / length;
		for(int32_t i = count - 1; i >= 0; --i) {
			auto const bucket_last = last - i * length;
			add_day_bucket(std::max(bucket_last - length + 1, r.period_first), bucket_last);
		}
	}
	return r;
}

void note_bill(sys::state& state, dcon::nation_id nation, bill_notice const& notice) {
	if(auto* book = ledger_book_of(state, nation)) book->bill = notice;
}

bill_notice last_bill(sys::state const& state, dcon::nation_id nation) {
	auto const* book = ledger_book_of(state, nation);
	return book ? book->bill : bill_notice{};
}

void note_debt(sys::state& state, dcon::nation_id nation, debt_notice const& notice) {
	if(auto* book = ledger_book_of(state, nation)) book->debt = notice;
}

debt_notice last_debt(sys::state const& state, dcon::nation_id nation) {
	auto const* book = ledger_book_of(state, nation);
	return book ? book->debt : debt_notice{};
}

} // namespace governance::finance

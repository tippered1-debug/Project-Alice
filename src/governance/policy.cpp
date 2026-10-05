#include "policy.hpp"

#include "system_state.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace governance::policy {
namespace {

constexpr float tolerance = 1.0e-4f;
constexpr topic_definition registry[] = {
	{ topic_id::income_tax, "income_tax", value_kind::continuous, implementation::statute, jurisdiction_kind::national, 0.0f, 0.4f, 0, 0.0f },
	{ topic_id::progressivity, "progressivity", value_kind::continuous, implementation::statute, jurisdiction_kind::national, 0.0f, 1.0f, 0, 0.5f },
	{ topic_id::education_appropriation, "education_appropriation", value_kind::continuous, implementation::appropriation, jurisdiction_kind::national, 0.0f, 1.0f, 0, 0.35f },
	{ topic_id::policing_appropriation, "policing_appropriation", value_kind::continuous, implementation::appropriation, jurisdiction_kind::national, 0.0f, 1.0f, 0, 0.25f },
	{ topic_id::public_works_appropriation, "public_works_appropriation", value_kind::continuous, implementation::appropriation, jurisdiction_kind::national, 0.0f, 1.0f, 0, 0.3f },
	{ topic_id::local_government_appropriation, "local_government_appropriation", value_kind::continuous, implementation::appropriation, jurisdiction_kind::national, 0.0f, 1.0f, 0, 0.3f },
	{ topic_id::minimum_wage, "minimum_wage", value_kind::continuous, implementation::statute, jurisdiction_kind::national, 0.0f, 100.0f, 0, 0.0f },
	{ topic_id::labor_protection, "labor_protection", value_kind::ordinal, implementation::statute, jurisdiction_kind::national, 0.0f, 3.0f, 0, int32_t(1) },
	{ topic_id::collective_bargaining, "collective_bargaining", value_kind::categorical, implementation::statute, jurisdiction_kind::national, 0.0f, 0.0f, 4, category_value{1} },
	{ topic_id::unemployment_replacement, "unemployment_replacement", value_kind::continuous, implementation::statute, jurisdiction_kind::national, 0.0f, 0.8f, 0, 0.0f },
	{ topic_id::unemployment_duration, "unemployment_duration", value_kind::ordinal, implementation::statute, jurisdiction_kind::national, 0.0f, 365.0f, 0, int32_t(0) },
	{ topic_id::dividend_tax, "dividend_tax", value_kind::continuous, implementation::statute, jurisdiction_kind::national, 0.0f, 0.6f, 0, 0.0f },
	{ topic_id::resource_royalty, "resource_royalty", value_kind::continuous, implementation::statute, jurisdiction_kind::national, 0.0f, 0.8f, 0, 0.0f },
	{ topic_id::bank_reserve_requirement, "bank_reserve_requirement", value_kind::continuous, implementation::statute, jurisdiction_kind::national, 0.0f, 0.5f, 0, 0.0f }
};

float numeric(policy_value const& value) {
	if(auto v = std::get_if<float>(&value)) return *v;
	if(auto v = std::get_if<int32_t>(&value)) return float(*v);
	if(auto v = std::get_if<bool>(&value)) return *v ? 1.0f : 0.0f;
	if(auto v = std::get_if<category_value>(&value)) return float(v->value);
	return 0.0f;
}

} // namespace

std::vector<topic_definition> const& topics() {
	static const std::vector<topic_definition> result(std::begin(registry), std::end(registry));
	return result;
}

topic_definition const* definition(topic_id id) {
	auto index = uint16_t(id);
	return index < std::size(registry) && uint16_t(registry[index].id) == index ? &registry[index] : nullptr;
}

bool valid(topic_id topic, policy_value const& value) {
	auto spec = definition(topic);
	if(!spec) return false;
	switch(spec->kind) {
	case value_kind::continuous: {
		auto number = std::get_if<float>(&value);
		return number && std::isfinite(*number) && *number >= spec->minimum && *number <= spec->maximum;
	}
	case value_kind::ordinal: {
		auto number = std::get_if<int32_t>(&value);
		return number && float(*number) >= spec->minimum && float(*number) <= spec->maximum;
	}
	case value_kind::categorical: {
		auto category = std::get_if<category_value>(&value);
		return category && category->value < spec->category_count;
	}
	case value_kind::binary: return std::holds_alternative<bool>(value);
	case value_kind::structured: {
		auto parameters = std::get_if<structured_value>(&value);
		return parameters && std::isfinite(parameters->first) && std::isfinite(parameters->second);
	}
	}
	return false;
}

std::optional<policy_value> get(position const& source, topic_id topic) {
	auto found = std::lower_bound(source.entries.begin(), source.entries.end(), topic,
		[](entry const& item, topic_id id) { return uint16_t(item.topic) < uint16_t(id); });
	if(found == source.entries.end() || found->topic != topic) return std::nullopt;
	return found->value;
}

policy_value value_or(position const& source, topic_id topic) {
	if(auto value = get(source, topic)) return *value;
	if(auto spec = definition(topic)) return spec->default_value;
	return 0.0f;
}

bool set(position& target, topic_id topic, policy_value const& value) {
	if(!valid(topic, value)) return false;
	auto found = std::lower_bound(target.entries.begin(), target.entries.end(), topic,
		[](entry const& item, topic_id id) { return uint16_t(item.topic) < uint16_t(id); });
	if(found != target.entries.end() && found->topic == topic) found->value = value;
	else target.entries.insert(found, { topic, value });
	return true;
}

position clamp(position value) {
	for(auto& item : value.entries) {
		auto spec = definition(item.topic);
		if(!spec) continue;
		if(spec->kind == value_kind::continuous) {
			auto number = numeric(item.value);
			item.value = std::clamp(std::isfinite(number) ? number : numeric(spec->default_value), spec->minimum, spec->maximum);
		} else if(spec->kind == value_kind::ordinal) {
			auto number = int32_t(numeric(item.value));
			item.value = std::clamp(number, int32_t(spec->minimum), int32_t(spec->maximum));
		} else if(!valid(item.topic, item.value)) item.value = spec->default_value;
	}
	value.entries.erase(std::remove_if(value.entries.begin(), value.entries.end(), [](entry const& item) {
		return definition(item.topic) == nullptr;
	}), value.entries.end());
	return value;
}

float distance(position const& a, position const& b, salience const& weights) {
	double sum = 0.0;
	uint32_t count = 0;
	for(auto const& spec : topics()) {
		auto av = get(a, spec.id), bv = get(b, spec.id);
		if(!av && !bv) continue;
		float weight = 1.0f;
		if(!weights.empty()) {
			auto found = std::find_if(weights.begin(), weights.end(), [&](issue_salience const& item) { return item.topic == spec.id; });
			weight = found == weights.end() ? 0.0f : std::max(0.0f, found->weight);
		}
		if(!(weight > 0.0f)) continue;
		auto left = av.value_or(spec.default_value), right = bv.value_or(spec.default_value);
		double delta = 0.0;
		if(spec.kind == value_kind::categorical || spec.kind == value_kind::binary) delta = left == right ? 0.0 : 1.0;
		else if(spec.kind == value_kind::structured) {
			auto x = std::get<structured_value>(left), y = std::get<structured_value>(right);
			auto d1 = double(x.first) - y.first, d2 = double(x.second) - y.second;
			delta = (d1 * d1 + d2 * d2) / 2.0;
		} else {
			auto range = double(spec.maximum - spec.minimum);
			delta = range > 0.0 ? std::pow((double(numeric(left)) - numeric(right)) / range, 2.0) : 0.0;
		}
		sum += delta * weight;
		if(weights.empty()) ++count;
	}
	if(weights.empty()) return count ? float(sum / count) : 0.0f;
	double total_weight = 0.0;
	for(auto const& item : weights) total_weight += std::max(0.0f, item.weight);
	return total_weight > 0.0 ? float(sum / total_weight) : 0.0f;
}

float distance(position const& a, position const& b) {
	return distance(a, b, {});
}

position weighted_mean(std::vector<std::pair<position const*, float>> const& values) {
	position result;
	for(auto const& spec : topics()) {
		double total = 0.0, first = 0.0, second = 0.0;
		std::map<uint16_t, double> categories;
		for(auto const& [source, weight] : values) {
			if(!source || !(weight > 0.0f)) continue;
			auto raw = get(*source, spec.id);
			if(!raw) continue;
			total += weight;
			if(spec.kind == value_kind::structured) {
				auto structured = std::get<structured_value>(*raw);
				first += weight * structured.first;
				second += weight * structured.second;
			} else if(spec.kind == value_kind::categorical) {
				categories[std::get<category_value>(*raw).value] += weight;
			} else first += weight * numeric(*raw);
		}
		if(total <= 0.0) continue;
		if(spec.kind == value_kind::categorical) {
			uint16_t best = 0;
			double best_weight = -1.0;
			for(auto const& [category, weight] : categories)
				if(weight > best_weight) { best = category; best_weight = weight; }
			(void)set(result, spec.id, category_value{best});
		} else if(spec.kind == value_kind::binary) {
			(void)set(result, spec.id, first / total >= 0.5);
		} else if(spec.kind == value_kind::ordinal) {
			(void)set(result, spec.id, int32_t(std::lround(first / total)));
		} else if(spec.kind == value_kind::structured) {
			(void)set(result, spec.id, structured_value{float(first / total), float(second / total)});
		} else (void)set(result, spec.id, float(first / total));
	}
	return clamp(std::move(result));
}

position move_toward(position const& from, position const& to, float fraction) {
	fraction = std::clamp(std::isfinite(fraction) ? fraction : 0.0f, 0.0f, 1.0f);
	position result = from;
	for(auto const& spec : topics()) {
		auto target = get(to, spec.id);
		if(!target) continue;
		auto original = get(from, spec.id).value_or(spec.default_value);
		if(spec.kind == value_kind::continuous) {
			auto a = numeric(original), b = numeric(*target);
			(void)set(result, spec.id, a + fraction * (b - a));
		} else if(spec.kind == value_kind::ordinal) {
			auto a = numeric(original), b = numeric(*target);
			(void)set(result, spec.id, int32_t(std::lround(a + fraction * (b - a))));
		} else if(spec.kind == value_kind::structured) {
			auto a = std::get<structured_value>(original), b = std::get<structured_value>(*target);
			(void)set(result, spec.id, structured_value{a.first + fraction * (b.first - a.first), a.second + fraction * (b.second - a.second)});
		} else if(fraction >= 0.5f) (void)set(result, spec.id, *target);
	}
	return clamp(std::move(result));
}

fiscal_rules rules_for(sys::state const& state, dcon::nation_id nation, position const& raw) {
	auto value = clamp(raw);
	fiscal_rules result{};
	auto base = numeric(value_or(value, topic_id::income_tax));
	auto spread = numeric(value_or(value, topic_id::progressivity));
	result.tax_rates[0] = std::clamp(base * (1.0f - 0.6f * spread), 0.0f, 1.0f);
	result.tax_rates[1] = std::clamp(base, 0.0f, 1.0f);
	result.tax_rates[2] = std::clamp(base * (1.0f + spread), 0.0f, 1.0f);
	std::vector<dcon::institution_id> territorial;
	for(auto institution : institutions_of(state, nation)) {
		if(territory_of(state, institution)) {
			if(state.world.institution_get_staffing_per_capita(institution) > 0.0f) territorial.push_back(institution);
			continue;
		}
		auto service = service_kind(state.world.institution_get_service(institution));
		float share = -1.0f;
		if(service == service_kind::education) share = numeric(value_or(value, topic_id::education_appropriation));
		else if(service == service_kind::policing) share = numeric(value_or(value, topic_id::policing_appropriation));
		else if(service == service_kind::construction) share = numeric(value_or(value, topic_id::public_works_appropriation));
		if(share > 0.0f) result.shares.push_back({ institution, share });
	}
	auto local = numeric(value_or(value, topic_id::local_government_appropriation));
	if(local > 0.0f && !territorial.empty())
		for(auto institution : territorial) result.shares.push_back({ institution, local / float(territorial.size()) });
	return result;
}

bool current(sys::state const& state, dcon::nation_id nation, sys::date date, position& result) {
	auto scope = national(nation);
	auto middle = law::effective_amount(state, scope, law::policy_rule_kind::income_tax_rate, date, 1);
	auto rich = law::effective_amount(state, scope, law::policy_rule_kind::income_tax_rate, date, 2);
	if(!middle || !rich) return false;
	result = {};
	(void)set(result, topic_id::income_tax, *middle);
	(void)set(result, topic_id::progressivity, *middle > tolerance ? *rich / *middle - 1.0f : 0.0f);
	for(auto const& entry : law::appropriations(state, scope, date)) {
		if(territory_of(state, entry.institution)) {
			(void)set(result, topic_id::local_government_appropriation,
				numeric(value_or(result, topic_id::local_government_appropriation)) + entry.share);
			continue;
		}
		auto service = service_kind(state.world.institution_get_service(entry.institution));
		if(service == service_kind::education) (void)set(result, topic_id::education_appropriation, entry.share);
		else if(service == service_kind::policing) (void)set(result, topic_id::policing_appropriation, entry.share);
		else if(service == service_kind::construction) (void)set(result, topic_id::public_works_appropriation, entry.share);
	}
	for(auto const& spec : topics())
		if(spec.id != topic_id::income_tax && spec.id != topic_id::progressivity
			&& spec.id != topic_id::education_appropriation && spec.id != topic_id::policing_appropriation
			&& spec.id != topic_id::public_works_appropriation && spec.id != topic_id::local_government_appropriation)
			if(auto value = law::effective_topic(state, scope, spec.id, date)) (void)set(result, spec.id, *value);
	return true;
}

bool law_matches(sys::state const& state, dcon::nation_id nation, position const& value, sys::date date) {
	auto wanted = rules_for(state, nation, value);
	auto scope = national(nation);
	for(uint8_t bucket = 0; bucket < 3; ++bucket) {
		auto rate = law::effective_amount(state, scope, law::policy_rule_kind::income_tax_rate, date, bucket);
		if(!rate || std::abs(*rate - wanted.tax_rates[bucket]) > tolerance) return false;
	}
	auto disbursement = law::effective_amount(state, scope, law::policy_rule_kind::disbursement_rate, date);
	if(!disbursement || std::abs(*disbursement - disbursement_rate) > tolerance) return false;
	auto shares = law::appropriations(state, scope, date);
	if(shares.size() != wanted.shares.size()) return false;
	for(auto const& [institution, share] : wanted.shares) {
		bool found = false;
		for(auto const& entry : shares)
			if(entry.institution == institution && std::abs(entry.share - share) <= tolerance) found = true;
		if(!found) return false;
	}
	for(auto const& item : value.entries) {
		auto spec = definition(item.topic);
		if(!spec || spec->enacted_by == implementation::appropriation) continue;
		auto actual = law::effective_topic(state, scope, item.topic, date);
		if(!actual || *actual != item.value) return false;
	}
	return true;
}

} // namespace governance::policy

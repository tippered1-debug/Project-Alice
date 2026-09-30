#include "technology_kernel.hpp"

#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/relations/relations.hpp"
#include "economy/demographics.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <tuple>

namespace technology {

struct technology_kernel_store {
	technology::kernel::snapshot data;
};

} // namespace technology

namespace technology::kernel {
namespace {

constexpr float epsilon = 1.0e-6f;

std::shared_ptr<technology_kernel_store> ensure_store(sys::state& state) {
	assert(state.technology_kernel && "technology kernel store must be initialized before simulation");
	if(!state.technology_kernel) std::abort();
	return state.technology_kernel;
}

std::shared_ptr<technology_kernel_store> ensure_store(sys::state const& state) {
	assert(state.technology_kernel && "technology kernel store must be initialized before lookup");
	if(!state.technology_kernel) std::abort();
	return state.technology_kernel;
}

template<typename T>
T const* by_id(std::vector<T> const& values, stable_id id) {
	auto it = std::lower_bound(values.begin(), values.end(), id,
		[](T const& value, stable_id key) { return value.id < key; });
	return it != values.end() && it->id == id ? &*it : nullptr;
}

research_organization const* research_org_by_id(snapshot const& data, stable_id id) {
	return by_id(data.organizations, id);
}

dcon::organization_id organization_by_stable_id(sys::state const& state, stable_id id) {
	dcon::organization_id result{};
	if(id == 0) return result;
	state.world.for_each_organization([&](dcon::organization_id organization) {
		if(!result && state.world.organization_get_canonical_id(organization) == id)
			result = organization;
	});
	return result;
}

stable_id stable_organization_id(sys::state const& state, dcon::organization_id organization) {
	return organization && state.world.organization_is_valid(organization)
		? state.world.organization_get_canonical_id(organization) : 0;
}

capability_definition const* capability_by_id(snapshot const& data, stable_id id) {
	return by_id(data.capabilities, id);
}

capability_holder const* holder(snapshot const& data, stable_id organization, stable_id capability) {
	auto it = std::lower_bound(data.holders.begin(), data.holders.end(),
		std::pair{organization, capability}, [](capability_holder const& value, auto key) {
			return std::pair{value.organization, value.capability} < key;
		});
	return it != data.holders.end() && it->organization == organization && it->capability == capability ? &*it : nullptr;
}

bool prerequisites_met(snapshot const& data, stable_id organization, stable_id capability) {
	for(auto const& prerequisite : data.prerequisites) {
		if(prerequisite.capability != capability) continue;
		auto known = holder(data, organization, prerequisite.prerequisite);
		if(!known || known->maturity < 0.5f) return false;
	}
	return true;
}

bool ready_holder(snapshot const& data, stable_id organization, stable_id capability) {
	auto knowledge = holder(data, organization, capability);
	return knowledge && knowledge->maturity >= 0.5f;
}

bool add_holder(snapshot& data, capability_holder value) {
	if(!value.organization || !value.capability || holder(data, value.organization, value.capability)) return false;
	data.holders.push_back(value);
	std::sort(data.holders.begin(), data.holders.end(), [](auto const& left, auto const& right) {
		return std::tie(left.organization, left.capability) < std::tie(right.organization, right.capability);
	});
	return true;
}

bool active_assignment(sys::state const& state, research_assignment const& assignment) {
	return persons::exists(state, assignment.person) && persons::alive(state, assignment.person)
		&& economy::exact_person_economy::is_work_eligible(state, assignment.person);
}

float effective_person_productivity(sys::state const& state, research_assignment const& assignment) {
	if(assignment.productivity_input > 0.0f)
		return std::clamp(assignment.productivity_input, 0.0f, 1.0f);
	auto population = persons::current_population(state, assignment.person);
	if(!population || !state.world.pop_is_valid(population)) return 0.1f;
	auto literacy = std::clamp(pop_demographics::get_literacy(state, population), 0.0f, 1.0f);
	return 0.1f + 0.9f * literacy;
}

float available_funding(sys::state const& state, dcon::monetary_account_id account) {
	if(!account || !state.world.monetary_account_is_valid(account)) return 0.0f;
	return std::max(0.0f, economy::accounts::balance(state, account)
		- economy::physical::concrete_market::reserved_bid_amount(state, account));
}

uint64_t hash_word(uint64_t hash, uint64_t value) noexcept {
	for(int shift = 0; shift < 64; shift += 8) {
		hash ^= uint8_t(value >> shift);
		hash *= 1099511628211ULL;
	}
	return hash;
}

uint64_t hash_float(uint64_t hash, float value) noexcept {
	return hash_word(hash, std::bit_cast<uint32_t>(value));
}

bool finite_fraction(float value) {
	return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

} // namespace

stable_id stable_id_for(std::string_view namespace_key, std::string_view authored_id) noexcept {
	uint64_t hash = 14695981039346656037ULL;
	for(unsigned char c : namespace_key) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	hash ^= uint8_t(':');
	hash *= 1099511628211ULL;
	for(unsigned char c : authored_id) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	return hash == 0 ? 1 : hash;
}

bool canonical_runtime_active(sys::state const& state) {
	return state.technology_kernel && state.technology_kernel->data.canonical_runtime_active != 0;
}

bool install_scenario_state(sys::state& state, snapshot const& value) {
	if(!state.technology_kernel) state.technology_kernel = std::make_shared<technology_kernel_store>();
	return import_snapshot(state, value);
}

bool add_initial_holder(sys::state& state, capability_holder const& value) {
	if(!value.organization || !value.capability || !finite_fraction(value.maturity)) return false;
	auto data = ensure_store(state)->data;
	if(!organization_by_stable_id(state, value.organization) || !capability_by_id(data, value.capability)) return false;
	if(!add_holder(data, value)) return false;
	ensure_store(state)->data = std::move(data);
	return true;
}

bool transfer_capability(sys::state& state, stable_id source_organization,
	stable_id destination_organization, stable_id capability, sys::date date,
	stable_id authorization, stable_id transfer_id) {
	auto data = ensure_store(state)->data;
	auto definition = capability_by_id(data, capability);
	if(!definition || definition->transferable == 0 || !authorization
		|| (state.current_date && date > state.current_date)
		|| !organization_by_stable_id(state, source_organization)
		|| !organization_by_stable_id(state, destination_organization)
		|| !holder(data, source_organization, capability)
		|| holder(data, destination_organization, capability)) return false;
	if(transfer_id == 0) {
		transfer_id = 14695981039346656037ULL;
		transfer_id = hash_word(transfer_id, source_organization);
		transfer_id = hash_word(transfer_id, destination_organization);
		transfer_id = hash_word(transfer_id, capability);
		transfer_id = hash_word(transfer_id, uint32_t(date.to_raw_value()));
		transfer_id = hash_word(transfer_id, authorization);
		if(transfer_id == 0) transfer_id = 1;
	}
	if(std::any_of(data.transfers.begin(), data.transfers.end(), [&](auto const& transfer) { return transfer.id == transfer_id; })) return false;
	auto maturity = holder(data, source_organization, capability)->maturity;
	if(!add_holder(data, {destination_organization, capability, maturity})) return false;
	data.transfers.push_back({transfer_id, source_organization, destination_organization, capability, date, authorization});
	std::sort(data.transfers.begin(), data.transfers.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
	ensure_store(state)->data = std::move(data);
	return true;
}

bool adopt_capability(sys::state& state, stable_id organization, stable_id capability, sys::date date) {
	auto data = ensure_store(state)->data;
	if((state.current_date && date > state.current_date)
		|| !organization_by_stable_id(state, organization) || !capability_by_id(data, capability)
		|| !ready_holder(data, organization, capability) || !prerequisites_met(data, organization, capability)) return false;
	if(std::any_of(data.adoptions.begin(), data.adoptions.end(), [&](auto const& adoption) {
		return adoption.organization == organization && adoption.capability == capability;
	})) return false;
	data.adoptions.push_back({organization, capability, date});
	std::sort(data.adoptions.begin(), data.adoptions.end(), [](auto const& left, auto const& right) {
		return std::tie(left.organization, left.capability) < std::tie(right.organization, right.capability);
	});
	ensure_store(state)->data = std::move(data);
	return true;
}

bool organization_holds(sys::state const& state, stable_id organization, stable_id capability) {
	return holder(ensure_store(state)->data, organization, capability) != nullptr;
}

bool organization_adopted(sys::state const& state, stable_id organization, stable_id capability) {
	auto const& values = ensure_store(state)->data.adoptions;
	auto it = std::lower_bound(values.begin(), values.end(), std::pair{organization, capability},
		[](auto const& value, auto key) { return std::pair{value.organization, value.capability} < key; });
	return it != values.end() && it->organization == organization && it->capability == capability;
}

bool organization_can_use_capability(sys::state const& state, stable_id organization, stable_id capability) {
	if(!state.technology_kernel) return false;
	auto const& data = state.technology_kernel->data;
	return ready_holder(data, organization, capability)
		&& organization_adopted(state, organization, capability)
		&& prerequisites_met(data, organization, capability);
}

bool organization_can_operate_factory_type(sys::state const& state,
	dcon::organization_id organization, dcon::factory_type_id factory_type) {
	if(!organization || !state.world.organization_is_valid(organization) || !factory_type
		|| !state.world.factory_type_is_valid(factory_type)) return false;
	auto const& data = ensure_store(state)->data;
	auto organization_id = stable_organization_id(state, organization);
	for(auto const& requirement : data.factory_processes) {
		if(requirement.factory_type != factory_type) continue;
		if(!organization_can_use_capability(state, organization_id, requirement.capability)) return false;
	}
	return true;
}

bool factory_process_has_canonical_requirement(sys::state const& state, dcon::factory_type_id factory_type) {
	if(!state.technology_kernel || !factory_type) return false;
	return std::any_of(state.technology_kernel->data.factory_processes.begin(),
		state.technology_kernel->data.factory_processes.end(), [&](auto const& value) {
			return value.factory_type == factory_type;
		});
}

void update_daily(sys::state& state) {
	auto store = ensure_store(state);
	auto& data = store->data;
	if(data.canonical_runtime_active == 0) return;

	// Death closes the assignment relation before validation and research advance.
	data.assignments.erase(std::remove_if(data.assignments.begin(), data.assignments.end(),
		[&](auto const& assignment) { return !persons::exists(state, assignment.person) || !persons::alive(state, assignment.person); }),
		data.assignments.end());

	std::vector<research_program*> ordered;
	ordered.reserve(data.programs.size());
	for(auto& program : data.programs) ordered.push_back(&program);
	std::sort(ordered.begin(), ordered.end(), [](auto left, auto right) { return left->id < right->id; });

	// Programs authored as planned are selected deterministically per research
	// organization. The legacy nation research selector is not consulted.
	for(auto const& organization : data.organizations) {
		bool has_active = std::any_of(ordered.begin(), ordered.end(), [&](auto const* program) {
			return program->organization == organization.id && program->status == program_status::active;
		});
		if(has_active) continue;
		for(auto* program : ordered) {
			if(program->organization != organization.id || program->status != program_status::planned
				|| (program->start_date && state.current_date && program->start_date > state.current_date)
				|| !prerequisites_met(data, stable_organization_id(state, organization.organization), program->capability)) continue;
			program->status = program_status::active;
			break;
		}
	}

	std::vector<research_assignment const*> assignments;
	for(auto* program : ordered) {
		if(program->status != program_status::active
			|| (program->start_date && state.current_date && program->start_date > state.current_date)) continue;
		auto research_org = research_org_by_id(data, program->organization);
		auto definition = capability_by_id(data, program->capability);
		if(!research_org || !definition || !research_org->site || !state.world.site_is_valid(research_org->site)
			|| !prerequisites_met(data, stable_organization_id(state, research_org->organization), program->capability)) continue;
		assignments.clear();
		for(auto const& assignment : data.assignments)
			if(assignment.program == program->id && active_assignment(state, assignment)) assignments.push_back(&assignment);
		std::sort(assignments.begin(), assignments.end(), [](auto left, auto right) {
			return std::tie(left->person.source_population_cell, left->person.ordinal)
				< std::tie(right->person.source_population_cell, right->person.ordinal);
		});
		if(assignments.empty()) continue;

		float raw_effort = 0.0f;
		for(auto assignment : assignments)
			raw_effort += assignment->allocation_fraction * effective_person_productivity(state, *assignment);
		if(!std::isfinite(raw_effort) || raw_effort <= epsilon) continue;
		auto candidate_effort = std::min(raw_effort * research_org->effectiveness,
			std::max(0.0f, program->required_effort - program->effort_accumulated));
		if(candidate_effort <= epsilon || !std::isfinite(program->cost_per_effort)
			|| program->cost_per_effort <= 0.0f) continue;
		auto funding = available_funding(state, research_org->funding_account);
		auto affordable_effort = funding / program->cost_per_effort;
		candidate_effort = std::min(candidate_effort, affordable_effort);
		if(candidate_effort <= epsilon) continue;
		float total_cost = candidate_effort * program->cost_per_effort;
		auto settlement = economy::accounts::settlement_of(state, research_org->funding_account);
		if(!settlement || !state.world.commodity_is_valid(settlement)) continue;

		float paid = 0.0f;
		float total_weight = 0.0f;
		for(auto assignment : assignments)
			total_weight += assignment->allocation_fraction * effective_person_productivity(state, *assignment);
		for(auto assignment : assignments) {
			auto weight = assignment->allocation_fraction * effective_person_productivity(state, *assignment);
			if(weight <= epsilon || total_weight <= epsilon) continue;
			auto amount = total_cost * (weight / total_weight);
			auto destination = economy::exact_person_economy::open_account(state, assignment->person, settlement);
			if(!destination) continue;
			if(economy::exact_person_economy::transfer(state,
				economy::exact_person_economy::account_ref::from_dcon(research_org->funding_account),
				destination, amount, economy::relations::transaction_kind::payroll, state.current_date))
				paid += amount;
		}
		if(paid <= epsilon) continue;
		program->effort_accumulated = std::min(program->required_effort,
			program->effort_accumulated + paid / program->cost_per_effort);
		if(program->effort_accumulated + epsilon >= program->required_effort) {
			program->effort_accumulated = program->required_effort;
			program->status = program_status::completed;
			auto holder_org = stable_organization_id(state, research_org->organization);
			auto existing = std::find_if(data.holders.begin(), data.holders.end(), [&](auto const& value) {
				return value.organization == holder_org && value.capability == program->capability;
			});
			if(existing != data.holders.end()) existing->maturity = 1.0f;
			else (void)add_holder(data, {holder_org, program->capability, 1.0f});
		}
	}
}

void initialize_empty_store(sys::state& state) {
	assert(!state.technology_kernel && "technology kernel store initialized more than once");
	if(state.technology_kernel) std::abort();
	state.technology_kernel = std::make_shared<technology_kernel_store>();
}

void clear_store(sys::state& state) {
	state.technology_kernel.reset();
}

snapshot export_snapshot(sys::state const& state) {
	if(!state.technology_kernel) return snapshot{};
	auto result = ensure_store(state)->data;
	std::sort(result.capabilities.begin(), result.capabilities.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
	std::sort(result.prerequisites.begin(), result.prerequisites.end(), [](auto const& left, auto const& right) {
		return std::tie(left.capability, left.prerequisite) < std::tie(right.capability, right.prerequisite);
	});
	std::sort(result.factory_processes.begin(), result.factory_processes.end(), [](auto const& left, auto const& right) {
		return std::tie(left.process, left.capability) < std::tie(right.process, right.capability);
	});
	std::sort(result.organizations.begin(), result.organizations.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
	std::sort(result.holders.begin(), result.holders.end(), [](auto const& left, auto const& right) {
		return std::tie(left.organization, left.capability) < std::tie(right.organization, right.capability);
	});
	std::sort(result.programs.begin(), result.programs.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
	std::sort(result.assignments.begin(), result.assignments.end(), [](auto const& left, auto const& right) {
		return std::tie(left.program, left.person.source_population_cell, left.person.ordinal)
			< std::tie(right.program, right.person.source_population_cell, right.person.ordinal);
	});
	std::sort(result.adoptions.begin(), result.adoptions.end(), [](auto const& left, auto const& right) {
		return std::tie(left.organization, left.capability) < std::tie(right.organization, right.capability);
	});
	std::sort(result.transfers.begin(), result.transfers.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
	return result;
}

bool import_snapshot(sys::state& state, snapshot const& value) {
	if(value.version != 1 || value.canonical_runtime_active > 1) return false;
	auto candidate = std::make_shared<technology_kernel_store>();
	candidate->data = value;
	state.technology_kernel = std::move(candidate);
	state.technology_kernel->data = export_snapshot(state);
	std::vector<std::string> errors;
	if(!validate_canonical_technology_state(state, errors)) {
		state.technology_kernel.reset();
		return false;
	}
	state.technology_kernel->data = export_snapshot(state);
	return true;
}

void load_snapshot_unvalidated(sys::state& state, snapshot const& value) {
	auto candidate = std::make_shared<technology_kernel_store>();
	candidate->data = value;
	state.technology_kernel = std::move(candidate);
	state.technology_kernel->data = export_snapshot(state);
}

bool validate_canonical_technology_state(sys::state const& state, std::vector<std::string>& errors) {
	if(!state.technology_kernel) {
		errors.push_back("technology kernel store is missing");
		return false;
	}
	auto const& data = state.technology_kernel->data;
	auto initial_count = errors.size();
	if(data.version != 1 || data.canonical_runtime_active > 1)
		errors.push_back("technology kernel schema or activation state is invalid");
	std::vector<stable_id> ids;
	for(auto const& capability : data.capabilities) {
		if(!capability.id || !capability.domain || !std::isfinite(capability.research_effort) || capability.research_effort <= 0.0f
			|| capability.codified > 1 || capability.transferable > 1)
			errors.push_back("capability definition has invalid stable ID, domain, research effort, or codification state");
		ids.push_back(capability.id);
	}
	std::sort(ids.begin(), ids.end());
	if(std::adjacent_find(ids.begin(), ids.end()) != ids.end()) errors.push_back("duplicate capability stable ID");
	for(auto const& prerequisite : data.prerequisites)
		if(!capability_by_id(data, prerequisite.capability) || !capability_by_id(data, prerequisite.prerequisite)
			|| prerequisite.capability == prerequisite.prerequisite)
			errors.push_back("capability prerequisite is missing or self-referential");
	for(std::size_t i = 0; i < data.prerequisites.size(); ++i)
		for(std::size_t j = i + 1; j < data.prerequisites.size(); ++j)
			if(data.prerequisites[i].capability == data.prerequisites[j].capability
				&& data.prerequisites[i].prerequisite == data.prerequisites[j].prerequisite)
				errors.push_back("duplicate capability prerequisite relation");
	for(auto const& process : data.factory_processes)
		if(!capability_by_id(data, process.capability) || !process.process || !process.factory_type
			|| !state.world.factory_type_is_valid(process.factory_type))
			errors.push_back("canonical factory process refers to a missing capability or factory type");
	for(std::size_t i = 0; i < data.factory_processes.size(); ++i)
		for(std::size_t j = i + 1; j < data.factory_processes.size(); ++j)
			if(data.factory_processes[i].process == data.factory_processes[j].process) {
				if(data.factory_processes[i].factory_type != data.factory_processes[j].factory_type)
					errors.push_back("factory process stable ID maps to multiple factory types");
				if(data.factory_processes[i].capability == data.factory_processes[j].capability)
					errors.push_back("duplicate capability factory process relation");
			}

	ids.clear();
	for(auto const& organization : data.organizations) {
		if(!organization.id || !organization.organization || !state.world.organization_is_valid(organization.organization)
			|| !state.world.organization_get_canonical_id(organization.organization)
			|| !organization.site || !state.world.site_is_valid(organization.site)
			|| !state.world.site_get_canonical_id(organization.site)
			|| !organization.funding_account || !state.world.monetary_account_is_valid(organization.funding_account)
			|| !state.world.monetary_account_get_canonical_id(organization.funding_account)
			|| economy::accounts::owner_of(state, organization.funding_account)
				!= actors::organizations::actor_for_organization(state, organization.organization)
			|| !std::isfinite(economy::accounts::balance(state, organization.funding_account))
			|| economy::accounts::balance(state, organization.funding_account) < 0.0f
			|| !std::isfinite(organization.effectiveness) || organization.effectiveness < 0.0f || organization.effectiveness > 1.0f
			|| uint8_t(organization.role) > uint8_t(research_role::other))
			errors.push_back("research organization has invalid organization, site, funding account, role, or effectiveness");
		ids.push_back(organization.id);
	}
	std::sort(ids.begin(), ids.end());
	if(std::adjacent_find(ids.begin(), ids.end()) != ids.end()) errors.push_back("duplicate research organization stable ID");
	ids.clear();
	for(auto const& organization : data.organizations)
		if(organization.organization && state.world.organization_is_valid(organization.organization))
			ids.push_back(state.world.organization_get_canonical_id(organization.organization));
	std::sort(ids.begin(), ids.end());
	if(std::adjacent_find(ids.begin(), ids.end()) != ids.end()) errors.push_back("duplicate organization stable ID in research organizations");
	for(auto const& left : data.organizations) for(auto const& right : data.organizations)
		if(&left != &right && left.organization == right.organization)
			errors.push_back("one organization is assigned more than one research organization record");
	for(auto const& organization : data.organizations)
		if(std::none_of(data.programs.begin(), data.programs.end(), [&](auto const& program) { return program.organization == organization.id; }))
			errors.push_back("research organization has no research program");

	for(auto const& value : data.holders) {
		if(!organization_by_stable_id(state, value.organization) || !capability_by_id(data, value.capability)
			|| !finite_fraction(value.maturity)) errors.push_back("capability holder refers to an invalid organization, capability, or maturity");
	}
	for(std::size_t i = 0; i < data.holders.size(); ++i)
		for(std::size_t j = i + 1; j < data.holders.size(); ++j)
			if(data.holders[i].organization == data.holders[j].organization
				&& data.holders[i].capability == data.holders[j].capability)
				errors.push_back("duplicate capability holder relation");

	ids.clear();
	for(auto const& program : data.programs) {
		auto organization = research_org_by_id(data, program.organization);
		auto definition = capability_by_id(data, program.capability);
		if(!program.id || !organization || !definition || !std::isfinite(program.effort_accumulated)
			|| program.effort_accumulated < 0.0f || !std::isfinite(program.required_effort)
			|| program.required_effort <= 0.0f || program.effort_accumulated > program.required_effort + epsilon
			|| !std::isfinite(program.cost_per_effort) || program.cost_per_effort <= 0.0f
			|| uint8_t(program.status) > uint8_t(program_status::cancelled))
			errors.push_back("research program has invalid stable ID, organization, target, status, effort, or funding cost");
		if(definition && std::abs(program.required_effort - definition->research_effort) > epsilon)
			errors.push_back("research program required effort does not match its target capability definition");
		if(program.effort_accumulated + epsilon >= program.required_effort
			&& program.status != program_status::completed)
			errors.push_back("research program at required effort is not marked completed");
		if(program.status == program_status::completed
			&& (program.effort_accumulated + epsilon < program.required_effort
				|| !organization || !holder(data, organization ? stable_organization_id(state, organization->organization) : 0, program.capability)))
			errors.push_back("completed research program is missing its completed effort or organization holder relation");
		ids.push_back(program.id);
	}
	std::sort(ids.begin(), ids.end());
	if(std::adjacent_find(ids.begin(), ids.end()) != ids.end()) errors.push_back("duplicate research program stable ID");
	for(auto const& assignment : data.assignments) {
		if(!by_id(data.programs, assignment.program) || !persons::exists(state, assignment.person)
			|| !persons::alive(state, assignment.person) || !std::isfinite(assignment.allocation_fraction)
			|| assignment.allocation_fraction <= 0.0f || assignment.allocation_fraction > 1.0f
			|| !finite_fraction(assignment.productivity_input))
			errors.push_back("research assignment has invalid program, living exact person, allocation, or productivity input");
	}
	for(std::size_t i = 0; i < data.assignments.size(); ++i) for(std::size_t j = i + 1; j < data.assignments.size(); ++j) {
		auto const& left = data.assignments[i];
		auto const& right = data.assignments[j];
		if(left.program == right.program && left.person == right.person)
			errors.push_back("duplicate exact-person research assignment within a program");
		if(left.person == right.person && left.program != right.program
			&& left.allocation_fraction + right.allocation_fraction > 1.0f + epsilon)
			errors.push_back("exact person research allocations exceed full-time capacity");
	}
	for(std::size_t i = 0; i < data.assignments.size(); ++i) {
		float total = data.assignments[i].allocation_fraction;
		for(std::size_t j = i + 1; j < data.assignments.size(); ++j)
			if(data.assignments[j].person == data.assignments[i].person) total += data.assignments[j].allocation_fraction;
		if(total > 1.0f + epsilon) errors.push_back("exact person's total research allocation exceeds one full-time assignment");
	}
	for(auto const& adoption : data.adoptions)
		if(!organization_by_stable_id(state, adoption.organization) || !capability_by_id(data, adoption.capability)
			|| !ready_holder(data, adoption.organization, adoption.capability)
			|| !prerequisites_met(data, adoption.organization, adoption.capability))
			errors.push_back("capability adoption lacks a valid holder or prerequisite state");
	for(std::size_t i = 0; i < data.adoptions.size(); ++i) for(std::size_t j = i + 1; j < data.adoptions.size(); ++j)
		if(data.adoptions[i].organization == data.adoptions[j].organization
			&& data.adoptions[i].capability == data.adoptions[j].capability)
			errors.push_back("duplicate capability adoption relation");
	for(auto const& transfer : data.transfers) {
		auto definition = capability_by_id(data, transfer.capability);
		if(!transfer.id || !transfer.authorization || !definition || definition->transferable == 0
			|| !organization_by_stable_id(state, transfer.source_organization)
			|| !organization_by_stable_id(state, transfer.destination_organization)
			|| !holder(data, transfer.source_organization, transfer.capability)
			|| !holder(data, transfer.destination_organization, transfer.capability))
			errors.push_back("capability transfer has invalid source, destination, target, or authorization");
	}
	for(std::size_t i = 0; i < data.transfers.size(); ++i) for(std::size_t j = i + 1; j < data.transfers.size(); ++j)
		if(data.transfers[i].id == data.transfers[j].id) errors.push_back("duplicate capability transfer stable ID");

	// Prerequisite cycles are not resolvable knowledge chains.
	std::vector<stable_id> active_nodes, complete_nodes;
	std::function<bool(stable_id)> visit = [&](stable_id current) {
		if(std::find(active_nodes.begin(), active_nodes.end(), current) != active_nodes.end()) return false;
		if(std::find(complete_nodes.begin(), complete_nodes.end(), current) != complete_nodes.end()) return true;
		active_nodes.push_back(current);
		for(auto const& prerequisite : data.prerequisites)
			if(prerequisite.capability == current && !visit(prerequisite.prerequisite)) return false;
		active_nodes.pop_back();
		complete_nodes.push_back(current);
		return true;
	};
	for(auto const& capability : data.capabilities)
		if(!visit(capability.id)) { errors.push_back("capability prerequisite graph contains a cycle"); break; }
	return errors.size() == initial_count;
}

uint64_t deterministic_checksum(sys::state const& state) {
	auto data = export_snapshot(state);
	uint64_t hash = 14695981039346656037ULL;
	hash = hash_word(hash, data.version);
	hash = hash_word(hash, data.canonical_runtime_active);
	for(auto const& value : data.capabilities) {
		hash = hash_word(hash, value.id); hash = hash_word(hash, value.domain);
		hash = hash_float(hash, value.research_effort); hash = hash_word(hash, value.codified); hash = hash_word(hash, value.transferable);
	}
	for(auto const& value : data.prerequisites) { hash = hash_word(hash, value.capability); hash = hash_word(hash, value.prerequisite); }
	for(auto const& value : data.factory_processes) { hash = hash_word(hash, value.process); hash = hash_word(hash, value.capability); }
	for(auto const& value : data.organizations) {
		hash = hash_word(hash, value.id);
		hash = hash_word(hash, state.world.organization_get_canonical_id(value.organization));
		hash = hash_word(hash, state.world.site_get_canonical_id(value.site));
		auto account_id = value.funding_account && state.world.monetary_account_is_valid(value.funding_account)
			? state.world.monetary_account_get_canonical_id(value.funding_account) : 0;
		hash = hash_word(hash, account_id); hash = hash_word(hash, uint8_t(value.role)); hash = hash_float(hash, value.effectiveness);
		hash = hash_float(hash, economy::accounts::balance(state, value.funding_account));
		hash = hash_float(hash, economy::physical::concrete_market::reserved_bid_amount(state, value.funding_account));
	}
	for(auto const& value : data.holders) { hash = hash_word(hash, value.organization); hash = hash_word(hash, value.capability); hash = hash_float(hash, value.maturity); }
	for(auto const& value : data.programs) {
		hash = hash_word(hash, value.id); hash = hash_word(hash, value.organization); hash = hash_word(hash, value.capability);
		hash = hash_word(hash, uint8_t(value.status)); hash = hash_word(hash, uint32_t(value.start_date.to_raw_value()));
		hash = hash_float(hash, value.effort_accumulated); hash = hash_float(hash, value.required_effort); hash = hash_float(hash, value.cost_per_effort);
	}
	for(auto const& value : data.assignments) {
		hash = hash_word(hash, value.program); hash = hash_word(hash, value.person.source_population_cell);
		hash = hash_word(hash, value.person.ordinal); hash = hash_float(hash, value.allocation_fraction); hash = hash_float(hash, value.productivity_input);
	}
	for(auto const& value : data.adoptions) { hash = hash_word(hash, value.organization); hash = hash_word(hash, value.capability); hash = hash_word(hash, uint32_t(value.date.to_raw_value())); }
	for(auto const& value : data.transfers) {
		hash = hash_word(hash, value.id); hash = hash_word(hash, value.source_organization); hash = hash_word(hash, value.destination_organization);
		hash = hash_word(hash, value.capability); hash = hash_word(hash, uint32_t(value.date.to_raw_value())); hash = hash_word(hash, value.authorization);
	}
	return hash;
}

} // namespace technology::kernel

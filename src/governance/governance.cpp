#include "governance.hpp"

#include "system_state.hpp"
#include "actors/ownership.hpp"

#include <type_traits>

namespace governance {
namespace {

bool date_set(sys::date date) { return bool(date); }

// The delegation relation is keyed by the delegated grant, so its id exists
// for every grant: the delegator itself tells whether there is one.
dcon::authority_grant_id delegator_of(sys::state const& state, dcon::authority_grant_id grant) {
	auto relation = state.world.authority_grant_get_authority_grant_delegator_as_delegated(grant);
	return relation ? state.world.authority_grant_delegator_get_delegator(relation) : dcon::authority_grant_id{};
}

template<typename Holder>
dcon::nation_id holder_nation(sys::state const& state, Holder holder) {
	if constexpr(std::is_same_v<Holder, dcon::institution_id>) return nation_of(state, holder);
	else return nation_of(state, institution_for_office(state, holder));
}

dcon::nation_id nation_of_jurisdiction(sys::state const& state, jurisdiction scope) {
	return scope.nation ? scope.nation : nation_of(state, scope.territory);
}

template<typename Holder>
void for_each_grant(sys::state const& state, Holder holder, auto&& function) {
	if constexpr(std::is_same_v<Holder, dcon::institution_id>)
		state.world.institution_for_each_authority_grant_institution_holder_as_institution(holder, [&](auto relation) {
			function(state.world.authority_grant_institution_holder_get_authority_grant(relation));
		});
	else
		state.world.office_for_each_authority_grant_office_holder_as_office(holder, [&](auto relation) {
			function(state.world.authority_grant_office_holder_get_authority_grant(relation));
		});
}

template<typename Holder>
dcon::authority_grant_id find_grant(sys::state const& state, Holder holder, authority_kind kind, jurisdiction scope, sys::date date) {
	dcon::authority_grant_id result{};
	if(!holder || !scope) return result;
	for_each_grant(state, holder, [&](dcon::authority_grant_id grant) {
		if(!result && state.world.authority_grant_get_kind(grant) == uint8_t(kind)
			&& covers(state, jurisdiction_of(state, grant), scope) && grant_valid(state, grant, date))
			result = grant;
	});
	return result;
}

template<typename Holder>
dcon::authority_grant_id create_grant(sys::state& state, Holder holder, authority_kind kind, jurisdiction scope,
	grant_terms const& terms, dcon::authority_grant_id delegator) {
	if(!holder || !scope || uint8_t(kind) > uint8_t(authority_kind::appropriate)) return {};
	auto nation = holder_nation(state, holder);
	if(!nation || nation_of_jurisdiction(state, scope) != nation) return {};
	if(date_set(terms.valid_from) && date_set(terms.valid_until) && !(terms.valid_from < terms.valid_until)) return {};
	auto grant = state.world.create_authority_grant();
	state.world.authority_grant_set_kind(grant, uint8_t(kind));
	state.world.authority_grant_set_valid_from(grant, terms.valid_from);
	state.world.authority_grant_set_valid_until(grant, terms.valid_until);
	if constexpr(std::is_same_v<Holder, dcon::institution_id>) state.world.force_create_authority_grant_institution_holder(grant, holder);
	else state.world.force_create_authority_grant_office_holder(grant, holder);
	if(scope.nation) state.world.force_create_authority_grant_nation_scope(grant, scope.nation);
	else state.world.force_create_authority_grant_territorial_scope(grant, scope.territory);
	if(terms.source) state.world.force_create_authority_grant_source(grant, terms.source);
	if(delegator) state.world.force_create_authority_grant_delegator(grant, delegator);
	return grant;
}

template<typename Holder>
dcon::authority_grant_id plain_grant(sys::state& state, Holder holder, authority_kind kind, jurisdiction scope) {
	// The same plain grant is not created twice.
	dcon::authority_grant_id existing{};
	for_each_grant(state, holder, [&](dcon::authority_grant_id grant) {
		if(!existing && state.world.authority_grant_get_kind(grant) == uint8_t(kind)
			&& jurisdiction_of(state, grant) == scope && grant_valid(state, grant, state.current_date)
			&& !state.world.authority_grant_get_legal_instrument_from_authority_grant_source(grant)
			&& !delegator_of(state, grant))
			existing = grant;
	});
	return existing ? existing : create_grant(state, holder, kind, scope, {}, {});
}

template<typename Holder>
dcon::authority_grant_id delegate(sys::state& state, dcon::authority_grant_id grant, Holder holder, jurisdiction scope, sys::date date) {
	if(!grant || !state.world.authority_grant_is_valid(grant) || !grant_valid(state, grant, date)) return {};
	// A delegation may narrow a national power to a territorial unit of the
	// same nation, but never widen it.
	auto source = jurisdiction_of(state, grant);
	auto narrows = source.nation && scope.territory && nation_of(state, scope.territory) == source.nation;
	if(!covers(state, source, scope) && !narrows) return {};
	grant_terms terms{};
	terms.valid_from = date;
	terms.valid_until = state.world.authority_grant_get_valid_until(grant);
	return create_grant(state, holder, authority_kind(state.world.authority_grant_get_kind(grant)), scope, terms, grant);
}

// The service an institution of a kind provides unless its constitution says
// otherwise. It carries no staff and no power by itself.
service_kind default_service(institution_kind kind) {
	switch(kind) {
	case institution_kind::finance_ministry: case institution_kind::cabinet: case institution_kind::regional_government:
	case institution_kind::municipality: case institution_kind::ministry: case institution_kind::agency:
	case institution_kind::regulator: return service_kind::administration;
	case institution_kind::education_ministry: return service_kind::education;
	case institution_kind::interior_ministry: return service_kind::policing;
	case institution_kind::public_works_ministry: return service_kind::construction;
	case institution_kind::tax_authority: return service_kind::revenue;
	case institution_kind::central_bank: return service_kind::monetary;
	case institution_kind::military_command: return service_kind::defense;
	case institution_kind::legislature: case institution_kind::legislative_chamber: return service_kind::legislation;
	case institution_kind::court: case institution_kind::prosecutor: return service_kind::justice;
	default: return service_kind::none;
	}
}

} // namespace

dcon::institution_id create_institution(sys::state& state, dcon::nation_id nation, institution_kind kind) {
	if(!nation) return {};
	auto institution = state.world.create_institution();
	state.world.institution_set_kind(institution, uint8_t(kind));
	state.world.force_create_institution_nation(institution, nation);
	auto actor = state.world.create_economic_actor();
	state.world.economic_actor_set_kind(actor, uint8_t(actors::ownership::actor_kind::state_entity));
	state.world.force_create_institution_actor(institution, actor);
	state.world.institution_set_service(institution, uint8_t(default_service(kind)));
	state.world.institution_set_staff_wage_multiplier(institution, 1.0f);
	return institution;
}

dcon::economic_actor_id actor_for_institution(sys::state const& state, dcon::institution_id institution) {
	return institution ? state.world.institution_get_economic_actor_from_institution_actor(institution) : dcon::economic_actor_id{};
}

dcon::nation_id nation_of(sys::state const& state, dcon::institution_id institution) {
	return institution ? state.world.institution_get_nation_from_institution_nation(institution) : dcon::nation_id{};
}

std::vector<dcon::institution_id> institutions_of(sys::state const& state, dcon::nation_id nation) {
	std::vector<dcon::institution_id> result;
	if(!nation) return result;
	state.world.nation_for_each_institution_nation_as_nation(nation, [&](dcon::institution_nation_id relation) {
		result.push_back(state.world.institution_nation_get_institution(relation));
	});
	return result;
}

institution_kind kind_of(sys::state const& state, dcon::institution_id institution) {
	return institution_kind(state.world.institution_get_kind(institution));
}

dcon::institution_id find_institution(sys::state const& state, dcon::nation_id nation, institution_kind kind) {
	for(auto institution : institutions_of(state, nation))
		if(kind_of(state, institution) == kind) return institution;
	return {};
}

dcon::territorial_unit_id territory_of(sys::state const& state, dcon::institution_id institution) {
	auto local_government = institution ? state.world.institution_get_local_government_from_local_government_institution(institution) : dcon::local_government_id{};
	auto relation = local_government ? state.world.local_government_get_local_government_jurisdiction(local_government) : dcon::local_government_jurisdiction_id{};
	return relation ? state.world.local_government_jurisdiction_get_territorial_unit(relation) : dcon::territorial_unit_id{};
}

jurisdiction jurisdiction_of(sys::state const& state, dcon::institution_id institution) {
	if(auto territory = territory_of(state, institution)) return local(territory);
	return national(nation_of(state, institution));
}

bool set_parent(sys::state& state, dcon::institution_id child, dcon::institution_id parent) {
	if(!child || !parent || child == parent || nation_of(state, child) != nation_of(state, parent)) return false;
	if(parent_of(state, child) == parent) return true;
	for(auto ancestor = parent; ancestor; ancestor = parent_of(state, ancestor))
		if(ancestor == child) return false;
	if(auto old = state.world.institution_get_institution_parent_as_child(child)) state.world.delete_institution_parent(old);
	state.world.force_create_institution_parent(child, parent);
	return true;
}

dcon::institution_id parent_of(sys::state const& state, dcon::institution_id child) {
	if(!child) return {};
	auto relation = state.world.institution_get_institution_parent_as_child(child);
	return relation ? state.world.institution_parent_get_parent(relation) : dcon::institution_id{};
}

std::vector<dcon::institution_id> children_of(sys::state const& state, dcon::institution_id parent) {
	std::vector<dcon::institution_id> result;
	if(!parent) return result;
	state.world.institution_for_each_institution_parent_as_parent(parent, [&](dcon::institution_parent_id relation) {
		result.push_back(state.world.institution_parent_get_child(relation));
	});
	return result;
}

dcon::territorial_unit_id parent_of(sys::state const& state, dcon::territorial_unit_id territory) {
	auto relation = territory ? state.world.territorial_unit_get_territorial_unit_parent_as_child(territory) : dcon::territorial_unit_parent_id{};
	return relation ? state.world.territorial_unit_parent_get_parent(relation) : dcon::territorial_unit_id{};
}

dcon::nation_id nation_of(sys::state const& state, dcon::territorial_unit_id territory) {
	for(auto unit = territory; unit; unit = parent_of(state, unit))
		if(auto nation = state.world.territorial_unit_get_nation_from_territorial_unit_owner(unit)) return nation;
	return {};
}

dcon::territorial_unit_id territory_of(sys::state const& state, dcon::province_id province) {
	auto relation = province ? state.world.province_get_territorial_unit_membership(province) : dcon::territorial_unit_membership_id{};
	return relation ? state.world.territorial_unit_membership_get_territorial_unit(relation) : dcon::territorial_unit_id{};
}

bool contains(sys::state const& state, dcon::territorial_unit_id outer, dcon::territorial_unit_id inner) {
	if(!outer) return false;
	for(auto unit = inner; unit; unit = parent_of(state, unit))
		if(unit == outer) return true;
	return false;
}

bool contains(sys::state const& state, jurisdiction scope, dcon::province_id province) {
	if(!province) return false;
	if(scope.nation) return state.world.province_get_nation_from_province_ownership(province) == scope.nation;
	return contains(state, scope.territory, territory_of(state, province));
}

std::vector<dcon::province_id> provinces_in(sys::state const& state, jurisdiction scope) {
	std::vector<dcon::province_id> result;
	if(scope.nation) {
		for(auto ownership : state.world.nation_get_province_ownership(scope.nation)) result.push_back(ownership.get_province().id);
		return result;
	}
	std::vector<dcon::territorial_unit_id> pending{ scope.territory };
	while(!pending.empty()) {
		auto unit = pending.back();
		pending.pop_back();
		if(!unit) continue;
		state.world.territorial_unit_for_each_territorial_unit_membership_as_territorial_unit(unit, [&](auto relation) {
			result.push_back(state.world.territorial_unit_membership_get_province(relation));
		});
		state.world.territorial_unit_for_each_territorial_unit_parent_as_parent(unit, [&](auto relation) {
			pending.push_back(state.world.territorial_unit_parent_get_child(relation));
		});
	}
	return result;
}

dcon::office_id create_office(sys::state& state, dcon::institution_id institution, office_kind kind) {
	if(!institution) return {};
	auto office = state.world.create_office();
	state.world.office_set_kind(office, uint8_t(kind));
	state.world.force_create_office_institution(office, institution);
	return office;
}

dcon::institution_id institution_for_office(sys::state const& state, dcon::office_id office) {
	return office ? state.world.office_get_institution_from_office_institution(office) : dcon::institution_id{};
}

std::vector<dcon::office_id> offices_of(sys::state const& state, dcon::institution_id institution) {
	std::vector<dcon::office_id> result;
	if(!institution) return result;
	state.world.institution_for_each_office_institution_as_institution(institution, [&](dcon::office_institution_id relation) {
		result.push_back(state.world.office_institution_get_office(relation));
	});
	return result;
}

dcon::authority_grant_id grant_authority_to_institution(sys::state& state, dcon::institution_id holder, authority_kind kind, jurisdiction scope, grant_terms const& terms) {
	return create_grant(state, holder, kind, scope, terms, {});
}
dcon::authority_grant_id grant_authority_to_office(sys::state& state, dcon::office_id holder, authority_kind kind, jurisdiction scope, grant_terms const& terms) {
	return create_grant(state, holder, kind, scope, terms, {});
}
dcon::authority_grant_id grant_authority_to_institution(sys::state& state, dcon::institution_id holder, authority_kind kind, dcon::nation_id nation) {
	return plain_grant(state, holder, kind, national(nation));
}
dcon::authority_grant_id grant_authority_to_institution(sys::state& state, dcon::institution_id holder, authority_kind kind, dcon::territorial_unit_id territory) {
	return plain_grant(state, holder, kind, local(territory));
}
dcon::authority_grant_id grant_authority_to_office(sys::state& state, dcon::office_id holder, authority_kind kind, dcon::nation_id nation) {
	return plain_grant(state, holder, kind, national(nation));
}
dcon::authority_grant_id grant_authority_to_office(sys::state& state, dcon::office_id holder, authority_kind kind, dcon::territorial_unit_id territory) {
	return plain_grant(state, holder, kind, local(territory));
}

dcon::authority_grant_id delegate_authority(sys::state& state, dcon::authority_grant_id grant, dcon::institution_id holder, jurisdiction scope, sys::date date) {
	return delegate(state, grant, holder, scope, date);
}
dcon::authority_grant_id delegate_authority(sys::state& state, dcon::authority_grant_id grant, dcon::office_id holder, jurisdiction scope, sys::date date) {
	return delegate(state, grant, holder, scope, date);
}

bool revoke_authority(sys::state& state, dcon::authority_grant_id grant, sys::date date) {
	if(!grant || !state.world.authority_grant_is_valid(grant) || !date) return false;
	auto until = state.world.authority_grant_get_valid_until(grant);
	if(!date_set(until) || date < until) state.world.authority_grant_set_valid_until(grant, date);
	return true;
}

bool revoke_authority(sys::state& state, dcon::authority_grant_id grant) {
	return revoke_authority(state, grant, state.current_date);
}

bool grant_valid(sys::state const& state, dcon::authority_grant_id grant, sys::date date) {
	for(int32_t depth = 0; grant && depth < 64; ++depth) {
		if(!state.world.authority_grant_is_valid(grant)) return false;
		auto from = state.world.authority_grant_get_valid_from(grant);
		auto until = state.world.authority_grant_get_valid_until(grant);
		if(date_set(from) && date < from) return false;
		if(date_set(until) && !(date < until)) return false;
		auto delegator = delegator_of(state, grant);
		if(!delegator) return true;
		grant = delegator;
	}
	return false;
}

jurisdiction jurisdiction_of(sys::state const& state, dcon::authority_grant_id grant) {
	if(!grant) return {};
	if(auto nation = state.world.authority_grant_get_nation_from_authority_grant_nation_scope(grant)) return national(nation);
	return local(state.world.authority_grant_get_territorial_unit_from_authority_grant_territorial_scope(grant));
}

dcon::authority_grant_id delegated_from(sys::state const& state, dcon::authority_grant_id grant) {
	return grant ? delegator_of(state, grant) : dcon::authority_grant_id{};
}

bool covers(sys::state const& state, jurisdiction grant, jurisdiction action) {
	if(!grant || !action) return false;
	if(grant.nation) return action.nation == grant.nation;
	return action.territory && contains(state, grant.territory, action.territory);
}

dcon::authority_grant_id authority_grant_for(sys::state const& state, dcon::institution_id holder, authority_kind kind, jurisdiction scope, sys::date date) {
	return find_grant(state, holder, kind, scope, date);
}
dcon::authority_grant_id authority_grant_for(sys::state const& state, dcon::office_id holder, authority_kind kind, jurisdiction scope, sys::date date) {
	return find_grant(state, holder, kind, scope, date);
}
bool has_authority(sys::state const& state, dcon::institution_id holder, authority_kind kind, jurisdiction scope, sys::date date) {
	return bool(find_grant(state, holder, kind, scope, date));
}
bool has_authority(sys::state const& state, dcon::office_id holder, authority_kind kind, jurisdiction scope, sys::date date) {
	return bool(find_grant(state, holder, kind, scope, date));
}
bool has_authority(sys::state const& state, dcon::institution_id holder, authority_kind kind, dcon::nation_id nation) {
	return has_authority(state, holder, kind, national(nation), state.current_date);
}
bool has_authority(sys::state const& state, dcon::institution_id holder, authority_kind kind, dcon::territorial_unit_id territory) {
	return has_authority(state, holder, kind, local(territory), state.current_date);
}
bool has_authority(sys::state const& state, dcon::office_id holder, authority_kind kind, dcon::nation_id nation) {
	return has_authority(state, holder, kind, national(nation), state.current_date);
}
bool has_authority(sys::state const& state, dcon::office_id holder, authority_kind kind, dcon::territorial_unit_id territory) {
	return has_authority(state, holder, kind, local(territory), state.current_date);
}

dcon::institution_id central_government_for(sys::state& state, dcon::nation_id nation) {
	if(auto existing = find_institution(state, nation, institution_kind::central_government)) return existing;
	return create_institution(state, nation, institution_kind::central_government);
}

bool bind_local_government(sys::state& state, dcon::local_government_id local_government, dcon::institution_id institution) {
	if(!local_government || !institution || !nation_of(state, institution)) return false;
	auto current = state.world.local_government_get_institution_from_local_government_institution(local_government);
	if(current) return current == institution;
	state.world.force_create_local_government_institution(local_government, institution);
	return true;
}

void bootstrap(sys::state& state) {
	state.world.for_each_nation([&](dcon::nation_id nation) { (void)central_government_for(state, nation); });
}

} // namespace governance

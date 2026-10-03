#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

#include <cstdint>
#include <vector>

namespace sys { class state; }

namespace governance {

// Public power belongs to concrete institutions and offices. A state is a tree
// of institutions; every public action is taken by an office holder or by an
// institution acting under an authority grant valid for the action's
// jurisdiction on its date. No action is authorized because a nation exists.

// Values are saved: new kinds are appended.
enum class institution_kind : uint8_t {
	central_government, ministry, central_bank, tax_authority, regulator, court, prosecutor, military_command,
	regional_government, municipality, other, household_sector, finance_ministry, education_ministry,
	interior_ministry, public_works_ministry,
	head_of_state, cabinet, legislature, legislative_chamber, agency
};
enum class office_kind : uint8_t {
	president, prime_minister, finance_minister, central_bank_governor, chief_of_general_staff, mayor,
	agency_director, judge_seat, other,
	head_of_state, head_of_government, minister, legislator, speaker, regional_governor, chief_justice,
	prosecutor_general, heir, deputy_head_of_state
};
enum class authority_kind : uint8_t {
	administer, regulate, levy_tax, spend_public_funds, license, enforce, adjudicate, command_forces, appoint,
	dismiss, issue_currency, expropriate, other, issue_public_debt, legislate,
	confirm, assent, amend_constitution, appropriate
};
// What an institution's staff produce: the service its mandate funds.
enum class service_kind : uint8_t {
	none, administration, education, policing, construction, revenue, monetary, defense, legislation, justice
};

// An action's jurisdiction: a whole nation, or a territorial unit inside one.
// A national grant does not cover a territorial action, nor the reverse; a
// grant over a territorial unit covers the units nested inside it.
struct jurisdiction {
	dcon::nation_id nation{};
	dcon::territorial_unit_id territory{};
	explicit operator bool() const noexcept { return bool(nation) != bool(territory); }
	friend bool operator==(jurisdiction const&, jurisdiction const&) = default;
};
inline jurisdiction national(dcon::nation_id nation) { return { nation, {} }; }
inline jurisdiction local(dcon::territorial_unit_id territory) { return { {}, territory }; }

// Institutions.
dcon::institution_id create_institution(sys::state&, dcon::nation_id, institution_kind);
dcon::economic_actor_id actor_for_institution(sys::state const&, dcon::institution_id);
dcon::nation_id nation_of(sys::state const&, dcon::institution_id);
std::vector<dcon::institution_id> institutions_of(sys::state const&, dcon::nation_id);
institution_kind kind_of(sys::state const&, dcon::institution_id);
dcon::institution_id find_institution(sys::state const&, dcon::nation_id, institution_kind);
// The territory of a regional or municipal government, else none.
dcon::territorial_unit_id territory_of(sys::state const&, dcon::institution_id);
// Where an institution acts: its territory, else its nation.
jurisdiction jurisdiction_of(sys::state const&, dcon::institution_id);
bool set_parent(sys::state&, dcon::institution_id, dcon::institution_id);
dcon::institution_id parent_of(sys::state const&, dcon::institution_id);
std::vector<dcon::institution_id> children_of(sys::state const&, dcon::institution_id);
// The nation a territorial unit belongs to.
dcon::nation_id nation_of(sys::state const&, dcon::territorial_unit_id);
dcon::territorial_unit_id parent_of(sys::state const&, dcon::territorial_unit_id);
// The innermost territorial unit a province belongs to.
dcon::territorial_unit_id territory_of(sys::state const&, dcon::province_id);
bool contains(sys::state const&, dcon::territorial_unit_id outer, dcon::territorial_unit_id inner);
bool contains(sys::state const&, jurisdiction, dcon::province_id);
std::vector<dcon::province_id> provinces_in(sys::state const&, jurisdiction);

// Offices.
dcon::office_id create_office(sys::state&, dcon::institution_id, office_kind);
dcon::institution_id institution_for_office(sys::state const&, dcon::office_id);
std::vector<dcon::office_id> offices_of(sys::state const&, dcon::institution_id);

// Authority. A grant has a holder (an institution or an office), a
// jurisdiction, a validity interval, optionally the legal instrument it rests
// on, and optionally the grant it was delegated from. A delegated grant lasts
// only while its delegator is valid.
struct grant_terms {
	sys::date valid_from{};
	sys::date valid_until{};
	dcon::legal_instrument_id source{};
};
dcon::authority_grant_id grant_authority_to_institution(sys::state&, dcon::institution_id, authority_kind, jurisdiction, grant_terms const& = {});
dcon::authority_grant_id grant_authority_to_office(sys::state&, dcon::office_id, authority_kind, jurisdiction, grant_terms const& = {});
dcon::authority_grant_id grant_authority_to_institution(sys::state&, dcon::institution_id, authority_kind, dcon::nation_id);
dcon::authority_grant_id grant_authority_to_institution(sys::state&, dcon::institution_id, authority_kind, dcon::territorial_unit_id);
dcon::authority_grant_id grant_authority_to_office(sys::state&, dcon::office_id, authority_kind, dcon::nation_id);
dcon::authority_grant_id grant_authority_to_office(sys::state&, dcon::office_id, authority_kind, dcon::territorial_unit_id);
// The holder of a valid grant hands its power, within the grant's
// jurisdiction, to another institution or office.
dcon::authority_grant_id delegate_authority(sys::state&, dcon::authority_grant_id, dcon::institution_id, jurisdiction, sys::date);
dcon::authority_grant_id delegate_authority(sys::state&, dcon::authority_grant_id, dcon::office_id, jurisdiction, sys::date);
// Ends a grant from `date`; grants delegated from it end with it.
bool revoke_authority(sys::state&, dcon::authority_grant_id, sys::date);
bool revoke_authority(sys::state&, dcon::authority_grant_id);
bool grant_valid(sys::state const&, dcon::authority_grant_id, sys::date);
jurisdiction jurisdiction_of(sys::state const&, dcon::authority_grant_id);
// The grant this one was delegated from, if any.
dcon::authority_grant_id delegated_from(sys::state const&, dcon::authority_grant_id);
bool covers(sys::state const&, jurisdiction grant, jurisdiction action);
// The valid grant through which a holder may act, if any.
dcon::authority_grant_id authority_grant_for(sys::state const&, dcon::institution_id, authority_kind, jurisdiction, sys::date);
dcon::authority_grant_id authority_grant_for(sys::state const&, dcon::office_id, authority_kind, jurisdiction, sys::date);
bool has_authority(sys::state const&, dcon::institution_id, authority_kind, jurisdiction, sys::date);
bool has_authority(sys::state const&, dcon::office_id, authority_kind, jurisdiction, sys::date);
// On the current date.
bool has_authority(sys::state const&, dcon::institution_id, authority_kind, dcon::nation_id);
bool has_authority(sys::state const&, dcon::institution_id, authority_kind, dcon::territorial_unit_id);
bool has_authority(sys::state const&, dcon::office_id, authority_kind, dcon::nation_id);
bool has_authority(sys::state const&, dcon::office_id, authority_kind, dcon::territorial_unit_id);

// Administrative continuity and political discretion. An institution keeps
// executing what law and budget already decided (collecting taxes, paying
// staff, disbursing appropriations, operating its mandate) even while its
// offices are empty. Discretion is exercised only by a person through an
// office, or by a chamber through its members' votes. Discretion means: making
// law or regulation, appointing, dismissing, confirming, assenting, borrowing,
// licensing, expropriating, adjudicating, commanding forces, and amending the
// constitution. An institution may still hold a discretionary grant as a
// link in a delegation chain, but it never acts on one in its own name.
bool discretionary(authority_kind);
// Whether an institution may act in its own name: the power is administrative
// and the institution holds a valid grant for it.
bool acts_administratively(sys::state const&, dcon::institution_id, authority_kind, jurisdiction, sys::date);

// The state itself as a legal person: the root of the nation's institutions,
// owner of public property and residual heir. It holds no power of its own;
// powers are granted by the constitution.
dcon::institution_id central_government_for(sys::state&, dcon::nation_id);
bool bind_local_government(sys::state&, dcon::local_government_id, dcon::institution_id);
void bootstrap(sys::state&);

} // namespace governance

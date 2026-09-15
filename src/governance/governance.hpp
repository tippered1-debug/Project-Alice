#pragma once
#include "dcon_generated.hpp"
namespace sys { class state; }
namespace governance {
enum class institution_kind : uint8_t { central_government, ministry, central_bank, tax_authority, regulator, court, prosecutor, military_command, regional_government, municipality, other };
enum class office_kind : uint8_t { president, prime_minister, finance_minister, central_bank_governor, chief_of_general_staff, mayor, agency_director, judge_seat, other };
enum class authority_kind : uint8_t { administer, regulate, levy_tax, spend_public_funds, license, enforce, adjudicate, command_forces, appoint, dismiss, issue_currency, expropriate, other, issue_public_debt, legislate };
dcon::institution_id create_institution(sys::state&, dcon::nation_id, institution_kind);
dcon::economic_actor_id actor_for_institution(sys::state const&, dcon::institution_id);
dcon::nation_id nation_of(sys::state const&, dcon::institution_id);
std::vector<dcon::institution_id> institutions_of(sys::state const&, dcon::nation_id);
dcon::office_id create_office(sys::state&, dcon::institution_id, office_kind);
dcon::institution_id institution_for_office(sys::state const&, dcon::office_id);
bool set_parent(sys::state&, dcon::institution_id, dcon::institution_id);
dcon::institution_id parent_of(sys::state const&, dcon::institution_id);
std::vector<dcon::institution_id> children_of(sys::state const&, dcon::institution_id);
dcon::authority_grant_id grant_authority_to_institution(sys::state&, dcon::institution_id, authority_kind, dcon::nation_id);
dcon::authority_grant_id grant_authority_to_institution(sys::state&, dcon::institution_id, authority_kind, dcon::territorial_unit_id);
dcon::authority_grant_id grant_authority_to_office(sys::state&, dcon::office_id, authority_kind, dcon::nation_id);
dcon::authority_grant_id grant_authority_to_office(sys::state&, dcon::office_id, authority_kind, dcon::territorial_unit_id);
bool revoke_authority(sys::state&, dcon::authority_grant_id);
bool has_authority(sys::state const&, dcon::institution_id, authority_kind, dcon::nation_id);
bool has_authority(sys::state const&, dcon::institution_id, authority_kind, dcon::territorial_unit_id);
bool has_authority(sys::state const&, dcon::office_id, authority_kind, dcon::nation_id);
bool has_authority(sys::state const&, dcon::office_id, authority_kind, dcon::territorial_unit_id);
dcon::institution_id central_government_for(sys::state&, dcon::nation_id);
bool bind_local_government(sys::state&, dcon::local_government_id, dcon::institution_id);
void bootstrap(sys::state&);
// Legacy politics remains nation-level; governance grants are the future authority source of truth.
}

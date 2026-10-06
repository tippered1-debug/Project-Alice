#pragma once

#include "dcon_generated.hpp"
#include "economy/information/information.hpp"
#include "governance/governance.hpp"

namespace sys { class state; }

namespace governance::intelligence {

dcon::institution_id agency_for(sys::state const&, dcon::nation_id,
	service_kind kind = service_kind::intelligence);
dcon::office_id director_office_for(sys::state const&, dcon::institution_id);

// Readiness is derived from funded staffing in the ordinary institution and
// payroll ledgers. It is zero for an agency with no active staff or funds.
float operational_readiness(sys::state const&, dcon::institution_id);

// Reports are sourced by the observer's intelligence institution and delivered
// to its central government actor after a fixed processing delay.
dcon::information_report_id publish_assessment(sys::state&,
	dcon::nation_id observer, dcon::nation_id subject,
	economy::information::information_fact_kind, float reported_value,
	float confidence, sys::date fact_date, sys::date received_on);

} // namespace governance::intelligence

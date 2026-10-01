#pragma once

#include "dcon_generated.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/relations/relations.hpp"

namespace sys { class state; }

namespace economy::wallets {

using account_ref = economy::exact_person_economy::account_ref;

// Every economic actor holds cash in exactly one ledger per settlement: a person
// in their exact account, a government institution in its treasury account,
// and an organization or household in its operating account. Owner income and
// owner payments go through this ledger, so a person's capital income is the
// same money they consume from.
account_ref account_for(sys::state const&, dcon::economic_actor_id, dcon::commodity_id settlement);
account_ref open_for(sys::state&, dcon::economic_actor_id, dcon::commodity_id settlement);
// Balance not reserved by active bids.
float spendable(sys::state const&, account_ref);
bool pay(sys::state&, account_ref source, account_ref destination, float amount, relations::transaction_kind);

} // namespace economy::wallets

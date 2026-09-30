#pragma once

#include "dcon_generated_ids.hpp"

namespace economy {
// Commodity 0 is the settlement unit used by legacy money-priced markets.
// It identifies a unit of account, not the balance instrument or the issuer.
constexpr inline dcon::commodity_id money(0);
}

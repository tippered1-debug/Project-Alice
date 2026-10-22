# Foreign policy: Strategic Statecraft

Strategic Statecraft gives every country a saved, bounded model for external
policy. Scenario loading initializes every country; a country created during a
campaign receives its profile in the creation path before it can make or answer
a foreign-policy decision. The model is required when loading a save.
The decision loop is:

```text
interests -> beliefs -> threats and opportunities -> objective
          -> commitments -> bargaining -> outcome -> updated beliefs
```

It is deterministic and uses the same limited information and diplomatic
history each country can observe. It does not use a personality catalogue or a
language model.

## Country state

Each country stores weights for security, territorial claims, market,
resource and route access, protection of allies and subjects, and prestige.
It also stores its current objective, target and value, plus its last decision.
Initial weights are bounded and derived from a small set of scenario facts such
as great-power status, ports, subject status and cores held by other countries.

Beliefs are stored separately for each observer and subject. Military and
industrial power estimates move toward observed values only when the observer
has a reason to know the country well; stale estimates drift toward uncertainty.
Resolve and reliability change as threats are rejected, promises are kept or
broken, concessions are made, and wars end. Crisis membership and visible
relationships expose events; unrelated countries do not receive every event as
world knowledge.

Alliances are evaluated through relations, common threats, perceived value and
reliability, reach, conflicting claims, entanglement, and third countries: being
allied to the observer's rival or to a country at war with it, or at war with
one of its allies, weighs against an alliance; sharing one of its allies weighs
for it. Forming an alliance
creates reciprocal promises. Breaking it records a broken commitment. Subject
relationships create a protector commitment from overlord to subject, which can
be kept in a crisis or broken when the relationship ends.

## What one government can tell of another

`strategic_statecraft_forecast.hpp` exposes each answer twice. The `forecast_*`
functions are the other side's own calculation; the deciding functions call
them and record the decision. The `assess_*` functions are what a proposer can
tell of that calculation before it asks, and they are all the interface shows.
An assessment recomputes the calculation with what is public (relations,
alliances, wars, claims, distance, the proposer's own record) and substitutes
the proposer's reconstruction for what is not: whom the other side counts its
rival, what it believes of the proposer's power and economy, and its interest
weights. A reconstruction starts from the proposer's own beliefs and moves
toward the truth by the quality of its reading of the other side.

That quality (`read_intentions`) combines independent sources: standing contact
(an alliance, a border, a war, rivalry, a great power's attention), the
intelligence agency's latest report on the country, the latest report of the
proposer's own envoys, and the openness of a friendly government. A diplomatic
mission (`increase_relations`) publishes an envoys' report from the central
government, at most once a month per country, received five days later. The
assessment carries a spread for what is inferred, and the answer is named from
the margin and the spread: very likely, likely, uncertain, unlikely, very
unlikely, or out of the question when a public reason (hostility, ill repute,
contested claims) refuses it whatever is offered. Nothing is drawn at random; a
poorly read country can still surprise the proposer.

`alliance_reactions` evaluates, for every modelled country, its own alliance
calculation of the proposer before and after a hypothetical alliance with the
partner, and reports the ones that move. A reaction whose cause is a hidden
rivalry is reported only when the proposer reads that country well.

## Crisis bargaining

A crisis tracks the claim, target, phase, offers, mobilization cost and outcome.
Interested countries compare their own interests, perceived balance,
reliability, reach and expected losses before choosing a side. Joining records
reciprocal backing commitments; abandoning the claim after soliciting support
damages the claimant's credibility with its backers.

AI leaders may demand the existing crisis wargoals, counter with their own
terms, use reparations as a counteroffer when they have no counterclaim, offer a
subset of terms when there are several, or accept a real crisis peace offer.
Settlements use the game's peace-offer implementation, so accepted terms
transfer territory or apply their normal effects. Mobilization uses the
existing readiness system and carries an explicit bargaining cost. If
bargaining fails, escalation depends on the actors' perceived coalition balance
and stakes. The existing crisis temperature and
`diplomatic_crisis_dynamics` remain useful projections and diagnostics; they do
not choose actions for modelled leaders.

## Save contract and diagnostics

Interests, beliefs, commitments and the current crisis are stored in a required
framed, versioned save extension. A save without a valid Statecraft record is
rejected. Legacy diplomacy AI fallback is not part of the runtime.

The headless runner's JSONL `statecraft` record reports crisis phase and
outcome, claimant and target, offers, readiness cost, and each participant's
objective, interests, beliefs, memory and commitment count. This makes a crisis
explainable in terms of what its actors wanted, believed, promised and accepted.

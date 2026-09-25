# Strategic Statecraft v1

Strategic Statecraft gives each AI country a saved, bounded model for external
policy. The decision loop is:

```text
interests -> beliefs -> threats and opportunities -> objective
          -> commitments -> bargaining -> outcome -> updated beliefs
```

It is deterministic and uses the same limited information and diplomatic
history each country can observe. It does not use a personality catalogue or a
language model.

## Country state

Each migrated country stores weights for security, territorial claims, market,
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

Alliances are evaluated through common threats, perceived value and
reliability, reach, conflicting claims, and entanglement. Forming an alliance
creates reciprocal promises. Breaking it records a broken commitment. Subject
relationships create a protector commitment from overlord to subject, which can
be kept in a crisis or broken when the relationship ends.

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

## Compatibility and diagnostics

Interests, beliefs, commitments and the current crisis are stored in a framed,
versioned save extension. Older saves without that extension keep using the
legacy diplomacy AI. This provides compatibility for saves that have not yet
migrated to the new model.

The headless runner's JSONL `statecraft` record reports crisis phase and
outcome, claimant and target, offers, readiness cost, and each participant's
objective, interests, beliefs, memory and commitment count. This makes a crisis
explainable in terms of what its actors wanted, believed, promised and accepted.

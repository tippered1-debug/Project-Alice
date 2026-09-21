# Representation-Neutral Ordering v1

Canonical economic events that compete in one queue are ordered by the same
key regardless of whether their state is held in DCON or in a sparse exact
store:

1. effective date;
2. a monotonic causal sequence allocated when the event is created.

The key never contains a representation kind, object address, container
iteration order, or a pointer. A stable object identifier is retained only as
a defensive final tie-break if a restored or externally constructed record has
an invalid/colliding sequence.

The shared ordering service is in `economy/causal_order.*`. Sparse exact bids,
applications, freight requests, and employment contracts persist their
sequence in their isolated snapshots. DCON objects use the state's auxiliary
`(event kind, object index)` registry, which assigns a sequence once and
observes restored exact sequences so future allocations are greater than all
restored values. No per-person ordering state is allocated.

The concrete goods market, job-market application queue, freight-request queue,
and labor-dynamics layoff selection all consume the same date/sequence rule.
This removes the former same-date representation preference (including the
legacy-first layoff tie-break). Existing economic price, wage, freight, and
labor policy rules remain responsible for eligibility and economic priority;
causal ordering only resolves otherwise competing events.

Job-market orchestration creates and refreshes the offer universe before the
sparse exact displaced-worker queue or the DCON population searches it. Their
applications then enter one pending queue, so a newly created vacancy is not
representation-dependent.

Canonical payroll uses one logical wage-claim queue with two global phases:

1. Phase A settles arrears only for all DCON and exact claims. Older
   `arrears_since` dates precede newer dates; equal dates use the shared causal
   sequence and stable object identifier.
2. Phase B is entered only after every arrears claim is clear. It settles
   current wages only, ordered by the shared causal sequence and stable object
   identifier. If Phase A cannot clear all arrears, no current wage is paid in
   that payroll call.

Events distinguish arrears repayment (`gross_due == 0`) from current wage due;
settlement still dispatches to the existing DCON or exact ledger, including
terminated contracts with surviving arrears.

Ordering state is currently an isolated state-side service. The normal save
pipeline still requires the existing exact snapshot boundaries to persist
sparse records; a future DCON save integration must serialize the auxiliary
registry or reconstruct it from persisted causal fields before processing
events. Full-world per-object migration is intentionally out of scope.

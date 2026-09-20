# Layoffs, quits, and unemployment v1

Canonical factories use `firm_agency::decide_factory(...).desired_units` as
their labor target. Concrete labor supplied is the sum of active legacy
employment contracts and active exact-person contracts; provincial POP labor
aggregates are not used to decide individual employment.

At the end of the production/payroll part of a transformation-era economy
tick, labor dynamics first processes arrears-based worker quits, then closes
open offers when supplied labor already meets the target and contracts any
remaining surplus. A layoff always ends a whole contract. The
deterministic order is descending daily contractual wage cost, newest start
date first, then legacy/exact kind and stable contract ID. A contract is
removed only when its removal leaves supplied labor at or above the target,
unless the target is zero.

Unemployment is derived state: an alive, individually work-eligible labor
force participant with no active concrete contract. Legacy and exact workers
therefore remain independent of POP employment/unemployment mutations.

Payroll runs before contraction. Wage arrears remain attached to the ended
contract and are settled by later payroll passes; they are never redirected
to a provincial labor clearing account for an individual contract. A worker
whose arrears reach one full contractual pay period may quit after payroll.
Current wages and arrears repayment are recorded as separate observations.

Separated workers cannot reapply on the separation date. Exact displaced
workers are held in a deterministic sparse queue and receive one explicit
search attempt on a later job-market pass. The queue and exact same-day
cooldowns are included in the isolated exact-economy snapshot boundary. The
normal save pipeline does not yet serialize that snapshot.

This milestone does not add benefits, unions, strikes, household behavior,
taxes, banking, migration, or a new population storage format.

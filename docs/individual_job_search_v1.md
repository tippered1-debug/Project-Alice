# Individual Job Search v1

Individual Job Search v1 connects existing concrete people to existing
concrete hiring. It does not create aggregate labor-pool workers, synthetic
applicants, or aggregate unemployment authority.

## Search contract

On each economy tick, a person searches when the person is alive, has no active
`EmploymentContract`, and has no pending application attached to an open offer.
Pending applications attached to closed or expired offers are rejected during
the next search pass, so the person may search again. A search creates at most
one `JobApplication`; it does not create a contract, account, transaction, or
money transfer.

The v1 candidate set is the stable ID-sorted list of open offers with positive
openings. Each offer must have a valid concrete factory and workplace site.
Candidates are ranked by higher concrete `wage_rate`, then valid workplace
status, then lowest `JobOffer` ID. The workplace check is retained as an
explicit ranking dimension even though invalid workplaces are excluded from
submission. Existing occupation compatibility is deferred until a reliable
concrete occupation model exists.

The current global person pass is deliberately deterministic and replaceable:
it avoids introducing a synthetic person-to-vacancy index before the concrete
relations are stable. Offer and person IDs are sorted before decisions.

## Vacancy wage terms

Automatic canonical factory vacancies first inherit the wage and pay period of
the lowest-ID active concrete contract for the same occupation. When no such
contract exists, the offer uses a factory-specific bootstrap of ten percent of
the firm agency's expected unit revenue, with a concrete floor of `1.0` and a
one-day pay period. This is a temporary concrete bootstrap, not a provincial
aggregate wage; it can later be replaced by an explicit firm wage policy.

## Tick order

The regular economy tick runs the following sequence:

1. canonical factory vacancies are posted;
2. eligible people submit at most one best application;
3. pending applications are matched into concrete employment contracts;
4. contracts and production continue through the existing economy flow.

Therefore a vacancy created by a canonical factory is visible to individual
search in the same tick. Matching remains responsible for openings, account
selection, and exact concrete contract creation.

Mutating provincial labor supply, demand satisfaction, or unemployment data
does not alter a selected concrete offer. Aggregate labor matching and
aggregate canonical payroll remain outside this v1 path.

## Deferred

Reliable occupation compatibility, richer worker preferences, indexed
person-to-offer discovery, offer renewal policy, and runtime integration tests
requiring a rebuilt game binary remain deferred.

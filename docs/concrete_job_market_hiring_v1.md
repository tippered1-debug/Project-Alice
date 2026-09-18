# Concrete Job Market & Hiring v1

Canonical factories create concrete `job_offer` records when Firm Agency's
desired production units exceed labor supplied by active employment contracts.
The vacancy count is the positive shortage after existing open offers, with
one v1 opening supplying one labor unit. Vacancy creation uses only concrete
factory capacity, concrete labor, the factory operator, and the exact payroll
account; provincial unemployment, labor availability, and aggregate wage
satisfaction are not inputs.

Each `job_application` names one persistent `person` and one `job_offer`.
Applications are indexed by offer and person. Matching processes offers in
stable JobOffer ID order and pending applications in stable JobApplication ID
order. An eligible living person with no active EmploymentContract is assigned
at most once per matching pass. Acceptance decrements openings and calls the
canonical EmploymentContract creation API. Applications that cannot pass
eligibility are rejected; applications left after an offer has no openings stay
pending and are reconsidered on later ticks.

At hiring, the worker's lowest-ID existing account in the offer settlement is
selected deterministically. If none exists, one person-owned account is opened.
The selected account is stored on the EmploymentContract and is the only wage
receiver used later. Matching itself creates no money or transaction.

`job_market::process` posts concrete factory vacancies and processes pending
applications during the regular economy tick. Offers can be closed, expired,
or given additional openings; this lets pending applications be reconsidered
later without aggregate labor clearing.

Deferred: job search, skills, education, negotiation, bargaining, migration,
households, commuting, unions, strikes, sickness, layoffs, benefits, taxes,
credit, remote work, and multi-job scheduling.

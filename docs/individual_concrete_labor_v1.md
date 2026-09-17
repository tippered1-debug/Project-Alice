# Individual Persons & Concrete Labor Market v1

This milestone makes the existing persistent `person` the canonical worker
identity. `economy::physical::concrete_labor` stores one persistent
`employment_contract` per person/employer/factory relationship and keeps the
factory-to-contract DCON relation as the iteration index.

An active contract supplies its positive `labor_capacity` each day. A dead,
future, ended, or terminated contract supplies zero. Canonical industrial
production sums these concrete capacities and no longer reads provincial labor
availability, employment satisfaction, or aggregate employment buckets.

`wage_rate` is the amount due per labor unit for the contract's full
`pay_period_days`; the current v1 daily due is:

```
wage_rate * labor_capacity / pay_period_days
```

Settlement uses the exact payer and worker accounts stored on the contract.
Available payer cash is the account balance less active concrete bid
reservations. Any unpaid current wage is added to a linked payroll obligation;
labor supply is not reduced when payment is short. A worker's other
same-currency accounts are never selected during settlement.

`contracts_for_factory`, `active_workers_for_factory`,
`labor_supplied_to_factory`, and `wage_due` provide the minimal query surface.
The old aggregate payroll path remains only as a compatibility path for
direct callers that have no employment contracts; it cannot make canonical
production happen without concrete labor.

Not included: households, consumption, migration, skills, job search,
absence/sickness/strike simulation, or aggregate labor-statistics redesign.

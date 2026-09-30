# Persons

`persons::person_key` is the stable identity for a logical human. It consists of a source population cell and an ordinal. The exact population store keeps compact cell descriptors and identity ranges rather than requiring a separate domain object for every identity.

The person kernel owns existence, alive state, birth-day index, current population membership, and canonical home site. The source cell records provenance; it does not determine a person's current location or occupation. Queries use the person kernel rather than inferring current state from a legacy POP row or a DCON profile mirror.

A DCON `person_id` is a sparse materialized profile used where an existing interface requires one. The person store keeps the key-to-profile mapping and its reverse mapping, validates that they are one-to-one, and treats profile fields as mirrors. Named or explicitly requested people can have profiles; profile allocation IDs do not define canonical identity.

Birth, death, and membership transfers are person-kernel lifecycle changes. Domain state remains with its owning system: exact economy owns person accounts and work relations; military owns formation assignments; government owns office tenure; household mobility owns household stock and relocation policy. These systems refer to a person by `person_key`.

Validation checks identity and membership coverage, live/dead state, profile mappings, and ownership references. Deterministic checksums use normalized canonical records, not DCON allocation order. Save contents and restore dependencies are described in [Serialization](../runtime/serialization.md).

The original identity-store milestones and their historical bootstrap/scale assumptions are preserved in [Archive](../archive/README.md).

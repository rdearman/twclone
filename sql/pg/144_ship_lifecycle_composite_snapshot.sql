-- V3-SHIP-02: expose one read-only lifecycle invariant contract.
--
-- The component snapshots remain useful for diagnosis.  This aggregate is the
-- restart/concurrency boundary: callers can assert that every lifecycle
-- invariant, including tow links, is zero without depending on a changing
-- collection of component views.
BEGIN;

CREATE OR REPLACE VIEW ship_lifecycle_composite_snapshot AS
SELECT
  (to_jsonb(l) || to_jsonb(c) || to_jsonb(r) || to_jsonb(a)
   || to_jsonb(s) || to_jsonb(o) || to_jsonb(so) || to_jsonb(t)) AS invariants,
  (
    COALESCE((SELECT sum(value::bigint) FROM jsonb_each_text(to_jsonb(l))), 0)
    + COALESCE((SELECT sum(value::bigint) FROM jsonb_each_text(to_jsonb(c))), 0)
    + COALESCE((SELECT sum(value::bigint) FROM jsonb_each_text(to_jsonb(r))), 0)
    + COALESCE((SELECT sum(value::bigint) FROM jsonb_each_text(to_jsonb(a))), 0)
    + COALESCE((SELECT sum(value::bigint) FROM jsonb_each_text(to_jsonb(s))), 0)
    + COALESCE((SELECT sum(value::bigint) FROM jsonb_each_text(to_jsonb(o))), 0)
    + COALESCE((SELECT sum(value::bigint) FROM jsonb_each_text(to_jsonb(so))), 0)
    + COALESCE((SELECT sum(value::bigint) FROM jsonb_each_text(to_jsonb(t))), 0)
  )::bigint AS violation_count
FROM ship_lifecycle_invariant_snapshot l
CROSS JOIN ship_cargo_invariant_snapshot c
CROSS JOIN ship_relationship_invariant_snapshot r
CROSS JOIN ship_active_reference_invariant_snapshot a
CROSS JOIN ship_lifecycle_state_snapshot s
CROSS JOIN ship_active_owner_invariant_snapshot o
CROSS JOIN ship_ownership_invariant_snapshot so
CROSS JOIN ship_tow_invariant_snapshot t;

COMMIT;

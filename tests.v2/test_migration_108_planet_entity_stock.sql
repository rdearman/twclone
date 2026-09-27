-- Isolated migration test. All objects are temporary and shadow any public
-- tables, so this script can be run against a database without changing it.
CREATE TEMP TABLE planettypes (
  planettypes_id integer PRIMARY KEY,
  maxore bigint NOT NULL,
  maxorganics bigint NOT NULL,
  maxequipment bigint NOT NULL
);

CREATE TEMP TABLE planets (
  planet_id integer PRIMARY KEY,
  type integer NOT NULL REFERENCES planettypes (planettypes_id),
  ore_on_hand integer,
  organics_on_hand integer,
  equipment_on_hand integer
);

CREATE TEMP TABLE planet_goods (
  planet_id integer NOT NULL REFERENCES planets (planet_id),
  commodity text NOT NULL,
  quantity integer NOT NULL DEFAULT 0,
  max_capacity bigint NOT NULL,
  production_rate bigint NOT NULL,
  PRIMARY KEY (planet_id, commodity),
  CONSTRAINT planet_goods_commodity_check
    CHECK (commodity IN ('ORE', 'ORG', 'EQU'))
);

CREATE TEMP TABLE entity_stock (
  entity_type text NOT NULL,
  entity_id bigint NOT NULL,
  commodity_code text NOT NULL,
  quantity integer NOT NULL,
  price integer,
  last_updated_ts integer NOT NULL,
  PRIMARY KEY (entity_type, entity_id, commodity_code)
);

INSERT INTO planettypes VALUES (1, 1000, 800, 600);
INSERT INTO planets VALUES
  (11, 1, 12, 34, 56),
  (12, 1, -4, 0, 0);
INSERT INTO planet_goods VALUES
  (11, 'ORE', 7, 700, 3),
  (11, 'ORG', -2, 600, 2);
INSERT INTO entity_stock
  (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts)
VALUES ('planet', 11, 'ORE', 3, 0, 1);

-- Apply twice to verify both the initial migration and its idempotence.
\ir ../sql/pg/108_planet_entity_stock.sql
\ir ../sql/pg/108_planet_entity_stock.sql

DO $test$
DECLARE
  actual integer;
BEGIN
  SELECT quantity INTO actual FROM entity_stock
   WHERE entity_type = 'planet' AND entity_id = 11 AND commodity_code = 'ORE';
  IF actual <> 22 THEN
    RAISE EXCEPTION 'ORE stock = %, expected 22 (existing 3 + legacy 12 + planet_goods 7)', actual;
  END IF;

  SELECT quantity INTO actual FROM entity_stock
   WHERE entity_type = 'planet' AND entity_id = 11 AND commodity_code = 'ORG';
  IF actual <> 32 THEN
    RAISE EXCEPTION 'ORG stock = %, expected 32 (legacy 34 + planet_goods -2)', actual;
  END IF;

  SELECT quantity INTO actual FROM entity_stock
   WHERE entity_type = 'planet' AND entity_id = 11 AND commodity_code = 'EQU';
  IF actual <> 56 THEN RAISE EXCEPTION 'EQU stock = %, expected 56', actual; END IF;

  SELECT quantity INTO actual FROM entity_stock
   WHERE entity_type = 'planet' AND entity_id = 12 AND commodity_code = 'ORE';
  IF actual <> -4 THEN RAISE EXCEPTION 'negative ORE balance = %, expected -4', actual; END IF;

  IF NOT EXISTS (
    SELECT 1 FROM planet_goods
     WHERE planet_id = 12 AND commodity = 'ORG'
       AND max_capacity = 800 AND quantity = 0
  ) THEN
    RAISE EXCEPTION 'missing class-based zero-balance ORG capacity row';
  END IF;

  IF EXISTS (
    SELECT 1 FROM planets
     WHERE COALESCE(ore_on_hand, 0) <> 0
        OR COALESCE(organics_on_hand, 0) <> 0
        OR COALESCE(equipment_on_hand, 0) <> 0
  ) THEN
    RAISE EXCEPTION 'legacy planet balances were not cleared';
  END IF;

  IF EXISTS (SELECT 1 FROM planet_goods WHERE COALESCE(quantity, 0) <> 0) THEN
    RAISE EXCEPTION 'legacy planet_goods balances were not cleared';
  END IF;
END
$test$;

\echo 'Migration 108 passed: balances preserved, capacities seeded, rerun idempotent.'

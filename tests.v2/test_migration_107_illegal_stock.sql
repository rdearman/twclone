-- Isolated migration test: all relations are temporary and shadow public data.
SET search_path TO pg_temp, public;

CREATE TEMP TABLE commodities (
  code text PRIMARY KEY,
  illegal boolean NOT NULL,
  base_price integer NOT NULL
);
CREATE TEMP TABLE ports (
  port_id integer PRIMARY KEY,
  sector_id integer NOT NULL,
  type integer NOT NULL,
  size integer NOT NULL
);
CREATE TEMP TABLE clusters (
  clusters_id integer PRIMARY KEY,
  alignment integer NOT NULL
);
CREATE TEMP TABLE cluster_sectors (
  cluster_id integer NOT NULL,
  sector_id integer NOT NULL,
  PRIMARY KEY (cluster_id, sector_id)
);
CREATE TEMP TABLE entity_stock (
  entity_type text NOT NULL,
  entity_id bigint NOT NULL,
  commodity_code text NOT NULL,
  quantity integer NOT NULL,
  price integer,
  PRIMARY KEY (entity_type, entity_id, commodity_code)
);

INSERT INTO commodities VALUES ('DRG', TRUE, 250), ('ORE', FALSE, 10);
INSERT INTO ports VALUES (1, 101, 10, 2), (2, 102, 1, 3), (3, 103, 10, 1);
INSERT INTO clusters VALUES (201, 500), (202, -500), (203, 0);
INSERT INTO cluster_sectors VALUES (201, 101), (202, 102), (203, 103);

-- Execute after clusters exist, as the Big Bang flow now does.
\ir ../sql/pg/107_seed_illegal_commodities.sql
CREATE TEMP TABLE first_seed AS
  SELECT entity_id, quantity FROM entity_stock ORDER BY entity_id;
\ir ../sql/pg/107_seed_illegal_commodities.sql

DO $test$
BEGIN
  IF EXISTS (SELECT 1 FROM entity_stock WHERE entity_id = 1) THEN
    RAISE EXCEPTION 'good-cluster black market received illegal stock';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM entity_stock WHERE entity_id = 2 AND entity_type = 'port') THEN
    RAISE EXCEPTION 'evil-cluster port did not receive illegal stock';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM entity_stock WHERE entity_id = 3 AND entity_type = 'port') THEN
    RAISE EXCEPTION 'neutral black market outside good cluster did not receive illegal stock';
  END IF;
  IF EXISTS (
    SELECT 1 FROM first_seed f
    FULL JOIN entity_stock es ON es.entity_id = f.entity_id
    WHERE es.entity_id IS NULL OR f.entity_id IS NULL OR es.quantity <> f.quantity
  ) THEN
    RAISE EXCEPTION 'rerunning illegal stock seed changed or duplicated existing stock';
  END IF;
END
$test$;

\echo 'Migration 107 passed: cluster targeting is correct and reruns do not duplicate stock.'

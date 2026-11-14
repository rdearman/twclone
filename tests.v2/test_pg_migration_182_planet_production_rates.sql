\set ON_ERROR_STOP on

-- Minimal isolated PostgreSQL schema; temporary tables shadow any public
-- objects in the selected database, and no persistent game data is touched.
CREATE TEMP TABLE planettypes (
  planettypes_id integer PRIMARY KEY,
  code text UNIQUE
);
CREATE TEMP TABLE commodities (code text PRIMARY KEY);
CREATE TEMP TABLE planet_production (
  planet_type_id integer NOT NULL,
  commodity_code text NOT NULL,
  base_prod_rate integer NOT NULL,
  base_cons_rate integer NOT NULL,
  PRIMARY KEY (planet_type_id, commodity_code)
);
CREATE TEMP TABLE planets (
  planet_id integer PRIMARY KEY,
  type integer NOT NULL
);

INSERT INTO planettypes VALUES (1,'M'),(2,'L'),(3,'O'),(4,'K'),
                               (5,'H'),(6,'C'),(7,'U');
INSERT INTO commodities VALUES ('ORE'),('ORG'),('EQU');
INSERT INTO planets VALUES (1,1),(2,2),(3,3),(4,4),(5,5),(6,6),(7,7);
INSERT INTO planet_production VALUES (1,'ORE',0,0);

\ir ../sql/pg/182_v3_planet_production_rates.sql

DO $check$
BEGIN
  IF (SELECT COUNT(*) FROM planet_production) <> 21 THEN
    RAISE EXCEPTION 'expected 21 class/commodity production rows';
  END IF;
  IF (SELECT workers_per_unit FROM planet_production
      WHERE planet_type_id=1 AND commodity_code='ORE') <> 100
     OR (SELECT workers_per_unit FROM planet_production
      WHERE planet_type_id=2 AND commodity_code='ORE') <> 50
     OR (SELECT workers_per_unit FROM planet_production
      WHERE planet_type_id=3 AND commodity_code='ORG') <> 50
     OR (SELECT workers_per_unit FROM planet_production
      WHERE planet_type_id=7 AND commodity_code='EQU') <> 0 THEN
    RAISE EXCEPTION 'versioned default rates were not applied';
  END IF;
  IF (SELECT COUNT(*) FROM planets WHERE ore_production_remainder=0
       AND organics_production_remainder=0
       AND equipment_production_remainder=0) <> 7 THEN
    RAISE EXCEPTION 'existing planets lack initialized labor remainders';
  END IF;
END
$check$;

UPDATE planet_production SET workers_per_unit=77
WHERE planet_type_id=1 AND commodity_code='ORE';

\ir ../sql/pg/182_v3_planet_production_rates.sql

DO $replay$
BEGIN
  IF (SELECT workers_per_unit FROM planet_production
      WHERE planet_type_id=1 AND commodity_code='ORE') <> 77
     OR (SELECT COUNT(*) FROM planet_production) <> 21 THEN
    RAISE EXCEPTION 'migration replay changed values or duplicated rows';
  END IF;
END
$replay$;

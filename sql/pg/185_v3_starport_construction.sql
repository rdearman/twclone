BEGIN;

ALTER TABLE port_trade
    ADD COLUMN IF NOT EXISTS capacity_level integer NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS v3_port_economy_rules (
    rule_key text PRIMARY KEY,
    rule_version integer NOT NULL,
    value bigint NOT NULL CHECK (value >= 0),
    description text NOT NULL,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO v3_port_economy_rules(rule_key,rule_version,value,description) VALUES
 ('build_credits',1,100000,'Noncanonical V3 initial StarPort credit cost'),
 ('initial_materials_each',1,1000,'Noncanonical V3 ORE, ORG and EQU required when construction starts'),
 ('delivery_amount_each',1,100,'Noncanonical V3 ORE, ORG and EQU required for each scheduled delivery'),
 ('delivery_count',1,3,'Noncanonical V3 scheduled deliveries before completion'),
 ('delivery_interval_seconds',1,86400,'Noncanonical V3 interval between construction deliveries'),
 ('upgrade_credits_per_level',1,10000,'Noncanonical V3 commodity capacity upgrade credit cost'),
 ('upgrade_material_per_level',1,100,'Noncanonical V3 selected commodity material cost per upgrade level'),
 ('upgrade_capacity_per_level',1,1000,'Noncanonical V3 capacity increase per commodity level'),
 ('upgrade_max_level',1,10,'Noncanonical V3 maximum commodity capacity upgrade level')
ON CONFLICT(rule_key) DO NOTHING;

CREATE TABLE IF NOT EXISTS planet_port_constructions (
    construction_id bigserial PRIMARY KEY,
    request_id text NOT NULL,
    player_id integer NOT NULL REFERENCES players(player_id),
    planet_id integer NOT NULL REFERENCES planets(planet_id),
    sector_id integer NOT NULL REFERENCES sectors(sector_id),
    port_class integer NOT NULL CHECK (port_class BETWEEN 1 AND 8),
    deliveries_completed integer NOT NULL DEFAULT 0,
    deliveries_required integer NOT NULL,
    delivery_amount integer NOT NULL,
    delivery_interval_seconds integer NOT NULL,
    next_delivery_at timestamptz NOT NULL,
    status text NOT NULL CHECK (status IN ('building','completed','cancelled')),
    port_id integer REFERENCES ports(port_id),
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at timestamptz,
    UNIQUE(player_id, request_id)
);

CREATE UNIQUE INDEX IF NOT EXISTS planet_port_constructions_one_active_sector
    ON planet_port_constructions(sector_id) WHERE status='building';
CREATE INDEX IF NOT EXISTS planet_port_constructions_due
    ON planet_port_constructions(next_delivery_at) WHERE status='building';

INSERT INTO cron_tasks(name,schedule,next_due_at,enabled)
VALUES('port_construction','every:60s',CURRENT_TIMESTAMP,TRUE)
ON CONFLICT(name) DO NOTHING;

COMMIT;

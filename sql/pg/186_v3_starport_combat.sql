BEGIN;

ALTER TABLE ports
    ADD COLUMN IF NOT EXISTS durability integer NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS defensive_strength integer NOT NULL DEFAULT 0;

UPDATE ports SET
    durability=COALESCE((SELECT value::integer FROM v3_port_economy_rules WHERE rule_key='port_durability'),10000),
    defensive_strength=COALESCE((SELECT value::integer FROM v3_port_economy_rules WHERE rule_key='port_defensive_strength'),1000)
WHERE is_active=TRUE AND durability=0 AND defensive_strength=0;

INSERT INTO v3_port_economy_rules(rule_key,rule_version,value,description) VALUES
 ('port_durability',1,10000,'Noncanonical V3 StarPort durability'),
 ('port_defensive_strength',1,1000,'Noncanonical V3 StarPort retaliation strength'),
 ('port_attack_turn_cost',1,1,'Noncanonical V3 turns consumed per StarPort attack')
ON CONFLICT(rule_key) DO NOTHING;

CREATE TABLE IF NOT EXISTS v3_port_action_receipts (
    player_id integer NOT NULL REFERENCES players(player_id),
    request_id text NOT NULL,
    action text NOT NULL CHECK (action IN ('upgrade','attack')),
    payload jsonb NOT NULL,
    result jsonb NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY(player_id,request_id)
);

COMMIT;

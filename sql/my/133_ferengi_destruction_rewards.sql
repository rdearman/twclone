ALTER TABLE ships ADD COLUMN IF NOT EXISTS destruction_sequence INT NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS ferengi_destruction_rewards (
  ship_id INT NOT NULL,
  destruction_sequence INT NOT NULL,
  recipient_player_id INT NOT NULL,
  amount BIGINT NOT NULL,
  source VARCHAR(32) NOT NULL,
  event_id BIGINT NOT NULL AUTO_INCREMENT UNIQUE,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_ferengi_reward_ship FOREIGN KEY (ship_id) REFERENCES ships(ship_id),
  CONSTRAINT fk_ferengi_reward_player FOREIGN KEY (recipient_player_id) REFERENCES players(player_id),
  CONSTRAINT ck_ferengi_reward_amount CHECK (amount = 5000),
  CONSTRAINT ck_ferengi_reward_source CHECK (source = 'system_generated'),
  CONSTRAINT ck_ferengi_reward_sequence CHECK (destruction_sequence > 0),
  PRIMARY KEY (ship_id, destruction_sequence)
);

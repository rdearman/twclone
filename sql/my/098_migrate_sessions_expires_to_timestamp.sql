-- Migration: Convert sessions.expires from bigint to TIMESTAMP
-- This aligns the live database with the schema definition in 000_tables.sql
-- which expects TIMESTAMP for proper timestamp handling.

START TRANSACTION;

-- MySQL: Convert epoch seconds to TIMESTAMP
ALTER TABLE sessions 
  MODIFY expires TIMESTAMP DEFAULT CURRENT_TIMESTAMP;

-- Update existing rows: convert epoch to timestamp
UPDATE sessions 
  SET expires = FROM_UNIXTIME(expires) 
  WHERE expires IS NOT NULL;

COMMIT;

-- V3 illegal-goods access policy.
INSERT INTO config (`key`, value, type)
VALUES ('illegal_allowed_neutral', '1', 'bool')
ON DUPLICATE KEY UPDATE `key` = `key`;

INSERT INTO config (`key`, value, type)
VALUES ('illegal_min_experience', '400', 'int')
ON DUPLICATE KEY UPDATE `key` = `key`;

-- ZoneSort event store schema (MySQL 8.4)
-- Run once as root in HeidiSQL (or `mysql -u root -p < cloud/schema.sql`).
-- MQTT topics and payloads that fill these tables: see cloud/README.md.

CREATE DATABASE IF NOT EXISTS zonesort CHARACTER SET utf8mb4;

-- app account: used by the subscriber, ML and dashboard
CREATE USER IF NOT EXISTS 'zonesort'@'%' IDENTIFIED BY 'pick-a-password';
GRANT ALL PRIVILEGES ON zonesort.* TO 'zonesort'@'%';

-- read-only account for teammates browsing the data
CREATE USER IF NOT EXISTS 'viewer'@'%' IDENTIFIED BY 'another-password';
GRANT SELECT ON zonesort.* TO 'viewer'@'%';

FLUSH PRIVILEGES;

USE zonesort;

-- One row per parcel. arrival, scan and sorted events upsert the same row
-- on (session, seq). All timestamps are stamped by the subscriber on receipt.
CREATE TABLE IF NOT EXISTS parcels (
  id          INT AUTO_INCREMENT PRIMARY KEY,
  session     VARCHAR(32) NOT NULL,     -- set by subscriber; new one each time Node 1 boots
  seq         INT NOT NULL,             -- Node 1 arrival number, restarts at 1 on boot
  parcel_id   VARCHAR(32),              -- from label; NULL if unreadable
  block       VARCHAR(8),
  floor       TINYINT,
  unit        VARCHAR(8),
  scan        VARCHAR(16),              -- ok / unreadable / unknown
  flips       TINYINT,                  -- flip commands sent before the final scan
  zone        VARCHAR(8),               -- gateway's decision: A / B / MANUAL
  routed      VARCHAR(8),               -- gate Node 2 actually fired: A / B / MANUAL
  reason      VARCHAR(16),              -- why routed differs from zone, e.g. timeout
  result      VARCHAR(16),              -- confirmed / missed
  arrived_at  DATETIME(3),
  scanned_at  DATETIME(3),
  sorted_at   DATETIME(3),
  UNIQUE KEY uq_session_seq (session, seq),
  KEY idx_block_time (block, arrived_at)
);

CREATE TABLE IF NOT EXISTS faults (
  id      INT AUTO_INCREMENT PRIMARY KEY,
  session VARCHAR(32),
  source  VARCHAR(16) NOT NULL,         -- node1 / node2 / gateway
  type    VARCHAR(32) NOT NULL,         -- jam / missed_ir / scan_fail_repeat / ...
  seq     INT,                          -- which parcel, if applicable
  detail  VARCHAR(255),
  at      DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
);

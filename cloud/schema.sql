-- ZoneSort event store schema (MySQL 8.4)
-- Run once as root in HeidiSQL (or `mysql -u root -p < cloud/schema.sql`).

CREATE DATABASE IF NOT EXISTS zonesort CHARACTER SET utf8mb4;

-- app account: used by the subscriber, ML and dashboard
CREATE USER IF NOT EXISTS 'zonesort'@'%' IDENTIFIED BY 'pick-a-password';
GRANT ALL PRIVILEGES ON zonesort.* TO 'zonesort'@'%';

-- read-only account for teammates browsing the data
CREATE USER IF NOT EXISTS 'viewer'@'%' IDENTIFIED BY 'another-password';
GRANT SELECT ON zonesort.* TO 'viewer'@'%';

FLUSH PRIVILEGES;

USE zonesort;

CREATE TABLE IF NOT EXISTS parcels (
  id          INT AUTO_INCREMENT PRIMARY KEY,
  session     VARCHAR(32) NOT NULL,     -- one prototype run = one "delivery day"
  seq         INT NOT NULL,             -- Node 1 arrival number
  parcel_id   VARCHAR(32),
  block       VARCHAR(8),
  floor       TINYINT,
  zone        VARCHAR(8),               -- A / B / MANUAL
  scan        VARCHAR(16),              -- ok / no_label / unreadable / unknown_block
  result      VARCHAR(16),              -- confirmed / missed
  reason      VARCHAR(16),              -- e.g. timeout
  scanned_at  DATETIME(3),
  sorted_at   DATETIME(3),
  UNIQUE KEY uq_session_seq (session, seq),
  KEY idx_block_time (block, sorted_at)
);

CREATE TABLE IF NOT EXISTS faults (
  id      INT AUTO_INCREMENT PRIMARY KEY,
  session VARCHAR(32),
  type    VARCHAR(32) NOT NULL,         -- jam / missed_ir / ...
  seq     INT,
  detail  VARCHAR(255),
  at      DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
);

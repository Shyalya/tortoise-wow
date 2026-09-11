-- Cached Eversong Wilds (Capybara Paradise) auction-house prices for the resident
-- buy feature, populated off-server by tools/wa_market_refresh.py. Prices in copper.
CREATE TABLE IF NOT EXISTS mod_turtlebots_market (
  item_entry INT UNSIGNED NOT NULL PRIMARY KEY,
  avg_price  INT UNSIGNED NOT NULL DEFAULT 0,
  min_buyout INT UNSIGNED NOT NULL DEFAULT 0,
  available  INT UNSIGNED NOT NULL DEFAULT 0,
  scanned_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);
-- Items the bot was asked about but has no price for yet; the refresher fetches these.
CREATE TABLE IF NOT EXISTS mod_turtlebots_market_want (
  item_entry   INT UNSIGNED NOT NULL PRIMARY KEY,
  requested_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

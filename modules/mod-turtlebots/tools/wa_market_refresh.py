#!/usr/bin/env python3
# Refresh mod_turtlebots_market from wowauctions.net (Capybara Paradise / Eversong Wilds).
# Runs off the game server (cron). Only fetches items the bot actually asked about
# (mod_turtlebots_market_want) plus stale rows, politely rate-limited.
import subprocess, re, json, time, sys

DB = "tw_char"
MYSQL = ["mysql", "-umangos", "-pmangos", "-N", "-B", DB]
UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/120 Safari/537.36"
SERVER_ID = 21      # Capybara Paradise
REALM_ID = 6544     # Eversong Wilds
MAX_PER_RUN = 40
STALE_HOURS = 24
SLEEP = 1.5

def q(sql):
    p = subprocess.run(MYSQL, input=sql, capture_output=True, text=True)
    return p.stdout

def ints(rows):
    return [int(x) for x in rows.split() if x.strip().isdigit()]

def fetch_prices(entry):
    url = "https://www.wowauctions.net/item/%d" % entry
    p = subprocess.run(["curl", "-sL", "-A", UA, url], capture_output=True, text=True, timeout=40)
    m = re.search(r'<script id="__NEXT_DATA__" type="application/json">(.*?)</script>', p.stdout, re.S)
    if not m:
        return None
    pp = json.loads(m.group(1))["props"]["pageProps"]
    for s in pp.get("itemPrices", []) or []:
        if s.get("server_id") == SERVER_ID:
            for r in s.get("realms", []) or []:
                if r.get("realm_id") == REALM_ID:
                    return (r.get("merged") or {}).get("prices")
    return None

def upsert(entry, pr):
    avg = int((pr or {}).get("avg_price") or 0)
    mb = int((pr or {}).get("minimum_buyout") or 0)
    av = int((pr or {}).get("available") or 0)
    q("INSERT INTO mod_turtlebots_market (item_entry,avg_price,min_buyout,available,scanned_at) "
      "VALUES (%d,%d,%d,%d,NOW()) ON DUPLICATE KEY UPDATE avg_price=%d,min_buyout=%d,available=%d,scanned_at=NOW()"
      % (entry, avg, mb, av, avg, mb, av))
    q("DELETE FROM mod_turtlebots_market_want WHERE item_entry=%d" % entry)
    return avg, mb, av

def main():
    todo = ints(q("SELECT item_entry FROM mod_turtlebots_market_want ORDER BY requested_at LIMIT %d" % MAX_PER_RUN))
    if len(todo) < MAX_PER_RUN:
        todo += ints(q("SELECT item_entry FROM mod_turtlebots_market WHERE scanned_at < NOW() - INTERVAL %d HOUR "
                       "ORDER BY scanned_at LIMIT %d" % (STALE_HOURS, MAX_PER_RUN - len(todo))))
    seen = set()
    n = 0
    for e in todo:
        if e in seen:
            continue
        seen.add(e)
        try:
            pr = fetch_prices(e)
        except Exception as ex:
            sys.stderr.write("entry %d: %s\n" % (e, ex))
            continue
        avg, mb, av = upsert(e, pr)
        n += 1
        print("entry %d -> avg=%d min_buyout=%d available=%d" % (e, avg, mb, av))
        time.sleep(SLEEP)
    print("done, %d items refreshed" % n)

if __name__ == "__main__":
    main()

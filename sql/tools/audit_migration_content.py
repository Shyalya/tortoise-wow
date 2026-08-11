"""Checks what the migration files insert against what the database holds.

The `migrations` table cannot answer this. Ours arrived with a complete dump and
carries that dump's bookkeeping, so it claims files were applied that this data
never received - verified on 2026-08-10, where 20260518050203_world sat in the
table with a real hash and timestamp while none of its 349 creature templates
existed. The content itself has to be compared.

Reads every `*_world.sql` in sql/database_updates, collects the first column of
each tuple per table, and counts how many of those ids exist. Prints only the
shortfalls.

Note on the file format: a statement is spread over several lines - the table
name, then the column list, then VALUES alone on its own line, then the tuples.
Ids are queried in batches; a single IN list of all of them is longer than the
command line allows.
"""
import io
import os
import re
import subprocess
import sys

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'database_updates')
CONF = '/home/shyalya/tortoise-playerbots/build/src/mangosd/mangosd.conf'
TABLES = ['creature_template', 'quest_template', 'gameobject_template', 'item_template']

H, P, U, PW, D = [l for l in io.open(CONF, encoding='utf8', errors='replace')
                  if l.startswith('WorldDatabase.Info')][0].split('"')[1].split(';')


def query(sql):
    r = subprocess.run(['mysql', '-h', H, '-P', P, '-u', U, '-p' + PW, D, '-N', '-B', '-e', sql],
                       capture_output=True, text=True)
    return r.stdout.strip()


INS = re.compile(r'(?:INSERT|REPLACE)\s+(?:IGNORE\s+)?INTO\s+`?(\w+)`?', re.I)
TUP = re.compile(r'^\((\d+)\s*,')

rows = []
for name in sorted(os.listdir(BASE)):
    if not name.endswith('_world.sql'):
        continue

    found = {t: set() for t in TABLES}
    current = None
    for line in io.open(os.path.join(BASE, name), encoding='utf8', errors='replace'):
        m = INS.search(line)
        if m:
            current = m.group(1) if m.group(1) in TABLES else None
        if current:
            t = TUP.match(line.strip())
            if t:
                found[current].add(int(t.group(1)))
            if line.rstrip().endswith(';'):
                current = None

    for tbl in TABLES:
        ids = sorted(found[tbl])
        if not ids:
            continue
        got = 0
        for k in range(0, len(ids), 400):
            part = ids[k:k + 400]
            got += int(query("SELECT COUNT(*) FROM `%s` WHERE entry IN (%s)"
                             % (tbl, ','.join(str(i) for i in part))) or 0)
        if got < len(ids):
            rows.append((name.replace('_world.sql', ''), tbl, len(ids), got))

if not rows:
    print('nothing missing')
    sys.exit()

print('%-26s %-22s %9s %8s %8s' % ('File', 'Table', 'in file', 'in db', 'missing'))
total = 0
for name, tbl, want, got in rows:
    total += want - got
    print('%-26s %-22s %9d %8d %8d' % (name, tbl, want, got, want - got))
print('\n%d entries missing across %d file/table pairs' % (total, len(rows)))

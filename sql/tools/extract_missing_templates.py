"""Pulls only the missing template rows out of the migration files.

Applying those files whole does not work here. They insert spawns with fixed
guids, and upstream reuses the same guid blocks across migrations - on
2026-08-10, 9794 of them collided with rows this database already had. The
tables are MyISAM, so a duplicate key aborts the file halfway and leaves the
rest unapplied with nothing to roll back.

The collisions are also the answer: the spawns are already here. What was
missing were the templates behind them - 327 creatures stood in the world with
no `creature_template` row of their own. This writes a file containing just
those, keeping each source file's own column list, and touching no guid at all.

Writes /tmp/missing_templates.sql. Read it, then apply it. Takes effect on the
next server start.
"""
import io
import os
import re
import subprocess

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'database_updates')
CONF = '/home/shyalya/tortoise-playerbots/build/src/mangosd/mangosd.conf'
OUT = '/tmp/missing_templates.sql'

H, P, U, PW, D = [l for l in io.open(CONF, encoding='utf8', errors='replace')
                  if l.startswith('WorldDatabase.Info')][0].split('"')[1].split(';')


def query(sql):
    r = subprocess.run(['mysql', '-h', H, '-P', P, '-u', U, '-p' + PW, D, '-N', '-B', '-e', sql],
                       capture_output=True, text=True)
    return r.stdout.strip()


# INSERT INTO `table`, then the column list over many lines, then VALUES alone.
HEAD = re.compile(r'^\s*INSERT\s+(?:IGNORE\s+)?INTO\s+`(creature_template|gameobject_template)`\s*$', re.I)
TUP = re.compile(r'^\((\d+)\s*,')

best = {}                                   # a later file wins on a repeated id
for name in sorted(f for f in os.listdir(BASE) if f.endswith('_world.sql')):
    table = cols = None
    header, in_header = [], False
    for line in io.open(os.path.join(BASE, name), encoding='utf8', errors='replace'):
        m = HEAD.match(line)
        if m:
            table, in_header, header = m.group(1), True, []
            continue
        if in_header:
            if line.strip().upper() == 'VALUES':
                cols = ' '.join(x.strip() for x in header).strip()
                in_header = False
            else:
                header.append(line)
            continue
        if table and cols:
            t = TUP.match(line.strip())
            if t:
                best[(table, int(t.group(1)))] = (cols, line.strip().rstrip(';').rstrip(','))
            if line.rstrip().endswith(';'):
                table = cols = None

groups = {}
for (table, eid), value in best.items():
    groups.setdefault(table, {})[eid] = value

statements = []
for table, items in sorted(groups.items()):
    ids = sorted(items)
    missing = []
    for k in range(0, len(ids), 400):
        part = ids[k:k + 400]
        have = query("SELECT entry FROM `%s` WHERE entry IN (%s)"
                     % (table, ','.join(map(str, part))))
        have = {int(x) for x in have.split() if x.isdigit()}
        missing += [i for i in part if i not in have]

    print('%-22s %d in the files, %d missing' % (table, len(ids), len(missing)))

    by_cols = {}
    for i in missing:
        cols, body = items[i]
        by_cols.setdefault(cols, []).append(body)
    for cols, bodies in by_cols.items():
        statements.append('INSERT INTO `%s` %s VALUES\n%s;\n' % (table, cols, ',\n'.join(bodies)))

io.open(OUT, 'w', encoding='utf8', newline='\n').write('\n'.join(statements))
print('\nwrote %s (%d statements, %.1f KB)' % (OUT, len(statements), os.path.getsize(OUT) / 1024))

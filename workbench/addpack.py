# addpack.py <pack> <menu set>: add a menu pass's textures to a pack. New ones are copied in with their rows; ones
# the pack has already get the menu pass's levels added to theirs (menus: "fe<n>", pause walks: "pz_<level>").
import csv, os, shutil, sys
pack, extra = sys.argv[1], sys.argv[2]
rows = {r['name']: r for r in csv.DictReader(open(f'{pack}/index.csv'))}
added = {'surface': 0, 'other': 0}; seen = 0
for r in csv.DictReader(open(f'{extra}/index.csv')):
    n = r['name']
    if n in rows:
        o = rows[n]; seen += 1
        o['levels'] = ' '.join(sorted(set(o['levels'].split()) | set(r['levels'].split())))
        o['drawn'] = str(int(o['drawn']) | int(r['drawn'])); o['tile'] = str(int(o['tile']) | int(r['tile']))
        if o['tile'] == '1' and r['kind'] == 'surface': o['kind'] = 'surface'
    else:
        shutil.copy(f'{extra}/textures/{n}', f'{pack}/textures/{n}'); rows[n] = r; added[r['kind']] += 1
with open(f'{pack}/index.csv', 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=['name', 'width', 'height', 'format', 'drawn', 'tile', 'kind', 'levels'])
    w.writeheader(); [w.writerow(rows[n]) for n in sorted(rows)]
k = sum(1 for r in rows.values() if r['kind'] == 'surface')
print(f'{pack}: {sum(added.values())} new ({added["surface"]} surfaces, {added["other"]} other), {seen} already there; '
      f'now {len(rows)} ({k} surfaces, {len(rows) - k} other)')

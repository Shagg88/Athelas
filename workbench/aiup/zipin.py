# zipin.py <game>: the game's prepared inputs as zips of at most ~24 MB each (stored, PNG is compressed already)
import os, sys, zipfile
g = sys.argv[1]; src = f'/home/claude/work/aiup/in/{g}'; dst = '/mnt/user-data/outputs/aiup/inputs'; os.makedirs(dst, exist_ok=True)
files = sorted(os.listdir(src)); files.remove('manifest.csv'); files = ['manifest.csv'] + files
part, size, z = 0, 0, None
for f in files:
    s = os.path.getsize(f'{src}/{f}')
    if z is None or size + s > 24_000_000:
        if z: z.close()
        part += 1; size = 0; z = zipfile.ZipFile(f'{dst}/{g}_{part:02d}.zip', 'w', zipfile.ZIP_STORED)
    z.write(f'{src}/{f}', f); size += s
z.close(); print(g, part, 'zips')

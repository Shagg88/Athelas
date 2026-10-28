# menuwalk.py <seed> <frames> [pause]: a pad script that wanders the menus at random (seeded), for TEXPACK dumps.
# Front end (default): past the title, then directions / A / B / X / Y. "pause": in a level, the pause menu opened
# every so often, wandered, closed, then a little play in between (inventory, journal, upgrade screens).
import random, sys
seed, frames = int(sys.argv[1]), int(sys.argv[2])
pause = len(sys.argv) > 3 and sys.argv[3] == 'pause'
r = random.Random(seed)
out = []
f = 300
for _ in range(8):  # logos and title: START and A
    out.append(f'{f} {r.choice(["START", "A"])} 6'); f += 40
moves = ['UP'] * 6 + ['DOWN'] * 8 + ['LEFT'] * 3 + ['RIGHT'] * 3 + ['A'] * 8 + ['B'] * 4 + ['X', 'Y', 'BLACK', 'WHITE']
if not pause: moves += ['START']
while f < frames:
    if pause:
        out.append(f'{f} START 6'); f += 30
        for _ in range(r.randint(6, 20)):
            out.append(f'{f} {r.choice(moves)} 5'); f += r.randint(18, 40)
        out.append(f'{f} B 5'); f += 25; out.append(f'{f} START 6'); f += 30
        for _ in range(r.randint(2, 5)):  # play a little: walk, attack, the back button (map / stats in some)
            b = r.choice(['LS 60', 'LSL 40', 'LSR 40', 'X 6', 'Y 6', 'A 6', 'BACK 6'])
            out.append(f'{f} {b}'); f += 70
    else:
        out.append(f'{f} {r.choice(moves)} 5'); f += r.randint(18, 45)
        if r.random() < 0.04:  # back right out now and then, so other branches of the top menu get visited
            for _ in range(4): out.append(f'{f} B 5'); f += 25
print('\n'.join(out))

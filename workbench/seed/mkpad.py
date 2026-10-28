# mkpad.py GAME SECONDS > pad: menus first, then a loop of moving, attacking, using things and pausing
import sys
g, T = sys.argv[1], float(sys.argv[2])
lines = []
t = 3.0
start = {'tt': ['START','START','A','START','A','START','A'], 'rx': ['START','START','A','START','A','START','A'],
         'fx': ['START','START','A','A','START','A','A','B']}[g]
for b in start: lines.append(f'{t:.1f} {b}'); t += 2.0
cycle = [('LS',1.5),('X',0.2),('X',0.2),('Y',0.2),('LSL',0.8),('A',0.2),('LS',1.0),('B',0.2),('RT',0.8),('X',0.2),
         ('LSR',0.8),('Y',0.2),('LT',0.6),('WHITE',0.2),('LSD',1.0),('BLACK',0.2),('X+Y',0.2)]
n = 0
while t < T - 4:
    b, d = cycle[n % len(cycle)]; lines.append(f'{t:.1f} {b} {d}'); t += d + 0.6; n += 1
    if n % 40 == 0:  # the pause menu and back
        lines.append(f'{t:.1f} START'); t += 2.5; lines.append(f'{t:.1f} START'); t += 2.0
print('\n'.join(lines))

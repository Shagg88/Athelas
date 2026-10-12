"""From the launcher's ROTKFix.ini layout (launcher/src/config.cpp ini_layout):
 1. the DLL's default ROTKFix.ini (src/rotkfix.c, compact aligned form)
 2. the 'ROTKFix.ini settings' section of dist-files/README.txt"""
import re, os
R = os.path.join(os.path.dirname(__file__), '..')
src = open(os.path.join(R, 'launcher/src/config.cpp')).read()
lay = src[src.index('static const std::vector<IniSection> L = {'):src.index('    return L;')]
secre = re.compile(r'\{ "(\w+)", "([^"]*)", \{(.*?)\} \},', re.S)
keyre = re.compile(r'\{ "(\w+)", (nullptr|"[^"]*"), (nullptr|"(?:[^"\\]|\\.)*") \}')
secs = []
for m in secre.finditer(lay):
    name, title, body = m.groups()
    keys = []
    for k in keyre.finditer(body):
        key, d, c = k.groups()
        d = None if d == 'nullptr' else d[1:-1]
        c = None if c == 'nullptr' else c[1:-1].replace('\\r\\n;   ', ' ').replace('\\"', '"').replace('\\\\', '\\')
        keys.append((key, d, c))
    secs.append((name, title, keys))
# 1. DLL template
out = ['; ROTK Fix settings - easiest to change with ROTKFixLauncher.exe.',
       '; Edits made here are kept. README.txt explains every setting.']
for name, title, keys in secs:
    rows = [(k, d) for k, d, c in keys if d is not None]
    w = max(len(k) for k, _ in rows)
    out += ['', '[%s]' % name] + ['%s%s= %s' % (k, ' ' * (w - len(k) + 1), d) for k, d in rows]
text = '\n'.join(out) + '\n'
cs = '            fputs(' + '\n'.join('                  "%s\\n"' % l.replace('\\', '\\\\').replace('"', '\\"') for l in text.rstrip('\n').split('\n')).lstrip() + ', f);'
p = os.path.join(R, 'src/rotkfix.c'); r = open(p).read()
a = r.index('            fputs("; ROTK Fix settings'); b = r.index(', f);', a) + len(', f);')
open(p, 'w').write(r[:a] + cs + r[b:])
# 2. README section
doc = ['ROTKFIX.INI SETTINGS', '--------------------',
       'The launcher writes ROTKFix.ini in the game folder. You can also edit it in Notepad.', '']
for name, title, keys in secs:
    doc.append('[%s]  %s' % (name, title))
    for k, d, c in keys:
        line = '  %-22s %s' % (k, c or '')
        if d is not None: line += ('' if not c else ' ') + '(default %s)' % d
        doc.append(line.rstrip())
    doc.append('')
p = os.path.join(R, 'dist-files/README.txt'); r = open(p).read()
start = r.find('ROTKFIX.INI SETTINGS')
block = '\n'.join(doc) + '\n'
if start >= 0:
    end = r.find('\n\n\n', start)
    r = r[:start] + block.rstrip('\n') + (r[end:] if end >= 0 else '\n')
else:
    i = r.find('INCLUDED OPEN-SOURCE SOFTWARE')
    r = r[:i] + block + '\n' + r[i:]
open(p, 'w').write(r)
print('ok')

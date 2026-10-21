#!/usr/bin/env python3
"""check_lang_mix.py <lang.txt>...: which language each block of 100 lines reads as (common words), to catch a
block from another language's file."""
import re, sys
W = {'de': 'und der die das ist nicht ich du sie wir ein eine zu mit auf für den dem',
     'fr': 'et le la les est pas je tu vous nous un une de du des pour avec que',
     'es': 'y el la los las es no yo tú usted un una de del para con que por',
     'it': 'e il la gli le è non io tu un una di del per con che sono',
     'pt': 'e o a os as é não eu você um uma de do da para com que',
     'pl': 'i w na nie jest to się że z do jak ale tak',
     'nl': 'en de het een is niet ik je jij wij van voor met dat op',
     'sv': 'och en ett är inte jag du vi att det för med på som',
     'en': 'and the is not you we a an of for with that to it'}
W = {k: set(v.split()) for k, v in W.items()}
for fn in sys.argv[1:]:
    lines = [l.split('\t', 1)[1] for l in open(fn, encoding='utf-8') if '\t' in l and not l.startswith('#')]
    out = []
    for b in range(0, len(lines), 100):
        words = re.findall(r"[^\W\d_]+", ' '.join(lines[b:b + 100]).lower())
        if re.search('[а-яё]', ' '.join(words)): out.append('ru'); continue
        score = {k: sum(w in v for w in words) for k, v in W.items()}
        out.append(max(score, key=score.get))
    print(fn.split('/')[-1], ' '.join(out))

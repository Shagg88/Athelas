#!/bin/bash
# every level headless for 110 s with videos skippable; summary in runs/sweep/summary.txt
cd /home/claude/rotkx/work
out=runs/sweep; mkdir -p $out
for L in Pat03 Min02 Pel02 Bla01 Ise01 Min00 Min04 Osg01 She01 Cir00 Cra01 Cir01 Pat02 Pal01 Pal02; do
  d=$out/$L; rm -rf $d; mkdir -p $d/1
  printf '[graphics]\nskip_all_videos = true\n' > $d/1/settings.toml
  ROTK_LEVEL=$L REF=1200 bash ../rotkxbox/sdk/harness/run.sh -x hbuild/rotkx_headless -g ../gamedata -o $d -t 110 -s pad_sweep.txt > $d/run.txt 2>&1
  last=$(grep -h "renderer: frame" $d/1/logs/*.log | tail -1 | grep -o "frame [0-9]*, [0-9]* draws")
  scx=$(grep -h "open d:.*Game" $d/1/logs/*.log | tail -1 | grep -o '[A-Za-z0-9]*\.scx')
  warn=$(grep -h "\[warn\]\|\[error\]" $d/1/logs/*.log | grep -v "wbinvd\|exports running" | sed 's/^\[[^]]*\] //' | sort | uniq -c | sort -rn | head -5 | tr '\n' ';')
  echo "$L | $scx | $last | $warn" >> $out/summary.txt
done
echo done >> $out/summary.txt

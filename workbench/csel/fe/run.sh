run () 
{ 
    N=$1;
    S=$2;
    T=$3;
    rm -rf $N;
    mkdir -p $N;
    cp -r tpl $N/1;
    REF=${REF:-30} REF_FROM=${REF_FROM:-300} bash /home/claude/tt-port/game/sdk/harness/run.sh -x /home/claude/work/h_gan/gan -g /mnt/user-data/uploads/TT--extracted -o $N -t $T -s $S > /dev/null 2>&1
}

for alpha1 in $(seq 4 0.1 6); do 
    for alpha2 in $(seq 10 1 35);
        do echo -n "$alpha1 $alpha2 ";
        scripts/run_vad.sh $alpha1 $alpha2 | fgrep TOTAL;
    done
done
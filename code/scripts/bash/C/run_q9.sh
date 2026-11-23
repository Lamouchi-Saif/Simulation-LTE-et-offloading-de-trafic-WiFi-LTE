#!/bin/bash
OUT=results/tp3/q9
COMMON="--apRadius=50 --speed=5 --rate=2 --offsetFrac=0.5 --shadowSigma=0 --simTime=80 --seed=1 --distNoise=3 --outDir=$OUT"

for run in 1 2 3 4 5; do
  ./ns3 run "tp3-offload --criterion=distance --hyst=0 --run=$run --tag=dist_h0_r$run $COMMON"
  ./ns3 run "tp3-offload --criterion=distance --hyst=5 --run=$run --tag=dist_h5_r$run $COMMON"
  ./ns3 run "tp3-offload --criterion=network            --run=$run --tag=net_r$run     $COMMON"
done

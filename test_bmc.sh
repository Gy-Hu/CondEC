#!/bin/bash

for i in {0..50}
do
  ./condec_test ./HWMCC24_test/WASIM_3_stage_pipe_add/WASIM_3_stage_pipe_add_bound$i.aig -q
  echo "bound$i done"
done


#!/bin/bash
# run_rcnode.sh

TS=$(date +"%Y%m%d-%H%M%S")
LOG="out-$TS.txt"

echo "📦 Writing logs to $LOG"
build/rcnode --debug 2>&1 | tee "$LOG"

#!/bin/bash
# run_rcnode.sh
export DYLD_LIBRARY_PATH=/Users/eflores/VulkanSDK/1.4.304.0/macOS/lib

TS=$(date +"%Y%m%d-%H%M%S")
LOG="out-$TS.txt"

echo "📦 Writing logs to $LOG"
build/rcnode --debug 2>&1 | tee "$LOG"

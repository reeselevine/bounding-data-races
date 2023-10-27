#!/bin/bash

PARAM_FILE="params.txt"

# Generate a random number between min and max
function random_between() {
  local min=$1
  local max=$2

  local range=$((max - min + 1))
  local random=$((RANDOM % range + min))
  echo "$random"
}

function random_config() {
  local workgroupLimiter=$1
  local workgroupSizeLimiter=$2

  echo "testIterations=200" > $PARAM_FILE
  local testingWorkgroups=$(random_between 2 $workgroupLimiter)
  echo "testingWorkgroups=$testingWorkgroups" >> $PARAM_FILE
  local maxWorkgroups=$(random_between $testingWorkgroups $workgroupLimiter)
  echo "maxWorkgroups=$maxWorkgroups" >> $PARAM_FILE
  local workgroupSize=$(random_between 1 $workgroupSizeLimiter)
  echo "workgroupSize=$workgroupSize" >> $PARAM_FILE
  echo "shufflePct=$(random_between 0 100)" >> $PARAM_FILE
  echo "barrierPct=$(random_between 0 100)" >> $PARAM_FILE
  local stressLineSize=$(($(random_between 2 10) ** 2))
  echo "stressLineSize=$stressLineSize" >> $PARAM_FILE
  local stressTargetLines=$(random_between 1 16)
  echo "stressTargetLines=$stressTargetLines" >> $PARAM_FILE
  echo "scratchMemorySize=$((32 * $stressLineSize * $stressTargetLines))" >> $PARAM_FILE
  echo "memStride=$(random_between 1 7)" >> $PARAM_FILE
  echo "memStressPct=$(random_between 0 100)" >> $PARAM_FILE
  echo "memStressIterations=$(random_between 0 1024)" >> $PARAM_FILE
  echo "memStressPattern=$(random_between 0 3)" >> $PARAM_FILE
  echo "preStressPct=$(random_between 0 100)" >> $PARAM_FILE
  echo "preStressIterations=$(random_between 0 128)" >> $PARAM_FILE
  echo "preStressPattern=$(random_between 0 3)" >> $PARAM_FILE
  echo "stressAssignmentStrategy=$(random_between 0 1)" >> $PARAM_FILE
  echo "permuteThread=419" >> $PARAM_FILE
}

if [ $# != 1 ] ; then
  echo "Need to pass device index as first argument"
  exit 1
fi

device_idx=$1

if [ ! -d results ] ; then
  mkdir results
fi

random_config 1024 256

res=$(./runner -n rr -s rr-mem-device-scope-device.spv -r rr-results.spv -p params.txt -t rr-mem-device-params.txt -d $device_idx)
device_used=$(echo "$res" | head -n 1 | sed 's/.*Using device \(.*\)$/\1/')
echo "$device_used"
failed=$(echo "$res" | tail -n 1)

if [[ "$failed" == "Violations detected!" ]]; then
  echo "got em"
  if [ ! -d "results/$device_used" ] ; then
    mkdir "results/$device_used"
  fi
fi





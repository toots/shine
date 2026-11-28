#!/bin/sh
# tests/golden under each kernel set the processor runs, forced with
# SHINE_KERNELS; a set it does not run, or an unknown one, must fail.
# Run from the build directory, as make check does.

runs=0
failed=0
for set in $(./tests/kernels --list | sed -n '/^missing /!p'); do
  if output=$(SHINE_KERNELS=$set ./tests/golden 2>&1); then
    echo "golden under $set:" $output
    runs=$((runs + 1))
  else
    echo "FAIL golden under $set:"
    echo "$output"
    failed=1
  fi
done
for set in $(./tests/kernels --list | sed -n 's/^missing //p') no-such-set; do
  if SHINE_KERNELS=$set ./tests/golden 2>&1 |
    grep -q "SHINE_KERNELS=$set is not available"; then
    echo "golden refused $set, which is not available"
  else
    echo "FAIL golden did not refuse $set, which is not available"
    failed=1
  fi
done
if [ "$runs" -eq 0 ]; then
  echo "FAIL no kernel set ran"
  failed=1
fi
exit $failed

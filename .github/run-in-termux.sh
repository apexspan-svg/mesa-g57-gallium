#!/bin/bash
# Run a command inside a Termux proot userland (aarch64 bootstrap).
# Usage: run-in-termux.sh <command...>
# Env: TROOT must point at the extracted bootstrap root.
set -eu
: "${TROOT:?set TROOT to the Termux rootfs dir}"
proot --rootfs="$TROOT" \
  -b /dev -b /proc -b /sys -b /etc/resolv.conf \
  -b "$GITHUB_WORKSPACE:/repo" \
  -w /repo \
  /usr/bin/env -i HOME=/root \
  PATH=/data/data/com.termux/files/usr/bin:/usr/bin:/bin \
  TERM=xterm-256color LANG=C.UTF-8 \
  /bin/bash -c "$*"

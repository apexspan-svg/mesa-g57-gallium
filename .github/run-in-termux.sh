#!/bin/bash
# Run a command inside a Termux proot userland (aarch64 bootstrap).
# Usage: run-in-termux.sh <command...>
# Env: TROOT must point at the extracted bootstrap root.
set -eu
: "${TROOT:?set TROOT to the Termux rootfs dir}"
PREFIX=/data/data/com.termux/files/usr
proot --rootfs="$TROOT" \
  -b /dev -b /proc -b /sys -b /etc/resolv.conf \
  -b "$GITHUB_WORKSPACE:/repo" \
  -w /repo \
  "$PREFIX/bin/env" -i HOME=/root \
  PATH="$PREFIX/bin:/usr/bin:/bin" \
  TERM=xterm-256color LANG=C.UTF-8 \
  "$PREFIX/bin/bash" -c "$*"

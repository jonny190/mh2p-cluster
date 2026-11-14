#!/bin/sh
# Builds altscreen.so (GPL-3.0-or-later, see altscreen.c) and copies it to the
# ModKit overlay, Update/altscreen/altscreen.so.
#
# Needs Docker and the QNX 6.5 ARMv7 toolchain image from
# https://github.com/luka-dev/qnx65-armv7-toolchain, built once as
#   docker build --platform linux/amd64 -t qnx65-armv7-toolchain:4.9 .
# (GCC 4.9; the binaries run on the MH2p's QNX 6.6).
set -e
cd "$(dirname "$0")"
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src qnx65-armv7-toolchain:4.9 sh -c '
  arm-unknown-nto-qnx6.5.0eabi-gcc -O2 -Wall -Wextra -fPIC -shared -o altscreen.so altscreen.c -lc'
cp altscreen.so ../../modkit/Mods/ClusterIntegration/Update/altscreen/altscreen.so
rm -f altscreen.so
ls -l ../../modkit/Mods/ClusterIntegration/Update/altscreen/altscreen.so

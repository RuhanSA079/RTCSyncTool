#!/bin/bash

#ln -s /usr/lib/aarch64-linux-gnu/libi2c.so.0 /usr/lib/aarch64-linux-gnu/libi2c.so

if [ -f "RTCSyncTool" ]; then
echo "Removing compiled RTCSyncTool..."
rm -r RTCSyncTool
fi

echo "Compiling RTCSyncTool..."
#ldconfig

#LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/lib/aarch64-linux-gnu/" gcc -static -o RTCSyncTool rtcsynctool.c -li2c -lc
gcc -static -o RTCSyncTool rtcsynctool.c -li2c -lc

if [ -f "RTCSyncTool" ]; then
echo "RTCSyncTool compiled successfully, stripping binary..."
strip RTCSyncTool
fi

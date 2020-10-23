#! /bin/bash
sleep 10
export DISPLAY=:0
cd /home/osmc/player
exec sudo -u pi /bin/sh - << eof
./player ../X/minhalista.m3u

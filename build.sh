#! /bin/bash

gcc -c main.c -o player.o `pkg-config --cflags gtk+-3.0 libvlc`
gcc player.o -o player `pkg-config --libs gtk+-3.0 libcurl libvlc` -export-dynamic
rm player.o

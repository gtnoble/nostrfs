#!/bin/bash
gcc nostrFs.c -Wall -Wpedantic -Werror -g -o nostrfs `pkg-config fuse --cflags --libs` -lsqlite3
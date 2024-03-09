#!/bin/bash
gcc db.c path.c nostrFs.c -fsanitize=address -fsanitize=leak -fsanitize=undefined -Wall -Wextra -Wpedantic -Werror -g -o nostrfs `pkg-config fuse --cflags --libs` -lsqlite3
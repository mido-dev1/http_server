#!/bin/sh

set -e

gcc src/*.c -o server -lz -lpthread

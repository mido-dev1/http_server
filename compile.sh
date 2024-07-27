#!/bin/sh

set -e

gcc src/*.c -o server app/*.c

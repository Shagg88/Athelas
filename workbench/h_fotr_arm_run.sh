#!/bin/bash
exec qemu-aarch64 -L /usr/aarch64-linux-gnu /home/claude/work/h_fotr_arm/fotr "$@"

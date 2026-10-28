#!/bin/bash
exec qemu-aarch64 -L /usr/aarch64-linux-gnu /home/claude/work/h_rx_arm/rotkx_headless "$@"

# Quick Command Reference

All commands work from any directory (PowerShell or CMD).

## Build

    make -C C:\Users\iceoc\Documents\EPS-second-try

## Clean Build

    make -C C:\Users\iceoc\Documents\EPS-second-try clean

## Flash

    make -C C:\Users\iceoc\Documents\EPS-second-try flash

## Reset (without reflashing)

    openocd -f C:\Users\iceoc\Documents\EPS-second-try\openocd.cfg -c "init; reset; exit"

## Live Serial Terminal

    putty -serial COM6 -sercfg 115200,8,n,1,N

## Flight Build (no logging, optimized)

    make -C C:\Users\iceoc\Documents\EPS-second-try flight

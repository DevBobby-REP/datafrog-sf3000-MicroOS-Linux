#!/bin/sh

rm -f /mnt/sdcard/cubegm/MicroOS-Linux/Logs/log.moslog

exec >> /mnt/sdcard/cubegm/MicroOS-Linux/Logs/log.moslog 2>&1

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/mnt/sdcard/cubegm/lib:/mnt/sdcard/cubegm/usr/lib;

echo "+=====+"
echo "MicroOS-Linux system log file contains all error information."
echo "+=====+"

echo "Linux version: $(uname -r)"

sh /mnt/sdcard/cubegm/MicroOS-Linux/IMPORTDRIVERS 
                                                                        
init -q

/mnt/sdcard/cubegm/MicroOS-Linux/MicroOS &

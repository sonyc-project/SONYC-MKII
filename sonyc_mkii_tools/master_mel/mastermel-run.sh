#!/bin/bash

SLEEP=30
while true; do
    # check for any devices matching (idk why bash doesn't have an easy empty glob test)
    while [ -z "$(find /dev -maxdepth 1 -name 'ttyACM*' -print -quit)" ]; do 
	echo 'No mastermel devices found matching /dev/ttyACM*. Waiting $SLEEP' && sleep "$SLEEP"
    done
    /usr/local/bin/master_mel --dev /dev/ttyACM* --listen --udp # &> /var/log/mastermel.out
done

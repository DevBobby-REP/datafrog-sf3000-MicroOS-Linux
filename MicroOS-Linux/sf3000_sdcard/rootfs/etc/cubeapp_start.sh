#!/bin/sh

START_COMMAND="/usr/bin/hcprojector &"
PROCESS_NAME="hcprojector"
KILLPROCESS_NAME="icube rkgame"
HCPROJECTOR_SHELL="cubeapp_start.sh"
START_CUBE="/media/mmc/cubegm/icube_start.sh &"
MYSLEEP_TIME=5

start_process() {
    echo "start '$START_COMMAND'"
    eval $START_COMMAND
}

stop_process() {
    echo "killstop '$KILLPROCESS_NAME'"
    killall  $KILLPROCESS_NAME
}

check_process() {
    local count
    count=$(ps aux | grep $PROCESS_NAME | grep -v grep | wc -l)
    if [[ $count -eq 0 ]]; then
    	return 0
    else
        return 1
    fi
}

stop_process
start_process
while true; do
	check_process
	retval=$?
	if [ $retval -eq 0 ]; then
		eval $START_CUBE
		break;
	fi
	sleep $MYSLEEP_TIME
done


#!/bin/sh

TIMEOUT_FILE=/configs/sinker_poll.config
CHECK_PERIOD=5
re='^[0-9]+$' # Regex to validate the timeout is a number

# Function to print to the syslog rather than stdout with echo
echo_syslog()
{
    echo $1 | /usr/bin/logger -t `basename $0`
}

# Function to be called when SIGUSR1 signal is recieved
read_timeout()
{
    if [ -f "$TIMEOUT_FILE" ]; then
        echo_syslog "Reading sinker poll config file..."
        PERIOD_SEC=$( cat $TIMEOUT_FILE )
        if [[ $PERIOD_SEC =~ $re ]]; then # Verifies that PERIOD_SEC is a number
            echo_syslog "Set sinker poll timeout to $PERIOD_SEC"
            period_div=$PERIOD_SEC/$CHECK_PERIOD
        else
            echo_syslog "Sinker poll timeout read was not a number! Read $PERIOD_SEC"
            echo_syslog "Setting sinker poll timeout to default of 21600"
            PERIOD_SEC=21600
            period_div=$PERIOD_SEC/$CHECK_PERIOD
        fi
    else
        echo_syslog "Sinker poll config file does not exist!"
    fi
}

# Function to be called when a kill (SIGTERM) signal is recieved
stop_polling()
{
    echo_syslog "Stopping sinker polling"
    exit 0
}

# Set "interrupts" for signals
trap "read_timeout" SIGUSR1
trap "stop_polling" SIGTERM

# Init PERIOD_SEC to -1 then attempt to read the config file until successful
PERIOD_SEC=-1
while [ $PERIOD_SEC -lt 0 ]
do
    sleep 5
    read_timeout
done

# Init loop counter to 0
# Divide our PERIOD_SEC found in the config file by the CHECK_PERIOD loop delay (i.e. find out how many loops between actions)
loop_counter=0
let period_div=$PERIOD_SEC/$CHECK_PERIOD

# Now loop forever sending the SIGUSR1 signal to sinker until script is killed
while true
do
    if [ $loop_counter -ge $period_div ]; then
       kill -SIGUSR1 $(pidof sinker)
       loop_counter=0
    fi
    sleep $CHECK_PERIOD
    let "loop_counter+=1"
done

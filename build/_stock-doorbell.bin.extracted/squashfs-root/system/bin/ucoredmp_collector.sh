#!/bin/sh

# This scripts will be set up to run during a user mode process crash. The
# crash will be piped via stdin to this script.
# Eventually, this script will be a wrapper around a tool that will shrink
# the core dump down to a size that is writeable to internal flash, but for
# now this script attempts to write the full crash dump to the SD card (if
# present)

function printk() {
    echo "$@" > /dev/kmsg
}

PID=0
CRASHTIME=0
EXENAME=""
SIGNAL=0
OUTPUT_DIR=""

while [ $# -ge 1 ]; do
    case "$1" in
        --)
            echo "Unknown option $1"
            exit 1
            ;;
        -p|--pid)
            PID="$2"
            shift
            ;;
        -t|--time)
            CRASHTIME="$2"
            shift
            ;;
        -n|--name)
            EXENAME="$2"
            shift
            ;;
        -s|--signal)
            SIGNAL="$2"
            shift
            ;;
        -d|--output-dir)
            OUTPUT_DIR="$2"
            shift
            ;;
    esac
    shift
done

# Note: This wrapper script is needed because the kernel is unaware of the
#       modifications made to PATH and LD_LIBRARY_PATH and so launches ucoredmp
#       without those variables set. Without this script, the loading of ucoredmp
#       will fail as the loader will be unable to find libssl, libcurl, or other libs
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH=/system/bin:$PATH
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/thirdlib:/system/lib

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

printk "ucoredmp_collector: PID $PID ($EXENAME) received signal $SIGNAL. $PID ($EXENAME) crashed"

# Copy full crash dump over to SD Card under the following conditions:
# * There is a SD Card attached
# * There is at least 128MB of disk space free on the SD Card
# * The crash dump folder has less than 3 crash dumps in it
# * The crash was not caused by SIGQUIT

SD_CARD_DIR="/media/mmc"
DEFAULT_CORES_DIR="$SD_CARD_DIR/cores"

if [ -z $OUTPUT_DIR ]; then
    OUTPUT_DIR=$DEFAULT_CORES_DIR
fi

# First create cores directory if it doesn't exist
# This may fail, thats ok
mkdir -p $OUTPUT_DIR

tries=0
while [[ $(mount | grep mmc | head -c1 | wc -c) -eq 0 ]]
do
    if [ $tries -eq 0 ]; then
        printk "ucoredmp_collector: No SD card attached, attempting to mount..."
    else
        printk "ucoredmp_collector: Still no SD card attached, trying $((3-tries)) more times..."
    fi

    # Try to mount the SD card
    mount /dev/mmcblk0p1 $SD_CARD_DIR

    tries=$((tries+1))
    if [ $tries -eq 4 ]; then
        printk "ucoredmp_collector: Failed to mount SD card, aborting..."
        exit 1
    fi
    sleep 10
done

if [ -d "$OUTPUT_DIR" ]; then 
    # Next validate at least 128MB of disk space free
    KBYTES_LEFT=$(df -P $OUTPUT_DIR | tail -1 | awk '{print $4}')
    if [ "$KBYTES_LEFT" -gt "131072" ]; then

        # Validate there aren't already 3 core files
        NUM_CORE_FILES=$(find $OUTPUT_DIR -name "*.core" | wc -l)
        if [ "$NUM_CORE_FILES" -lt "3" ]; then
            TEMP_DIR=$(mktemp -d)
            cd $TEMP_DIR

            FW_VERSION=$(grep appver /system/bin/app.ver | cut -d= -f2)
            # Read core file from stdin and pass it through ucoredmp<name>_<pid>_<signal>_<epoc_time>.core
            cat - | $SCRIPTPATH/ucoredmp -n "$FW_VERSION" --pid ${PID} --size-limit 15728640 --verbose - $TEMP_DIR/crash.core
            
            logread > application.log
            dmesg > kernel.log
            cp /system/bin/app.ver .
            tar cvf ${EXENAME}_${PID}_${SIGNAL}_${CRASHTIME}.tar *
            gzip ${EXENAME}_${PID}_${SIGNAL}_${CRASHTIME}.tar
            cd /
            mv $TEMP_DIR/${EXENAME}_${PID}_${SIGNAL}_${CRASHTIME}.tar.gz $OUTPUT_DIR
            printk "ucoredmp_collector: Copied dump info to $OUTPUT_DIR/${EXENAME}_${PID}_${SIGNAL}_${CRASHTIME}.tar.gz"
            rm -rf $TEMP_DIR
        else
            printk "ucoredmp_collector: Skipping core dump, already have 3 core dumps in $OUTPUT_DIR"
        fi
    else
        printk "ucoredmp_collector: Unable to output crash to '$OUTPUT_DIR'. Not enough space on disk"
    fi
else
    printk "ucoredmp_collector: Unable to output crash to '$OUTPUT_DIR'. '$OUTPUT_DIR' does not exist"
fi

$SCRIPTPATH/ucoredmp_metrics --pid $PID --signal $SIGNAL

#!/bin/sh
count=0

GPIO_WIFI_SETUP=57
WIFI_SETUP()
{
    echo "swith wifi setup for petcube !!"
    if [ -e /sys/class/gpio/gpio$GPIO_WIFI_SETUP ]; then
        echo out > "/sys/class/gpio/gpio$GPIO_WIFI_SETUP/direction"
        echo 0 > "/sys/class/gpio/gpio$GPIO_WIFI_SETUP/value"
        sleep 0.2
        echo 1 > "/sys/class/gpio/gpio$GPIO_WIFI_SETUP/value"
    else
        cd /sys/class/gpio
        echo $GPIO_WIFI_SETUP > export
        echo out > "/sys/class/gpio/gpio$GPIO_WIFI_SETUP/direction"
        echo 0 > "/sys/class/gpio/gpio$GPIO_WIFI_SETUP/value"
        sleep 0.2
        echo 1 > "/sys/class/gpio/gpio$GPIO_WIFI_SETUP/value"
    fi
}

while [ `ifconfig|grep wlan0|wc -l` = 1 ]
do
ifconfig wlan0 down
echo "down"
done
sleep 0.5
while [ `ifconfig|grep wlan0|wc -l` = 0 ]
do
count=$(($count+1))
if [ $count -gt 4 ]; then
    echo "Remount WiFi driver !!!"
    count=0
    WIFI_SETUP
    sleep 1
    rmmod atbm603x_wifi_sdio
    sleep 3
    WIFI_SETUP
    sleep 1
    insmod /system/driver/atbm603x_wifi_sdio.ko
    sleep 1
fi
ifconfig wlan0 up
echo "up"
sleep 0.5
done
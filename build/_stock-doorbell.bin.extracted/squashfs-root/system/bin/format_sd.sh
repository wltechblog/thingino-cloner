#!/bin/sh
# $1 is the device path; $2 is the file system format, 0 is fat32, 1 is unknown, 2 is exfat
case $2 in
"0")
	echo "SD type is vfat"
	mkfs.vfat $1
	;;
"2")
	echo "SD type is exfat"
	mkexfatfs $1
	;;
*)
	echo "unknow type, format to exfat"
	mkexfatfs $1
	;;
esac

# version 1.2	initial version

Copyright (c) 2017 Paul van Haastrecht <paulvha@hotmail.com>


## Background
In preparation to a party, I created a “wheel of fortune” (WOF) with 4 lights, 
which light up in a random or sequential manner, start and stopped with push-buttons 
and controlled by a Raspberry Pi-2. This can easily be extended to the amount of lights needed. 

It contains a shell- script that will allow to automatically start the program and power-on and 
perform a controlled shutdown with the push-button sequence. As such this can be used without screen
and keyboard.

For detailed information about hardware and software, please read the included wof.odt

 
## Software installation


Make your self superuser : sudo bash
BCM2835 library
Install latest from BCM2835 from : http://www.airspayce.com/mikem/bcm2835/

1. cd /home/pi
2. wget http://www.airspayce.com/mikem/bcm2835/bcm2835-1.50.tar.gz
3. tar -zxf bcm2835-1.50.tar.gz		// 1.50 was version number at the time of writing
4. cd bcm2835-1.50
5. ./configure
6. sudo make check
7. sudo make install

“wheel of fortune”

mkdir wof		# assumed is /home/pi/wof
cp wof.c  wof_start wof
cd wof
cc -o wof wof.c -lbcm2835

sudo ./wof		# this MUST be run as root given we use PWM capabilities

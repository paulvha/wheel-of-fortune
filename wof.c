/* Wheel of Fortune
 * 
 * Copyright (c) 2017 Paul van Haastrecht <paulvha@hotmail.com>
 *
 * Wheel of Fortune is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Wheel of Fortune is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wheel of Fortune. If not, see <http://www.gnu.org/licenses/>.
 * 
 * 
 * make sure to download and install the BCM2835 library
 * 
 * To compile : cc -o wof wof.c -lbcm2835
 * 
 * use wof_start for auto start after reboot.
 * 
 * cp wof_start /etc/init.d
 * chmod +x /etc/init.d/wof.start
 * update-rc.d wof_start defaults
 *  
 */

#include <bcm2835.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <sys/stat.h>

// version number
#define WOF_VERSION	"version 1.2, April 2017"

/* 
 * To change the output GPIO pin for each light update the
 * defined light output pins.
 * 
 * To extend the number of lights
 * 1. define for each light the GPIO pin like for LIGHT1 below
 * 2. set the NUM_LIGHT to the new amount
 * 3. add each new defined light pin to the array light_pins
 */
// definition for lights
#define LIGHT1			24		// light output pins
#define LIGHT2			23
#define LIGHT3			22
#define LIGHT4			21

#define NUM_LIGHT		4		// number of lights

int	light_pins[NUM_LIGHT] = {LIGHT1, LIGHT2, LIGHT3, LIGHT4};

long 	usage_cnt[NUM_LIGHT];	// save counts to balance out

#define GLOW_TIME_DEFAULT 	1	// default glow time (*0.125)
#define ON				1		// light & led commands
#define ONN				2		// on (not impacted by optional invert)
#define OFF				3
#define OFFN			4		// off (not impacted by optional invert)

// definition for switches
#define START_PIN		25		// START switch input
#define STOP_PIN		19		// STOP switch input
#define START_LED		17		// Start switch led output
#define STOP_LED		16		// stop switch led output

#define STOP_PRESSED 	1		// switch status
#define START_PRESSED 	2
#define	BOTH_PRESSED	3
#define NO_PRESS_DETECTED 	0

// definition for sound effect
#define SOUND_PIN RPI_BPLUS_GPIO_J8_32 	// pin 12	pwm
#define SOUND_NORMAL	1536	// dividers for clicking sound
#define SOUND_SLOW1		2048
#define SOUND_SLOW2		2560	
#define SOUND_SLOW3		3072
#define SOUND_SLOW4		3584

#define HORN_HIGH		3064	// horn sounds
#define HORN_LOW		4096
#define HORN_OUT		5000
#define HORN_TIME		1		// time in seconds to sound horn

int snd_slow[4] = {SOUND_SLOW1, SOUND_SLOW2, SOUND_SLOW3, SOUND_SLOW4};


// global parameters
int no_sound = 0;				// 1= disable sound
int do_random = 1;				// 0 = sequential light selection
int no_led = 0;					// 1 = no led at START/STOP buttons
int glow_time= GLOW_TIME_DEFAULT;// light glow counter * 0.125 seconds
int no_shutdown = 0;			// 1 = disable shutdown after 5 x START+STOP button
int	invert_light = 0;			// 1 = invert on and off light during game.
FILE *logfile= NULL;			// holds pointer to logfile
int DEBUG = 0;					// debug

/* set PWM for clicking sound to mark-space
 * no FIFO, no reverse polarity, no silence bit
 */
void pwm_set_mode()
{
	// if no sound was requested on command line
	if (no_sound) return;
	
	uint32_t control= 0x0; 

    // set clock divisor
    bcm2835_pwm_set_clock(1000);

    // set range to 1024 for PWM0
    bcm2835_pwm_set_range(0, 2048);

    // clear any error pending
    bcm2835_peri_write(bcm2835_pwm + BCM2835_PWM_STATUS , 0x01fc);

	// add mark-space & enable PWM0
	control |= BCM2835_PWM0_MS_MODE | BCM2835_PWM0_ENABLE;

    // write to control register
    bcm2835_peri_write_nb(bcm2835_pwm + BCM2835_PWM_CONTROL, control);
 
    // write initial value to PWM0
	bcm2835_pwm_set_data(0, 512);
 }

/* initialise hardware */
int do_init()
{
	int i;

	// setup BCM2835
	if (!bcm2835_init())	return(1);
	
	// set pin for START_switch as input, pull-up, Trigger on change to low.
	bcm2835_gpio_fsel(START_PIN,BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_set_pud(START_PIN, BCM2835_GPIO_PUD_UP);

	// set pin for STOP_switch as input, pull-up, Trigger on change to low.
	bcm2835_gpio_fsel(STOP_PIN,BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_set_pud(STOP_PIN, BCM2835_GPIO_PUD_UP);

	//set light pins as output and low
	for (i=0; i < NUM_LIGHT; i++)
	{
		bcm2835_gpio_write(light_pins[i], HIGH);
		bcm2835_gpio_fsel(light_pins[i],BCM2835_GPIO_FSEL_OUTP);

	}
	
	// skip if no_led was requested on command line
	if (! no_led)
	{
		// set button led pins as output and low
		bcm2835_gpio_fsel(START_LED,BCM2835_GPIO_FSEL_OUTP);
		bcm2835_gpio_write(START_LED, LOW);
	
		bcm2835_gpio_fsel(STOP_LED,BCM2835_GPIO_FSEL_OUTP);
		bcm2835_gpio_write(STOP_LED, LOW);
	}
	
	// setup PWM for clicking sound
	pwm_set_mode();
	
	return(0);
}

/* write to logfile otherwise to screen */
void logprintf(char * mess)
{
	if (logfile)
	{
		fprintf(logfile, "%s", mess);
	}
	else if (DEBUG)
		printf(mess);
}

/* Turn a light on or off
 * @param light : light # 
 * @param cmd : 
 * 		ON or OFF (impacted by invert)
 * 		ONN or OFFN (NOT impacted by invert)
 */

void set_light(int light, int cmd)
{
	// check within range
	if (light > NUM_LIGHT -1) return;
	
	// if invert lights was requested
	if (invert_light)
	{
		if (cmd == ON)	cmd = OFF;
		else if (cmd == OFF) cmd = ON;
	}
	
	if (cmd == ON || cmd == ONN)
		bcm2835_gpio_write(light_pins[light], LOW);
    else
		bcm2835_gpio_write(light_pins[light], HIGH);

}

/* Turn button led on or off
 * 
 * @param led : either START_LED or STOP_LED 
 * @param cmd : ON / ONN or OFF / OFFN
 */

void set_button_led(int led, int cmd)
{
	// if do not use button leds request was provided on command line
	if (no_led) return;
	
	// check within range
	if (led != START_LED && led != STOP_LED) return;
	
	if (cmd == ON ||cmd == ONN)
		bcm2835_gpio_write(led, HIGH);
    else
		bcm2835_gpio_write(led, LOW);
}

/* turn sound on or off
 * @param sound:  0 do not play sound, > 0 set divider & sound
 */
 
void set_sound(int sound)
{
	// if do not use sound request was provided on command line
	if (no_sound) return;
	
	// if sound click requested
	if (sound)
	{
		// set clock divisor
		bcm2835_pwm_set_clock(sound);
		
		// set sound_pin as PWM0 output
		bcm2835_gpio_fsel(SOUND_PIN, BCM2835_GPIO_FSEL_ALT0);
	}
	else
		// reset sound_pin
		bcm2835_gpio_fsel(SOUND_PIN,BCM2835_GPIO_FSEL_INPT);
}

/* sound a horn tone */

void sound_horn(int value)
{
	// no sound request was provided on command line
	if (no_sound) return;

    // set clock divisor
    bcm2835_pwm_set_clock(16);
    
    // set range to 2048 for PWM0
    bcm2835_pwm_set_range(0, value);
	
	// set sound_pin as PWM0 output
	bcm2835_gpio_fsel(SOUND_PIN, BCM2835_GPIO_FSEL_ALT0);	
	
	sleep(HORN_TIME);

	// reset sound_pin
	bcm2835_gpio_fsel(SOUND_PIN,BCM2835_GPIO_FSEL_INPT);
	
	// restore range to 2048 for PWM0
    bcm2835_pwm_set_range(0, 2048);
}


/* close out correctly and reset the hardware / release BCM2835 
 * @param  ret : exit code 
 * 
 * if ret = 7 : do not exit but return */
 
void close_out(int ret)
{
	int i;
	
	// deselect the switch pins and return to normal
	bcm2835_gpio_set_pud(START_PIN, BCM2835_GPIO_PUD_OFF);
	bcm2835_gpio_set_pud(STOP_PIN, BCM2835_GPIO_PUD_OFF);

		
	// reset light pins 
	for (i=0; i < NUM_LIGHT; i++)
	{
		//bcm2835_gpio_write(light_pins[i], LOW);
		bcm2835_gpio_fsel(light_pins[i],BCM2835_GPIO_FSEL_INPT);
	}	
	
	// skip is no_led was requested on command line
	if (! no_led)
	{
		// reset button led pins
		bcm2835_gpio_write(START_LED, LOW);
		bcm2835_gpio_fsel(START_LED, BCM2835_GPIO_FSEL_INPT);
	
		bcm2835_gpio_write(STOP_LED, LOW);
		bcm2835_gpio_fsel(STOP_LED, BCM2835_GPIO_FSEL_INPT);
	}
	
	// skip is no_sound was requested on command line	
	if (! no_sound)
	{
		// reset sound pin
		bcm2835_gpio_fsel(SOUND_PIN,BCM2835_GPIO_FSEL_INPT);
	
		// clear PWM control
		bcm2835_peri_write_nb(bcm2835_pwm + BCM2835_PWM_CONTROL, 0x0);
	}
	
	// close bcm2835 library
	bcm2835_close();
	
	// do  not exit if 7 (needed for close_down)
	if (ret == 7) return;
	
	if (logfile) fclose(logfile);
	
	exit(ret);
}




/* close out and shutdown */
void close_down()
{
	int i;

	// indicate going out
	sound_horn(HORN_OUT);
	
	// set all lights on
	for (i = 0 ; i< NUM_LIGHT ; i++)	set_light(i, ONN);	
	
	// move down to indicate closing down
	for (i = NUM_LIGHT ; i > 0 ; i--)
	{
		set_light(i, OFFN);	
		sleep (1);
	}

	// sound horn second
	sound_horn(HORN_OUT);
	
	// close out resources
	close_out(7);
	
	logprintf("shutdown\n");
	if (logfile) fclose(logfile);
	
	system("shutdown -P now");

	// debug: comment line above and uncomment line below
	//exit(0);
}

/* catch signals to close out correctly */
void signal_handler(int sig_num)
{
	logprintf("\nStopping Wheel of Fortune.\n");
	close_out(0);
}

/* setup signals */
void set_signals()
{
	struct sigaction act;
	
	memset(&act, 0x0,sizeof(act));
	act.sa_handler = &signal_handler;
	sigemptyset(&act.sa_mask);
	
	sigaction(SIGTERM,&act, NULL);
	sigaction(SIGINT,&act, NULL);
	sigaction(SIGABRT,&act, NULL);
	sigaction(SIGSEGV,&act, NULL);
	sigaction(SIGKILL,&act, NULL);
}


/* check for switch pressed
 * @param res: 1 = is reset the trigger register and counter
 * 
 * return STOP_PRESSED, START_PRESSED or NO_PRESS_DETECTED
 */
int read_switch(int res)
{
	/* remember how often STOP and START have been pressed together
	 *  > 4 times will force shutdown.
	 */
	static	int	shutdown_cnt = 0;
	int ret = NO_PRESS_DETECTED;
	
	// reset (clear any pending triggers)
	if (res)
	{
	
	 	// reset shutdown counter
	 	shutdown_cnt = 0;
	 	
	 	return(ret);
	}
	
	// read stop switch trigger 
	if (bcm2835_gpio_lev(STOP_PIN) == LOW)
	{ 
		logprintf("Stop switch detected\n");
		
		// wait for short time to handle any bouncing
		usleep(250000);

		ret = STOP_PRESSED;
	}
	
	// read start switch trigger 
	if (bcm2835_gpio_lev(START_PIN) == LOW)
	{ 
		logprintf("Start switch detected\n");
		
		// wait for short time to handle any bouncing
		usleep(250000);
		
		// if STOP + START switch pressed together more than 5 times shutdown
		// and this features was NOT disabled on the command line
		if (ret == STOP_PRESSED && ! no_shutdown)
		{	
			if (shutdown_cnt++ > 4)	
			{
				logprintf("no closing down\n");
				close_down();
			}
			
			ret = BOTH_PRESSED;
			logprintf("Triggered counter to close down\n");
		}
		// else just return that switch has been detected
		else
			ret = START_PRESSED;
	}	

	return (ret);
}

/* Determine next light to turn on that is NOT the same as the last
 * light. 
 * 
 * If the random generator resulted in the same as last light, it will
 * select another light with the lowest usage so far.
 */
int get_random_light()
{
	int 		j, r_count, r;
	static	int	prev=0;
	
	// get random light number
	srand(time(NULL)); 
	r = rand() % NUM_LIGHT;
	
	// if same as previous: look for other
	if (r == prev) 
	{
		r = 0xff;		// no new found yet
		
		for (j = 0; j < NUM_LIGHT; j++)
		{
			// if NOT the same as previous
			if (j != prev)
			{
				// set value if first found
				if (r == 0xff)
				{
					r_count = usage_cnt[j];
					r = j;
				}
				
				// select the light with lowest count
				else if (usage_cnt[j] < r_count)
				{ 
					r_count = usage_cnt[j];
					r = j;
				}
			}
		}
	}
	
	// increment light usage counter
	usage_cnt[r]++;
	
	// save as previous
	prev=r;
	
	// for debug only
	//printf ("\n\tcnt0 = %d, cnt1 = %d, cnt2 = %d, cnt3 = %d\n",  usage_cnt[0], usage_cnt[1], usage_cnt[2], usage_cnt[3]);

	return(r);
}

/* Determine next light 
 * if do_random was overrulled it will handle in sequential order
 */
int get_light()
{
	static int current_light = 0;
	
	// if random was NOT de-selected on the command line
	if (do_random) return(get_random_light());

	// increment light
	if (++current_light == NUM_LIGHT) current_light=0;
	
	return(current_light);
}

/* Set light on and others off, and wait
* @param light : light to turn on
* @param glow_time : how long to glow in 0.25 seconds before returning
*                    and checking for START or STOP pressed.
* 
* returns 
* 	STOP_PRESSED if STOP_button was pressed 
*   START_PRESSED if START_button was pressed 
* 	BOTH_PRESSED if both keys had been pressed together
* 	else NO_PRESS_DETECTED.
*/

int glow_light(int light, int glow_time)
{
	int i, ret = NO_PRESS_DETECTED;
	
	// set the requested light, reset others
	for (i = 0;  i < NUM_LIGHT; i++)
    {
		if (i == light)	set_light(i, ON);
		else	set_light(i, OFF);
	}

	while (glow_time-- > 0)				// count down time on.
	{
		// as long as no switch detected : keep checking
		if (ret == NO_PRESS_DETECTED)	
		{	
			ret = read_switch(0);
		
			// make sound to indicate we stop
			if (ret == STOP_PRESSED)  sound_horn(HORN_LOW);
		}

		usleep(125000);					// sleep 0.125 seconds
	}
	
	return(ret); 
}

/* wait for START switch to be pressed
 * blinking light will show we are waiting
 * also this light was last selected.
 */

void wait_for_start(int light)
{
		int i, rswitch, lp;
		
		i = ONN;		// ONN  / OFFN is NOT impacted by optional invert-light request 
		
		// reset switch triggers
		read_switch(1);
		
		do
		{
			// blink light
			set_light(light, i);	
			
			// blink start button led
			set_button_led(START_LED, i);	
			
			// continue to read switches
			for (lp = 0; lp < 16; lp++)
			{
				usleep(125000);	
				
				rswitch = read_switch(0);
		
				if (rswitch != NO_PRESS_DETECTED)
					lp=16;
			}
			
			// if BOTH pressed detected, indicating shutdown
			if (rswitch == BOTH_PRESSED)
			{
				// switch off current light
				set_light(light, OFFN);
				
				// select next
				if (++light == NUM_LIGHT) light = 0;
			}
			
			// reverse light/led command
			if (i == ONN) i = OFFN;
			else i = ONN;
			
		} while (rswitch != START_PRESSED);
	
		// set all lights on
		for (i = 0;  i < NUM_LIGHT; i++)
		{
			set_light(i, ONN);
			usleep(250000);	
		}
		
		// reset switch triggers
		read_switch(1);
		
		// set button leds on
		set_button_led(START_LED, ON);
		set_button_led(STOP_LED, ON);
		
		/* wait for either START or STOP 
		 * and blink light 16 x 0.125 seconds = 2 sec
		 */
		
		do 
		{
			for (lp = 0; lp < 16; lp++)
			{
				usleep(125000);	
				
				rswitch = read_switch(0);
		
				// stop if switch detected
				if (rswitch != NO_PRESS_DETECTED) lp=16;
			}
		
		} while (rswitch == NO_PRESS_DETECTED);
		
		
		// sound horn to indicate we are going to start
		sound_horn(HORN_HIGH);	
		
		// dim lights one-by-one 
		for (i = 0;  i < NUM_LIGHT; i++)	
		{
			set_light(i, OFFN);
			usleep(250000);	
		}
}

/* main loop */
void main_loop()	
{
	int	out_loop, on_time, rswitch;
	int	light = 0, i, j;
	
	while(1)
	{
		// (re) set light usage counters
		for (i=0; i < NUM_LIGHT; i++)	usage_cnt[i] = 0;
		
		// (re) set time on for lights to default
		on_time = glow_time;
		
		// wait for start OK
		wait_for_start(light);	
		
		// set clicking sound on
	    set_sound(SOUND_NORMAL);

		// reset switch triggers
		read_switch(1);
   
	    // as long no STOP switch pressed  
	    do
	    {
			// determine next light
			light = get_light();
		
			// glow the light for on_time period
			rswitch = glow_light(light, on_time);
			
			// if START was pressed increase speed (only this round)
			if (rswitch == START_PRESSED)
			{
				if (on_time > 1) on_time--;
				else on_time = 3;
			}
		} while (rswitch != STOP_PRESSED);
		
		// set button leds off
		set_button_led(STOP_LED, OFF);
		set_button_led(START_LED, OFF);	
		
		/* now that STOP has been pressed, slow down the lights 
		 * based on random number between 2 and 4 = 2 + (3 -1)
		 */
		
		srand(time(NULL)); 
		out_loop = 2 + (rand() % 3);
		
		// spread the slow sound across the out_loop
		j = (10 * 4) / out_loop;
		
		for (i = 0; i < out_loop; i++)
		{
			// determine next light
			light = get_light();
			
			// set light on
			glow_light(light, on_time);
			
			// set slower sound aligned with the random out_loop
			set_sound(snd_slow[i * j / 10]);
			
			// in between sleep longer everytime
			usleep (250000 * i);
	 	}
	 	
	 	// turn sound off
	 	//set_sound(0);
	 	sound_horn(HORN_OUT);
	}
}

/* display usage / help information */
void usage(char *name)
{
	printf("%s \n", WOF_VERSION);
	printf("usage: %s  [-d] [-g #] [-L logfile] [-i] [-l] [-r] [-s ] [-h] \n\n"
	
		"Copyright (c)  2017 Paul van Haastrecht\n"
		"\n"
		"-d,	Disable shutdown after STOP & START buttons pressed together for 5 times\n"
		"-g,	Light glow counter in 0.125 second increment.(default: %d => %d second)\n"
		"-i,	Invert lights.\n"
		"-l, 	Disable leds at buttons.\n"
		"-r, 	Light selection is not random, but sequential.\n"
		"-s, 	Disable clicking sound.\n"
		"-L,	Write progress messages to logfle.\n"
		"-D,	enable debug messages.\n" 
		"-h, 	Help.\n\n",
		name, glow_time, (glow_time * 125 /1000));
}

/* convert string to int */
int str_to_i(char *arg)
{
	int	dec = 0, len, i;
	
	len = strlen(arg);
	
	for (i = 0; i < len; i++)
		dec = dec * 10 + (arg[i] - '0');
	
	return(dec);
}

/* try to create log file */
int try_logfile (char * arg)
{
	if (strlen(arg) != 0 )
	{
		logfile = fopen(arg, "a");

		if (logfile == NULL) return(1);
	
		return(0);
	}
	
	return(1);
}


int main(int argc, char **argv)
{
	int c;

	if (geteuid() != 0)
	{
		printf("Must be run as root.\n");
		exit(-1);
	}
	
	while (1)
	{
		c = getopt(argc, argv,"-dslrg:hiL:D");
			
		if (c == -1)	break;
			
		switch (c) {
			case 's':	// no sound
				no_sound = 1;
				break;
				
			case 'd':	// no shutdown based on 5 x START + STOP button
				no_shutdown = 1;
				break;
								
			case 'l':	// no led with start/stop buttons
				no_led = 1;
				break;

			case 'D':	// no led with start/stop buttons
				DEBUG = 1;
				break;
			case 'i':	// invert lights
				invert_light = 1;
				break;
				
			case 'r':	// no random light selection
				do_random = 0;
				break;
		
			case 'L':	//write to LOGFILEtime
				if (try_logfile(optarg))
				{
					printf("Can not create logfile. Request ignored.\n");
				}
				break;
			case 'g':	//overwrite glow_time
				glow_time = str_to_i(optarg);
				if (glow_time < 1)
				{
					printf("Can not handle glow counter values below 1. Request ignored.\n");
					logprintf("Can not handle glow counter values below 1. Request ignored.\n");
					glow_time = GLOW_TIME_DEFAULT;
				}
				break;
			case 'h':
			default:
				usage(argv[0]);
				exit(0);
				break;
		}
	}

	logprintf("starting program\n");
	
	// intitialise the hardware
	if (do_init()) exit(1);
	
	// catch cntrl-c to stop
	set_signals();
	
	// go to main loop
	main_loop();
	
	// stop -WALL complaining..
	exit(0);
}

#include <stdio.h>
#include <conio.h>
#include <dos.h>
#include <mem.h>
#include <ctype.h>
#include <stdlib.h>

typedef unsigned int	uint;
typedef unsigned char	uchar;
typedef unsigned long	ulong;

// ****************************************************************************
// Local defines to make life easier
#define	LPT1_PORT		0x3BC
#define	LPT2_PORT		0x378
#define	LPT3_PORT		0x278

#define	DATA_PORT		(LPT_BASE + 0)
#define	STATUS_PORT		(LPT_BASE + 1)
#define	CONTROL_PORT	(LPT_BASE + 2)
#define	ECP_CONTROL_PORT	(LPT_BASE + 0x402)

// in the following comments two pin numbers are listed.  The actual pin
// that will be read depends on the setting of the INPUT_SOURCE parameter.
// If it is set to 1, then pins 2-9 are used, if 0 then pins 15, 13, etc. are
// used.  This setting is based on two things: 1) if the port address for
// the printer is 0x3BC then we always use 15-11. 2) If the
// command line swith 'T' is specified as 0 we always use 15-11, if it is 1
// then we use pins 2-9.
// Pins 15-11 are the safest set and work on all ports, pins 2-9 only work
// under certain circumstances, but it gives you 3 more bits.
#define	START_BIT		0x01		// pin 2 or 15
#define	LANE1_FINISH	0x02		// pin 3 or 13
#define	LANE2_FINISH	0x04		// pin 4 or 12
#define	LANE3_FINISH	0x08		// pin 5 or 10
#define	LANE4_FINISH	0x10		// pin 6 or 11
#define	LANE5_FINISH	0x20		// pin 7
#define	LANE6_FINISH	0x40		// pin 8
#define	LANE7_FINISH	0x80		// pin 9


// defines for the printer output of race results
#define	FORM_FEED		"\x0C"


#define	MAX_LANES		7		// max number of lanes on a track

#if  MAX_LANES < 1 || MAX_LANES > 7
	#error This software supports a maximum of 7 lanes
#endif

#define	MAXCARS	100	// maximum number of cars in the race

// used by the Race() function to track each car
typedef struct
{
	int		ID;
	ulong	laneTime[MAX_LANES],	// in clicks
			totalTime;				// in clicks
	int		scaleSpeed[MAX_LANES],	// in scaleMPH
			place[MAX_LANES];		// 1 = first, 2 = second, etc.
} CarData;

// used by Heat() function to return data on each lane
typedef struct
{
	int		carID;		// input to Heat() function, only used for display
	ulong	laneTime;	// in clicks
	int		scaleSpeed,	// in scaleMPH
			place;		// 1 = first, 2 = second, etc.
} LaneInfo;


// ****************************************************************************
// prototypes
void Diagnostics(void);
void Race(void);
void OpenRace(void);
void UpdateLaneInformation(int cars, CarData *car);
void ConfigurePort(uint port, int inputSource);
void ResetPort(void);
void SetTimerMode2(void);
ulong HiResolutionTime(void);
uint GetTrackBitsFromStatus(void);
uint GetTrackBitsFromData(void);
// #define GetTrackBits()	((inportb(STATUS_PORT) >> 4) ^ 0x08)
// #define GetTrackBits()	inportb(DATA_PORT)

// ****************************************************************************
// global variables
uint	LPT_BASE,
		LANES = 3,		// number of lanes on this track (default)
		TIMEOUT = 15;	// number of seconds to timeout a race (default)
int		INPUT_SOURCE;	// default input source is status bits

uint (*GetTrackBits)(void);


// ****************************************************************************
// routines
void main(int argc, char *argv[])
{
	int		done;
	uint	lptPort;
	int		inputSource = 0;	// default input source is status bits

	clrscr();

	// find a default LPT port address
	lptPort = *((uint far *)MK_FP(0x40, 0x08));
	if (lptPort == 0)
		lptPort = LPT1_PORT;

	if (argc > 1)
	{
		int		i;
		for (i = 1; i < argc; i++)
		{
			// P for port
			if (toupper(argv[i][0]) == 'P')
			{
				lptPort = (int) strtoul(&argv[i][1], NULL, 16);
				// did they put a port (1-3) number instead of a port address (0x378)
				if (lptPort >=1 && lptPort <=3)
				{
					lptPort = ((uint far *)MK_FP(0x40, 0x08))[lptPort - 1];
					if (lptPort == 0)
					{
						// if specified port not available, try first LPT
						lptPort = ((uint far *)MK_FP(0x40, 0x08))[0];
						// if that's 0 too, default to our LPT1 address
						if (lptPort == 0)
							lptPort = LPT1_PORT;
					}
				}
			}

			// T for port type (read from data or status port)
			// T0 (default) is read from status port
			// T1 is read from data port
			if (toupper(argv[i][0]) == 'T')
			{
				inputSource = atoi(&argv[i][1]);
			}

			// L for lanes
			if (toupper(argv[i][0]) == 'L')
			{
				int		lanes;

				lanes = atoi(&argv[i][1]);

				// set lanes if it is valid
				if (lanes >= 1 && lanes <= MAX_LANES)
					LANES = lanes;
			}

			// O for timeOut
			if (toupper(argv[i][0]) == 'O')
			{
				int		val;

				val = atoi(&argv[i][1]);

				// set timeout if it is valid
				if (val >= 1 && val <= 3500) // 0xFFFFFFFF / 1193182 = 3599
					TIMEOUT = val;

				printf("Timeout set to %u\n", TIMEOUT);
			}
		}
	}

	SetTimerMode2();		// set 8253 timer chip to mode 2
   
	done = 0;
	do
	{
		// configure port at start of each loop, we may have reset
		// the port at the end of the last race to print results to the
		// printer
		ConfigurePort(lptPort, inputSource);

		printf("Pinewood Derby Race timing V1.1 (" __DATE__ ")\n"
				"Copyright (C) 1997-1999 by James H. Brown, Orem, UT (jbrown@burgoyne.com)\n"
				"All Rights Reserved Worldwide\n"
				"\n"
				"License is hereby granted by the author for non-commercial use by any and all\n"
				"charitable organizations and / or community groups.\n"
				"\n"
				"\n");
		// present main menu
		printf("  -- Main Menu --\n"
				"\n"
				" 1  - Race\n"
				" 2  - Open track racing\n"
				" 3  - Diagnostics\n"
				"ESC - Exit\n"
				"\n"
				"Your choice: ");

		switch(getch())
		{
		case '1':
			Race();
			break;

		case '2':
			OpenRace();
			break;

		case '3':
			Diagnostics();
			break;

		case 'x':
		case 'X':
		case 0x1b:	// escape
			done = 1;
			break;

		default:
			printf("\n\aInvalid entry\n");
			break;
		}
		printf("\n\n");
	} while (!done);

	ResetPort();
}

// ****************************************************************************
// port is the base port address
// inputSource is 1 to read from the data port and 0 to read from status port
void ConfigurePort(uint port, int inputSource)
{
	LPT_BASE = port;		// set port number for all operations

	outportb(DATA_PORT, 0x5A);	// flag to see if input mode is working

	// ECP mode is not available on 0x3BC
	if (LPT_BASE != 0x3BC && inputSource)
	{
		// need to reconfigure ECP to SPP bidirectional mode
		outportb(ECP_CONTROL_PORT, 0x20);	// set to byte mode
		outportb(CONTROL_PORT, 0x24);		// set direction bit to input
		GetTrackBits = GetTrackBitsFromData;
		INPUT_SOURCE = inputSource;
	}
	else
	{
		GetTrackBits = GetTrackBitsFromStatus;
		INPUT_SOURCE = 0;
	}
}

// ****************************************************************************
void ResetPort(void)
{
	// ECP mode is not available on 0x3BC
	if (LPT_BASE != 0x3BC && INPUT_SOURCE)
	{
		outportb(CONTROL_PORT, 0x04);		// set direction bit to output
		// need to reconfigure port back to ECP mode
//		outportb(ECP_CONTROL_PORT, 0x60);	// set to ECP FIFO mode
	}
}

// ****************************************************************************
// this function returns pins 2-9 as bits 0-7
uint GetTrackBitsFromData(void)
{
	return inportb(DATA_PORT);
}

// ****************************************************************************
// this function returns
// bit 0 - pin 15	/ERROR
// bit 1 - pin 13	SELECT
// bit 2 - pin 12	PAPER OUT
// bit 3 - pin 10	/ACK
// bit 4 - pin 11 	/BUSY (See note below)
// bit 5-7 always 0
//
// pin 11 (bit 4 after the shift) is inverted by the hardware so we invert it
// in software so that it reads correctly.
uint GetTrackBitsFromStatus(void)
{
	return ((inportb(STATUS_PORT) >> 3) ^ 0x10);
}


// ****************************************************************************
// Heat() returns 0 on success NZ on error or abort (1 = key hit, -1 = timeout)
int Heat(LaneInfo *laneInfo)
{
	ulong		startClicks,			// when the race started
				stopClicks[MAX_LANES],	// when each car crossed the line
				speedClicks[MAX_LANES];	// when each car cleared the sensor
	uchar		bits;
	enum {INITIAL, SPEED, END}
				state[MAX_LANES];
	int		i,
				done,
				placeCnt;

	// set lane states to their initial condition
	for (i = 0; i < LANES; i++)
		state[i] = INITIAL;

	// check for proper sensor setup
	printf("Waiting for proper sensor state (any key exits)\n");
	do
	{
		done = 1;	// assume all is ready

		printf("Not ready: ");

		// all sensor bits should be zero right now
		if (GetTrackBits() & START_BIT)
		{
			printf("Start sensor");
			done = 0;
		}
		for (i = 0; i < LANES; i++)
		{
			if ((GetTrackBits() & (LANE1_FINISH << i)))
			{
				if (!done)
					printf(", ");
				printf("Lane %d", i + 1);
				done = 0;
			}
		}
		clreol();
		printf("\r");

		// if not ready now, wait for a quarter second before checking again
		if (!done)
			delay(250);

		if (kbhit())
		{
			getch();
			return 1;
		}
	} while (!done);
	clreol();
	printf("All sensors ready -- waiting for start of race\n");

	// wait for start of race
	while ((GetTrackBits() & START_BIT) == 0)
		;
	startClicks = HiResolutionTime();

	printf("The race has started! ... waiting for all cars to cross the finish line\n");

	placeCnt = 1;

	do
	{
		ulong		clicks;

		done = 1;		// assume we are done

		// get current bits and time
		bits = GetTrackBits();
		clicks = HiResolutionTime();

		// now process each lane
		for (i = 0; i < LANES; i++)
		{
			switch(state[i])
			{
			// the race has started looking for this car to cross the finish line
			case INITIAL:
				// look for first 0 to 1 transition (leading edge of car)
				if (bits & (LANE1_FINISH << i))
				{
					laneInfo[i].place = placeCnt++;
					stopClicks[i] = clicks;
					state[i] = SPEED;
				}
				done = 0;	// not done w/ some in initial state
				break;

			// the nose has crossed measure the car's speed by measuring how long
			// it takes for the entire car to cross
			case SPEED:
				// look for last 1 to 0 transition (trailing edge of car)
				if ((bits & (LANE1_FINISH << i)) == 0)
				{
					speedClicks[i] = clicks;
					state[i] = END;
				}
				done = 0; // never done if we are in the speed state
				break;

			case END:
				// if we go back to a 1 (sensor covered) then revert to speed state
				if (bits & (LANE1_FINISH << i))
				{
					state[i] = SPEED;
				}
				else
				// check if it has been long enough to assume we have the last one
				// a time of 700,000 would allow enough time for a 7 inch car to
				// pass the sensor while traveling at only 1 foot per second
				if (clicks - speedClicks[i] < 700000L)
				{
					done = 0;	// not done until finished with END state
				}
				break;
			}
		}

		// check to see if race is running too long
		if (clicks - startClicks > TIMEOUT * 1193182L)	// over timeout value
		{
			printf("\a\n\nRace time limit exceeded!  Race aborted!\n\n");
			return -1;	// return failure!
		}
	} while (!done);

	printf("The race is over, all lanes are clear\n"
			"\n"
			"Lane  Car  Time(.0001)  Place  Speed(scale MPH)\n");
	for (i = 0; i < LANES; i++)
	{
		// assume the car traveled 15 scale feet (the size of a real car) in the
		// time it took to cross the sensor.  Generate scale MPH from that.
		// equation used:
		// (feet / feetPerMile) = miles
		// (clicks / clicksPerHour) = hours
		// so ...
		// (feet / feetPerMile) / (clicks / clicksPerHour) = miles per hour
		// this is the same as inverting the second group and multiplying so ...
		// (feet / feetPerMile) * (clicksPerHour / clicks) = miles per hour
		// substituting real numbers gives you
		// (15 / 5280) * ((1193182*60*60) / clicks) = scale MPH
		// 15 = scale feet
		// 5280 = feet per mile
		// 1193182*60*60 = clicks per second * seconds per minute * minutes per hour
		// rearranging gets:
		// ((15 * 1193182*60*60) / 5280) / clicks = scale MPH
		// reducing the constants gets:
		// 12202997.73 / clicks = scale MPH
		// I rounded the ...7.73 up to ...8

		laneInfo[i].laneTime = stopClicks[i] - startClicks;
		laneInfo[i].scaleSpeed = (int) (12202998L / (speedClicks[i] - stopClicks[i]));

		printf(" %-3d  %3d  %9ld    %3d    %d\n",
				i + 1,								// lane
				laneInfo[i].carID,				// car #
				laneInfo[i].laneTime / 119,	// time
				laneInfo[i].place,				// place
				laneInfo[i].scaleSpeed);		// speed

	}

	return 0;	// successful
}

// ****************************************************************************
// compare function for qsort() to call
int _USERENTRY CarTimeCompare(const void *elem1, const void *elem2)
{
	const CarData
				*car1, *car2;

	car1 = (const CarData *) elem1;
	car2 = (const CarData *) elem2;

	if (car1->totalTime == car2->totalTime)
		return 0;
	else
		return (car1->totalTime < car2->totalTime) ? -1 : 1;
}

// ****************************************************************************
#define CarIndex(x)	((x) >= cars ? (x) - cars : (x))
void Race(void)
{
	LaneInfo	laneInfo[MAX_LANES];		// storage for each race's results
	CarData	car[MAXCARS];
	int		cars,
				i,
				done;

	// clear the car memory to all zeros
	memset(car, 0, sizeof(CarData)*MAXCARS);

	// now set the ID field of each car entry
	for (i = 0; i < MAXCARS; i++)
		car[i].ID = i + 1;

	clrscr();
	printf("Race mode\n\n");

	do
	{
		cars = 0;
		printf("How many cars [%d-%d]: ", LANES, MAXCARS);
		scanf("%d", &cars);

		if (cars > MAXCARS || cars < LANES)
		{
			cars = 0;
			printf("\aERROR: You must enter a number between %d and %d\n", LANES, MAXCARS);
		}
	} while (cars < LANES);

	// run each heat (there will be as many heats as there are cars)
	for (i = 0; i < cars; i++)
	{
		int		j;

		// identify the cars in this heat & set LaneInfo.carID for each lane
		printf("\nThe cars in this heat are:\n"
				"Lane Car\n");
		for (j = 0; j < LANES; j++)
			printf(" %-3d %d\n", j + 1, (laneInfo[j].carID = car[CarIndex(i + j)].ID));

		// run the heat
		if (Heat(laneInfo) == 0)
		{
			char	ch;

			do
			{
				// possibly record the car's time
				printf("Record this race [Y/N]: ");
				ch = getch();
				ch = toupper(ch);
			} while (ch != 'Y' && ch != 'N');
			printf("%c\n", ch);

			if (ch == 'Y')
			{
				// set CarData fields from LaneInfo information
				for (j = 0; j < LANES; j++)
				{
					CarData	*c;

					c = &car[CarIndex(i + j)];		// for efficiency

					c->totalTime += (c->laneTime[j] = laneInfo[j].laneTime);
					c->scaleSpeed[j] = laneInfo[j].scaleSpeed;
					c->place[j] = laneInfo[j].place;
				}
			}
			else
				goto InvalidRace;
		}
		else
		{
InvalidRace:
			// invalid race
			char	ch;

			// clear keyboard buffer first
			while (kbhit())
				getch();

			do
			{
				printf("Continue race [Y/N]: ");
				ch = getch();
				ch = toupper(ch);
			} while (ch != 'Y' && ch != 'N');
			printf("%c\n", ch);

			if (ch == 'Y')
			{
				i--;	// decrement I to rerun the race
				continue;
			}
			else
			{
				printf("\n\nPartial race results\n");
				break;
			}
		}
	}

	// sort the results by time
	qsort(car, cars, sizeof(CarData), CarTimeCompare);

	// only update lane info if not a partial race
	if (i >= cars)
		UpdateLaneInformation(cars, car);

	ResetPort();	// reconfigure the LPT port so the printer will work

	done = 0;
	do {
		// the printer is the STDPRN device
		FILE	*printer = stdprn;

		// present race menu
		printf("  -- Race Results Menu --\n"
				"\n"
				" 1  - Display race results\n"
				" 2  - Print race results\n"
				" 3  - Print individual race sheets\n"
				"ESC - Return to main menu\n"
				"\n"
				"Your choice: ");

		switch(getch())
		{
		case '1':
			// display race results
			printf("\nCar  Avg. time  Total clicks\n");
			for (i = 0; i < cars; i++)
			{
				printf("%3d  %9lu  %12lu\n",
						car[i].ID,
						(car[i].totalTime / 119) / LANES,
						car[i].totalTime);

				// pause the display every 20 lines
				if (i && i % 20 == 0)
				{
					printf("Press any key to continue ... ");
					// flush keyboard
					while (kbhit())
						getch();
					// wait for a key
					getch();
					printf("\n");
				}
			}
			break;

		case '2':	// print race results
			fprintf(printer, "Car  Avg. time  Total clicks\r\n");
			fprintf(printer, " #   (seconds)  1193182/sec \r\n");
			fprintf(printer, "---  ---------  ------------\r\n");

			for (i = 0; i < cars; i++)
			{
				fprintf(printer, "%3d  %4lu.%04lu  %12lu\r\n",
						car[i].ID,
						((car[i].totalTime / 119) / LANES) / 10000L,
						((car[i].totalTime / 119) / LANES) % 10000L,
						car[i].totalTime);
			}

			fprintf(printer, FORM_FEED);	// go to top of form
			break;

		case '3':	// print individual race sheets
			for (i = 0; i < cars; i++)
			{
				int	j,
						totalSpeed;

				fprintf(printer,
						"\r\n"
						"\r\n"
						"%33sPinewood Derby\r\n"
						"\r\n"
						"Car #: %3d   Driver Name: ____________________   Car Name: ____________________\r\n"
						"\r\n",
						"", car[i].ID);

				fprintf(printer, "Lane        |");
				for (j = 0; j < LANES; j++)
					fprintf(printer, "   %2d   |", j + 1);
				fprintf(printer, "  Avg.  |\r\n");

				fprintf(printer, "Time (secs) |");
				for (j = 0; j < LANES; j++)
					fprintf(printer, " %1ld.%04ld |",
						(car[i].laneTime[j] / 119) / 10000L,
						(car[i].laneTime[j] / 119) % 10000L);
				fprintf(printer, " %1ld.%04ld |\r\n",
						((car[i].totalTime / 119) / LANES) / 10000L,
						((car[i].totalTime / 119) / LANES) % 10000L);

				fprintf(printer, "Scale speed |");
				for (totalSpeed = 0, j = 0; j < LANES;
						totalSpeed += car[i].scaleSpeed[j], j++)
					fprintf(printer, " %6d |", car[i].scaleSpeed[j]);
				fprintf(printer, " %6d |\r\n", totalSpeed / LANES);

				fprintf(printer, "Place       |");
				for (j = 0; j < LANES; j++)
					fprintf(printer, "   %2d   |", car[i].place[j]);
				fprintf(printer, "\r\n");

				fprintf(printer, "\r\n");
			}

			fprintf(printer, FORM_FEED);	// go to top of form
			break;

		case 'x':
		case 'X':
		case 0x1b:	// escape
			done = 1;
			break;

		default:
			printf("\n\aInvalid entry\n");
			break;
		}
		printf("\n\n");
	} while (!done);
}


// ****************************************************************************
void OpenRace(void)
{
	int		i;

	LaneInfo	laneInfo[MAX_LANES];		// storage for each race's results

	clrscr();
	printf("Open racing mode\n\n");

	for (i = 0; i < LANES; i++)
		laneInfo[i].carID = 0;

	// loop on timeout (returns -1) or success (0), exit on key hit (1)
	while (Heat(laneInfo) <= 0)
		printf("To exit open racing leave the start lever down and press a key\n");
}


// ****************************************************************************
// Update the disk file w/ the per-lane data from this race
// Format of per-lane data file:
//	timestamp, # cars, lane 1 avg. time, lane 1 std. deviation, ...
void UpdateLaneInformation(int cars, CarData *car)
{
	FILE	*laneInfo;
	int		i,
			j;

	laneInfo = fopen("LANEINFO.DAT", "at");
	if (laneInfo == NULL)
	{
		printf("Could not open/create LaneInfo.dat file\n");
		return;
	}

	// print the header for this line (timestamp, # cars)
	fprintf(laneInfo, "%lu, %d", time(NULL), cars);

	for (j = 0; j < LANES; j++)
	{
		ulong	avg,
				stdDev;

		// get avg time for the lane
		for (i = 0, avg = 0; i < cars; i++)
			avg += car[i].laneTime[j] / 119;
		avg /= cars;	// calculate average

		// now calculate standard deviation
		for (i = 0, stdDev = 0; i < cars; i++)
		{
			// use the absolute value of the deviation
			if (avg < (car[i].laneTime[j] / 119))
				stdDev += (car[i].laneTime[j] / 119) - avg;
			else
				stdDev += avg - (car[i].laneTime[j] / 119);
		}
		stdDev /= cars;

		fprintf(laneInfo, ",  %lu, %lu", avg, stdDev);
	}

	fprintf(laneInfo, "\n");

	fclose(laneInfo);
}	


// ****************************************************************************
char *ToBinary(uchar bits)
{
	static char	text[9];
	int	i;

	for (i = 0; i < 8; i++)
	{
		text[i] = (bits & (1 << (7 - i))) ? '1' : '0';
	}
	text[8] = '\0';

	return text;
}

// ****************************************************************************
void Diagnostics(void)
{
	uchar		bits;
	ulong		startClicks,
				clicks,
				maxClicks = 0;

	clrscr();

	printf("Port Diagnostics\n\n");

	printf("Configuration:\nLanes:     %d\nLPT Port:  0x%03X\nInput:     %s\nTimeout:   %u (secs)\n\n", 
			LANES, LPT_BASE, 
			INPUT_SOURCE ? "Data (pins 2-9)" : "Status (pins 15, 13, 12, 10, 11)",
			TIMEOUT);

	if (LANES > 4 && !INPUT_SOURCE)
		printf("* * * WARNING: Too many lanes for input source\n\n");

	printf("Bits     Clicks\n");
	while (!kbhit())
	{
		startClicks = HiResolutionTime();
		bits = GetTrackBits();
		clicks = HiResolutionTime() - startClicks;

		if (maxClicks < clicks)
			maxClicks = clicks;

		printf("%s %-10lu\r", ToBinary(bits), clicks);
	}
	getch();

	printf("\n\nMax clicks = %lu\n", maxClicks);
}

void SetTimerMode2(void)
{
	disable();						// no one else must touch timer chip
	outportb(0x43, 0x34);		// set channel 0, mode 2
   outportb(0x40, 0);			// reset divisor to 65536 (0,0 = 65536)
   outportb(0x40, 0);
   enable();
}

// The PC timer runs at 1,193,182 / 65536 = 18.20651245 ticks per second.
// The click counter runs at 1,193,182 per second and wraps at 16 bits.
// The total clicks can be obtained by TICKS * 65536 + CLICKS

// Milliseconds can be caluclated by total clicks / 1193

/****************************************************************************/
#define	BIOS_TIMER	0x46C


/****************************************************************************/
// return total clicks value
ulong HiResolutionTime(void)
{
	uint	clicks;
	ulong	ticks,
			retVal;
	ulong far *
			biosTicks = (ulong far *) MK_FP(0, BIOS_TIMER);
	static ulong
			lastTime = 0;	// memory for our last returned value

retry:
	ticks = *biosTicks;				// read bios tick count
	disable();						// no one must touch 8253
	outportb(0x43, 0);				// latch counter value for counter 0
	clicks = inportb(0x40);			// read lsb counter 0
	clicks += inportb(0x40) << 8;	// read msb counter 0
	enable();						// ok to touch 8253 now

	clicks = 65535U - clicks;		// convert from count down to count up

	if (ticks != *biosTicks)		// did tick elapse while we processed?
		goto retry;					// if so, try again


	// return total clicks by ticks * 65536 + clicks
	retVal = (ticks << 16) + clicks;

	// check for a missed tick value
	if (retVal < lastTime &&
		(retVal & 0xFFFF0000L) == (lastTime & 0xFFFF0000L))
	{
#if 0
		fprintf(stderr, "\n\nHiResolutionTime: Missed a tick!\n");
		fprintf(stderr, "\tlastTime = %lX, retVal = %lX, ticks = %lX, clicks = %X\n",
				lastTime, retVal, ticks, clicks);
#endif
		(*biosTicks)++;	// add a tick!
		goto retry;
	}

	lastTime = retVal;

	return retVal;
}

// ****************************************************************************
// ****************************************************************************


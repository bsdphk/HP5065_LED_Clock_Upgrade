/**************************************************************************/
/*!
    @file     main.c

    @section LICENSE

    Software License Agreement (BSD License)

    Copyright (c) 2013, K. Townsend (microBuilder.eu)
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holders nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/**************************************************************************/
#include <stdio.h>
#include "LPC8xx.h"
#include "gpio.h"
#include "mrt.h"
#include "uart.h"

/* Push function before CRP */

/*---------------------------------------------------------------------
 * Disable PIO0_1 boot-loader entry
 */

__attribute__ ((section(".crp"), used))
    const unsigned CRP_WORD = 0x4e697370;

/*---------------------------------------------------------------------
 * Firmware API
 */

#define IAP_LOCATION	0x1fff1ff1
static unsigned int iap_cmd[5];
static unsigned int iap_status[4];
typedef void (*IAP)(unsigned int[5], unsigned int[4]);
static IAP iap_entry = (IAP)IAP_LOCATION;

#define FREQ	5000000				// Input clock frequency

extern volatile uint32_t mrt_counter;

/*---------------------------------------------------------------------
 * Watchdog (pg 171)
 */

static void
fido_setup(unsigned tmo)
{
	LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 17);	// WWDT
	LPC_SYSCON->PDRUNCFG &= ~(1 << 6);	// WDTOSC_PD
	LPC_WWDT->MOD = 0
		| (1 << 0)			// WDEN
		| (1 << 1)			// WDRESET
		;
	LPC_WWDT->TC = tmo;
	LPC_WWDT->FEED = 0xaa;
	LPC_WWDT->FEED = 0x55;
}

static void
fido_pat(void)
{
	LPC_WWDT->FEED = 0xaa;
	LPC_WWDT->FEED = 0x55;
}

/*---------------------------------------------------------------------
 * UART banner
 */

static void
welcome(void)
{
	printf("\r\n\r\nHP5065A Clock Upgrade\r\n");
	printf("(c) 2016 Poul-Henning Kamp\r\n");
	printf("Beerware licensed.\r\n");
	iap_cmd[0] = 54;
	iap_entry(iap_cmd, iap_status);
	printf("\r\nPart ID: %x (%x)\r\n", iap_status[1], iap_status[0]);
	iap_cmd[0] = 55;
	iap_entry(iap_cmd, iap_status);
	printf("Bootloader Ver: %d.%d (%x)\r\n",
	    (iap_status[1] >> 8) & 0xff,
	    iap_status[1] & 0xff, iap_status[0]);
	iap_cmd[0] = 58;
	iap_entry(iap_cmd, iap_status);
	printf("UID = [0x%08x, 0x%08x, 0x%08x, 0x%08x]\r\n",
	    iap_status[0], iap_status[1], iap_status[2], iap_status[3]);
	printf("SystemCoreClock = %d Hz\r\n", (int)SystemCoreClock);
	printf("CRP 0x%x\r\n", CRP_WORD);
}


/*---------------------------------------------------------------------
 */

static void
uart_enable(void)
{
	LPC_SWM->PINASSIGN0 =
	    (0xffU << 24) |	// U0_CTS_I disabled
	    (0xffU << 16) |	// U0_RTS_O disabled
	    (0x00U <<  8) |	// U0_RXD_I PIO0_0
	    (0x04U <<  0);	// U0_TXD_O PIO0_4
}

static void
uart_disable(void)
{
	LPC_SWM->PINASSIGN0 =
	    (0xffU << 24) |	// U0_CTS_I disabled
	    (0xffU << 16) |	// U0_RTS_O disabled
	    (0xffU <<  8) |	// U0_RXD_I PIO0_0
	    (0xffU <<  0);	// U0_TXD_O PIO0_4
}

/*---------------------------------------------------------------------
 */

/* These are calibrated to look like a 0x00 byte at 115200 bps */
static int pulse_lo = 177;
static int pulse_hi = 17;

static void
pulse(void)
{
	int i;

	for (i = pulse_lo; i > 0; i--)
		LPC_GPIO_PORT->CLR0 |= (1 << 4);
	for (i = pulse_hi; i > 0; i--)
		LPC_GPIO_PORT->SET0 |= (1 << 4);
}

/*---------------------------------------------------------------------
 * Send 86400 pulses which look like NUL at 115200,8,N,1
 * If along the way the "Minute" switch is pressed,
 * (re)invoke the bootloader
 */

static void
check_bootloader(void)
{
	int i;

	LPC_GPIO_PORT->DIR0 |= (1 << 4);
	for (i = 0; i < 86400; i++) {
		if (!(LPC_GPIO_PORT->PIN0 & (1<<3))) {
			fido_setup(0x10000);
			iap_cmd[0] = 57;
			iap_entry(iap_cmd, iap_status);
		}
		pulse();
	}
	LPC_GPIO_PORT->DIR0 &= ~(1 << 4);
}

/*---------------------------------------------------------------------
 */

static void
sct_stop(void)
{
	LPC_SCT->CTRL_L |= (1 << 2);		// Halt
}

static void
sct_start(void)
{
	LPC_SCT->CTRL_L &= ~(1 << 2);		// Halt
}


/* pg 133 */
static void
sct_setup(unsigned pin_in)
{
	LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 8);		// SCT

	if (pin_in == 0xff) {
		LPC_SCT->CONFIG =
		    (1 << 0) |			// UNIFIED
		    (0 << 1) |			// CLOCKMODE = sysclk
		    (1 << 9) |			// INSYNC[0]
		    (1 << 17);			// Auto limit
	} else {
		LPC_SCT->CONFIG =
		    (1 << 0) |			// UNIFIED
		    (2 << 1) |			// CLOCKMODE = Input
		    (1 << 9) |			// INSYNC[0]
		    (1 << 17);			// Auto limit
	}

	LPC_SCT->MATCHREL[0].U = 	FREQ - 1;

	/* Calibrated to look like 0x00 at 115200 bps */
	LPC_SCT->MATCHREL[1].U = 	FREQ - 392;

	LPC_SCT->EVENT[0].STATE =	~0;
	LPC_SCT->EVENT[0].CTRL =	(1 << 12);
	LPC_SCT->EVENT[1].STATE =	~0;
	LPC_SCT->EVENT[1].CTRL =	(1 << 0) | (1 << 12);

	LPC_SCT->OUT[0].SET =		(1 << 0);
	LPC_SCT->OUT[0].CLR =		(1 << 1);

	sct_start();

	LPC_SWM->PINASSIGN5 =
	    (pin_in << 24) |	// CT_IN_0_I
	    (0xffU << 16) |
	    (0xffU <<  8) |
	    (0xffU <<  0);

}


void
sct_output(unsigned pin_out)
{
	LPC_SWM->PINASSIGN6 =
	    (pin_out << 24) |	// CT_OUT_0_O
	    (0xffU << 16) |
	    (0xffU <<  8) |
	    (0xffU <<  0);
}

/*---------------------------------------------------------------------
 */

static void
run_pulse(void)
{

	while(LPC_SCT->COUNT_U > FREQ - 1000)
		continue;
	while(LPC_SCT->COUNT_U < 1000)
		continue;
	LPC_GPIO_PORT->SET0 = (1<<4);
	LPC_GPIO_PORT->DIR0 |= (1<<4);
	sct_output(0xff);
	pulse();
	sct_output(4);
	LPC_GPIO_PORT->DIR0 &= ~(1<<4);
}

/*---------------------------------------------------------------------
 */

int
main(void)
{
	int i, j, k, l;
	unsigned button[3];
	unsigned steps[3];
	unsigned hz;

	/*-------------------------------------------------------------
	 * First things first:  Start the watchdog
	 */

	fido_setup(0x1000);

	/*-------------------------------------------------------------
	 * Setup GPIO
	 */

	LPC_SYSCON->SYSAHBCLKCTRL |=  (1 << 6);			// GPIO
	LPC_SYSCON->PRESETCTRL    &= ~(1 << 10);		// GPIO reset
	LPC_SYSCON->PRESETCTRL    |= (1 << 10);

	LPC_IOCON->PIO0_0 = 0
		| (1 << 3)					// Pull down
		| (1 << 5)					// Hysteresis
		;

	LPC_IOCON->PIO0_1 = 0
		| (2 << 3)					// Pull up
		| (1 << 5)					// Hysteresis
		;

	LPC_IOCON->PIO0_2 = 0
		| (2 << 3)					// Pull up
		| (1 << 5)					// Hysteresis
		;

	LPC_IOCON->PIO0_3 = 0
		| (2 << 3)					// Pull up
		| (1 << 5)					// Hysteresis
		;

	LPC_IOCON->PIO0_5 = 0
		| (2 << 3)					// Pull up
		| (1 << 5)					// Hysteresis
		;

	/*-------------------------------------------------------------
	 * Provide a chance to enter the boot-loader
	 */

	check_bootloader();

	/*-------------------------------------------------------------
	 * Enable UART for startup-debugging
	 */

	uart0Init(115200);
	uart_enable();

	/*-------------------------------------------------------------
	 * Start timer
	 */

	mrtInit(__SYSTEM_CLOCK/1000);

	/*-------------------------------------------------------------
	 * Enable reset pin
	 */

	LPC_SWM->PINENABLE0 = ~(1 << 6);		// PIO0_5 RESET_EN

	/*-------------------------------------------------------------
	 * Hello World
	 */

	welcome();

	/*-------------------------------------------------------------
	 * Detect pin-configuration by counting edges on the two possible
	 * possible clock inputs.
 	 *
	 * If neither is active the watch-dog will trigger.
	 */

	do {
		/* Measure input frequency on PIO0_1 and PIO0_2 */
		for (i = 1; i < 3; i++) {
			sct_setup(i);
			k = LPC_SCT->COUNT_U;
			mrtDelay(100);
			l = LPC_SCT->COUNT_U;
			hz = 10 * (l-k);
			printf("Rate PIO0_%d = %u\r\n", i, hz);
			if (hz > 1000)
				break;
		}
	} while (hz < 1000);

	/*-------------------------------------------------------------
 	 * Configure counter
	 */

	mrtDelay(100);
	printf("Using PIO0_%d\r\n", i);
	mrtDelay(100);
	uart_disable();

	button[0] = 1<<(3-i);	steps[0] = 1;
	button[1] = 1<<3;	steps[1] = 60;
	button[2] = 1<<5;	steps[2] = 3600;
	if (i == 1) {

		LPC_SWM->PINENABLE0 &= ~(1 << 7);       // Enable CLKIN

		LPC_SYSCON->MAINCLKSEL = 0x1;		// PLL input
		LPC_SYSCON->MAINCLKUEN = 0x0;
		LPC_SYSCON->MAINCLKUEN = 0x1;

		LPC_SYSCON->SYSPLLCLKSEL = 0x3;		// CLKIN
		LPC_SYSCON->SYSPLLCLKUEN = 0x0;
		LPC_SYSCON->SYSPLLCLKUEN = 0x1;

		LPC_SYSCON->SYSAHBCLKDIV = 1;

		sct_setup(0xff);
		mrtInit(FREQ/1000);

		/* Calibrated to look like 0x00 at 115200 bps */
		pulse_lo = 29;
		pulse_hi = 10;
	} else {
		sct_setup(2);
	}

	sct_output(4);

	/*-------------------------------------------------------------
	 * Until the clock is set, have it run 11 times too fast
	 */
	while (1) {
		fido_pat();
		if (!(LPC_GPIO_PORT->PIN0 & button[0]))
			break;
		if (!(LPC_GPIO_PORT->PIN0 & button[1]))
			break;
		if (!(LPC_GPIO_PORT->PIN0 & button[2]))
			break;
		mrtDelay(100);
		run_pulse();
	}

	LPC_SWM->PINENABLE0 |= (1 << 6);		// Disable RESET

	j = 0;
	while(1) {
		if (j == 0 && LPC_SCT->COUNT_U < 10000) {
			fido_pat();
			j = 1;
		}
		if (j == 1 && LPC_SCT->COUNT_U > 10000) {
			j = 0;
		}

		for (i = 0; i < 3; i++) {
			if (LPC_GPIO_PORT->PIN0 & button[i])
				continue;

			fido_pat();

			for(k = 0; k < steps[i]; k++)
				run_pulse();

			if (i > 0) {
				/*
				 * Min/Hour button release
				 * If you hold them for 10+ seconds
				 * The watch-dog bites
				 */
				for(k = 0; k < 1000; k++) {
					if (LPC_GPIO_PORT->PIN0 & button[i])
						continue;
					k = 0;
				}
				continue;
			}

			/* Check if the Sec button is held for >1s */

			mrt_counter = 0;
			for(k = 0; k < 1000; k++) {
				if (LPC_GPIO_PORT->PIN0 & button[i])
					break;
				if (mrt_counter > 1000)
					break;
				k = 0;
			}
			if (k == 1000)
				continue;

			/* Stop counter */
			sct_stop();

			/*
			 * Restart counter on button release
			 * or sync input
			 */
			for(k = 0; k < 1000; k++) {
				if (LPC_GPIO_PORT->PIN0 & button[i])
					break;
				l = LPC_GPIO_PORT->PIN0 & (1<<0);
				if (l)
					k = 0;
			}

			l = (1 << 0) | button[i];
			while (!(LPC_GPIO_PORT->PIN0 & l))
				continue;

			/*
			 * Calibrated for falling edge = PPI rising edge
			 * PPSO comes 164ns after PPS1
			 */
			LPC_SCT->COUNT_U = FREQ - 366;
			LPC_SCT->CTRL_L &= ~(1 << 2);		// Start

			for(k = 0; k < 1000; k++) {
				if (LPC_GPIO_PORT->PIN0 & button[i])
					continue;
				k = 0;
			}
		}
	}
}

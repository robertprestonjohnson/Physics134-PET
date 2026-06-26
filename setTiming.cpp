// Load the registers on the DS1023 timing chips to set the PET coincidence width

#include <stdio.h>
#include <stdlib.h>

#include "PET.h"
#include "rp.h"

// D is output from connector E1, pin 7, DIO2_P
// CLK is output from connector E1, pin 6, DIO1_N
// LE is output from connector E1, pin 5, DIO1_P
// OE is output from connector E1, pin 8, DIO2_N
// Q is input to connector E1, pin 9, DIO3_P

// To write register contents to the two DS1023 chips
// * Start with CLK and D low
// * hold OE high
// * set LE high
// * send 16 clock pulses with a 1 ms period
// * align the 16 data bits with the clock, so that the clock transition
//   is slightly after the data edge
// * set LE low to hold the data

#define PET_D_PIN RP_DIO2_P
#define PET_CLK_PIN RP_DIO1_N
#define PET_LE_PIN RP_DIO1_P
#define PET_OE_PIN RP_DIO2_N
#define PET_Q_PIN RP_DIO3_P

int PET::setTiming(int ichanA, int ichanB) {
    if (ichanA < 1 || ichanB < 1 || ichanA > 255 || ichanB > 255) {
        printf("setTiming: values %d and/or %d are not allowed. Must be 1 through 255\n",ichanA,ichanB);
        return EXIT_FAILURE;
    }
    char chanA = ichanA;
	char chanB = ichanB;

    int unsigned period = 1000; // uS

    char setting[16];
    char settingQ[16];
    char mask = 1;
    for (int i=7; i>=0; --i) {
        setting[i] = (chanA & mask)>>(7-i);
        mask = mask<<1;
        //printf("%d  %d   %x   %x\n",i,setting[i],mask,chanA);
    }
    mask = 1;
    for (int i=7; i>=0; --i) {
        setting[i+8] = (chanB & mask)>>(7-i);
        mask = mask<<1;
    }
    printf("setTiming: set channel A to %d = ",chanA);
    for (int i=0; i<8; ++i) printf("%d",setting[i]);
    printf("\n");
    printf("setTiming: set channel B to %d =  ",chanB);
    for (int i=8; i<16; ++i) printf("%d",setting[i]);
    printf("\n");
    UARTmsg("STATUS: setting channel A and B timing to " + to_string(chanA) + " and " + to_string(chanB) + "\n");
    
    rp_pinState_t state;
    rp_pinState_t levels[2];
    
    levels[0] = RP_LOW;
    levels[1] = RP_HIGH;

    // configure DIO pins
    rp_DpinSetDirection(PET_D_PIN, RP_OUT);
    rp_DpinSetDirection(PET_CLK_PIN, RP_OUT);
    rp_DpinSetDirection(PET_LE_PIN, RP_OUT);
    rp_DpinSetDirection(PET_OE_PIN, RP_OUT);
    rp_DpinSetDirection(PET_Q_PIN, RP_IN);

    // set initial pin levels
    rp_DpinSetState(PET_D_PIN, RP_LOW);
    rp_DpinSetState(PET_CLK_PIN, RP_LOW);
    rp_DpinSetState(PET_OE_PIN, RP_HIGH);
    rp_DpinSetState(PET_LE_PIN, RP_HIGH);
    
    // loop over 16 clock cycles
    // load the DS1023 chips MSB first, LSB last
    for (int i=0; i<16; ++i) {
        int j = (int)setting[i];
        rp_DpinSetState(PET_D_PIN, levels[j]);
        usleep(20);
        rp_DpinSetState(PET_CLK_PIN, RP_HIGH);
        usleep(period/2 - 20);
        if (i == 15) rp_DpinSetState(PET_LE_PIN, RP_LOW);
        rp_DpinSetState(PET_CLK_PIN, RP_LOW);
        usleep(period/2);
    }
    usleep(period);
    
    // Now read back the register contents (non-destructive read) and check
	// This is done twice to make sure that the register was reloaded correctly
	// during the first read operation.
    bool good = true;
    for (int k=0; k<2; ++k) {
        rp_DpinSetState(PET_CLK_PIN, RP_LOW);
        rp_DpinSetState(PET_OE_PIN, RP_LOW);
        rp_DpinSetState(PET_LE_PIN, RP_HIGH);
        for (int i=0; i<16; ++i) {
            rp_DpinGetState (PET_Q_PIN, &state);
            if (state == RP_HIGH) settingQ[i] = 1;
            else settingQ[i] = 0;
			usleep(20);
            rp_DpinSetState(PET_CLK_PIN, RP_HIGH);
            usleep(period/2 - 20);
            rp_DpinSetState(PET_CLK_PIN, RP_LOW);
            if (i == 15) rp_DpinSetState(PET_LE_PIN, RP_LOW);
            usleep(period/2);
        }

        printf("setTiming: returned register values, iteration %d:  ",k);
        for (int i=0; i<16; ++i) {
            printf("%d", settingQ[i]);
            if (i == 7) printf("  ");
            if (settingQ[i] != setting[i]) good = false;
        }
        printf("\n");
    }
    if (!good) {
        printf("setTiming: register setting returned does not match values loaded\n");
    }
    else {
        UARTmsg("STATUS: timing registers read back exactly what was written.\n");
    }

    if (!good) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
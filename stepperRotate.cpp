// Red Pitaya PET API for rotating the stage by a given angle in the specified direction
//
// Author: Robert P. Johnson, January 2026
//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#include "PET.h"
#include "rp.h"

int PET::stepperRotate (float angle, char const* sense) {
    // angle: rotation angle in degrees
    // sense: rotation direction: forward or backward
    
    snprintf(logmsg, sizeof(logmsg), "stepperRotate: rotating the stage %f degrees in the %s direction\n", angle, sense);
	logger(logmsg);

    // NEMA 23 stepper motor has a standard step angle of 1.8 degrees, for 200 steps per full revolution
    // The stepper motor controller can have microsteps from 1 to 125 depending on switch settings
    // The worm gear of the rotation table has a reduction ratio of 180

    static double microsteps = 8.0;      // Switches 5,6,7,8 are off off on on
    double degPerStep = (1.8 / microsteps) / 180.;       

    // Drive the stepper motor controller to make one microstep per millisecond
    #define DLY 430 //pulse length in microseconds  (430 shows as 500 microseconds on the oscilloscope)

    int numSteps = floor(angle / degPerStep);
    #ifdef VERBOSE
        float time = (float)numSteps / 1000.;
        printf("stepperRotate: the stage rotation will require %d steps in %f seconds.\n", numSteps, time);
    #endif
    // Defining the Red Pitaya digital pins
    rp_dpin_t STEP_PIN = RP_DIO3_N;
    rp_dpin_t DIR_PIN = RP_DIO6_N;

    // Setting Red Pitaya digital pins as outputs
    rp_DpinSetDirection(STEP_PIN, RP_OUT);
    rp_DpinSetDirection(DIR_PIN, RP_OUT);

    if (strcmp(sense,"forward") == 0) {
        rp_DpinSetState(DIR_PIN, RP_HIGH);
    } else {
        rp_DpinSetState(DIR_PIN, RP_LOW);
    }
    if (angle != 0.) {  
        double angleCovered = numSteps * degPerStep;
        for (int i=0; i <= numSteps; i++) {             
            rp_DpinSetState(STEP_PIN, RP_HIGH);    
            usleep(DLY);      // This code creates square pulses of about 1 ms period to drive the stepper
            #ifdef VERBOSE
                if (i%1000 == 0) printf("Step %d . . .\n",i);
            #endif
            rp_DpinSetState(STEP_PIN, RP_LOW);
            usleep(DLY);
        }   
        #ifdef VERBOSE
            printf("Completed rotation by %f degrees.\n", angle);
        #endif
        UARTmsg("STATUS: stage successfully rotated by " + to_string(angleCovered) + " degrees.\n");
    } 
    UARTmsg("STATUS: the stage was rotated by " + to_string(angle) + " degrees\n");
    return EXIT_SUCCESS;
}
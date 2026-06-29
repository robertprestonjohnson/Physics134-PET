// Red Pitaya API for rotating the stage a given angle
//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#include "PET.h"
#include "rp.h"

int PET::stepperRotate (float angle) {
	printf("stepperRotate: rotating the stage %f degrees\n",angle);

	// NEMA 23 stepper motor has a standard step angle of 1.8 degrees, for 200 steps per full revolution
	// The stepper motor controller can have microsteps from 1 to 125 depending on switch settings
	// The worm gear of the rotation table has a reduction ratio of 180

	static double microsteps = 8.0;      // Switches 5,6,7,8 are off off on on
	double degPerStep = (1.8 / microsteps) / 180.;       

	// Drive the stepper motor controller to make one microstep per millisecond
	#define DLY 430 //pulse length in microseconds  (430 shows as 500 microseconds on the oscilloscope)

	int numSteps = floor(angle / degPerStep);
	float time = (float)numSteps / 1000.;
	if (verbose) printf("stepperRotate: the stage rotation will require %d steps in %f seconds.\n", numSteps, time);

	// Defining the Red Pitaya digital pin
	rp_dpin_t STEP_PIN = RP_DIO3_N;

	// Setting Red Pitaya digital pins as inputs and outputs
	rp_DpinSetDirection(STEP_PIN, RP_OUT);

	if (angle != 0.) {  
		double angleCovered = numSteps * degPerStep;
		for (int i=0; i <= numSteps; i++) {				
			rp_DpinSetState(STEP_PIN, RP_HIGH);    
			usleep(DLY);      // This code creates square pulses of about 1 ms period to drive the stepper
			if (verbose && i%1000 == 0) printf("Step %d . . .\n",i);
			rp_DpinSetState(STEP_PIN, RP_LOW);
			usleep(DLY);
		}	
		if (verbose) printf("Completed rotation by %f degrees.\n", angle);
		UARTmsg("STATUS: stage successfully rotated by " + to_string(angleCovered) + " degrees.\n");
	} 
	UARTmsg("STATUS: the stage was rotated by " + to_string(angle) + " degrees\n");
	return EXIT_SUCCESS;
}
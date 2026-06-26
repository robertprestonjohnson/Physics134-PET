// Red Pitaya API for moving the linear stage left or right a specified distance
//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#include "PET.h"
#include "rp.h"

int PET::stepperLeftRight (char const* direction, float distance) {
	if (strcmp(direction,"left") == 0 || strcmp(direction,"right") == 0) {
		printf("stepperLeftRight: moving stage %f mm to the %s\n",distance,direction);
	} else {
		printf("stepperLeftRight: bad stage direction %s\n",direction);
		return EXIT_FAILURE;
	}

	// NEMA 23 stepper motor has a standard step angle of 1.8 degrees, for 200 steps per full revolution
	// There is a 10 mm stroke per revolution from the screw of the linear stage
	// The stepper motor controller can have microsteps from 1 to 125 depending on switch settings

	static double microsteps = 8.0;      // Switches 5,6,7,8 are off off on on
	double mmPerStep = 10.0 / (200.0 * microsteps);

	// Drive the stepper motor controller to make one step per millisecond
	// This will give a speed of about 6.25 mm per second or 375 mm per minute
	#define DLY 430 //pulse length in microseconds  (430 shows as 500 microseconds on the oscilloscope)

	int numSteps = floor(distance / mmPerStep);
	if (verbose) printf("stepperLeftRight: the stage translation will require %d steps.\n", numSteps);

	// Defining the Red Pitaya digital pins
	rp_dpin_t STEP_PIN = RP_DIO5_P;
	rp_dpin_t DIR_PIN = RP_DIO5_N;
	rp_dpin_t STOPPINR = RP_DIO7_P;
	rp_dpin_t STOPPINL = RP_DIO7_N;

	// Setting Red Pitaya digital pins as inputs and outputs
	rp_DpinSetDirection(STEP_PIN, RP_OUT);
	rp_DpinSetDirection(DIR_PIN, RP_OUT);
	rp_DpinSetDirection(STOPPINL, RP_IN);
	rp_DpinSetDirection(STOPPINR, RP_IN);

	rp_pinState_t left, right;    // States of the two stop pins

	if (strcmp(direction,"left") == 0) {  // Move the stage to the left the specified number of steps
		rp_DpinSetState(DIR_PIN, RP_HIGH);    //sets direction low=right high=left
		rp_DpinGetState(STOPPINR, &right);
		rp_DpinGetState(STOPPINL, &left);
		if (verbose) printf("stepperLeftRight: stop pin states at beginning of travel: right=%d, left=%d\n", right, left);
		if (left == RP_HIGH) {
			for (int i=0; i <= numSteps; i++) {				
				rp_DpinSetState(STEP_PIN, RP_HIGH);    
				usleep(DLY);                               // This code creates square pulses of about 1 ms period to drive the stepper
				rp_DpinSetState(STEP_PIN, RP_LOW);
				usleep(DLY);
				rp_DpinGetState(STOPPINL, &left);
				if (left == RP_LOW) {
					double distCovered = i * mmPerStep;
					if (verbose) printf("stepperLeftRight: motion is stopping after %d steps or %f mm due to hitting the end of travel.\n", i, distCovered);
					break;
				}
			}
			rp_DpinGetState(STOPPINR, &right);
			if (verbose) printf("stepperLeftRight: stop pin states at end of travel: right=%d, left=%d\n", right, left);
		} else {
			printf("stepperLeftRight: no motion is possible, as the stage is already at the leftmost limit of travel.\n");
			return EXIT_FAILURE;
		}
	} else {  // Move the stage to the right the specified number of steps
		rp_DpinSetState(DIR_PIN, RP_LOW);    //sets direction low=right high=left
		rp_DpinGetState(STOPPINR, &right);
		rp_DpinGetState(STOPPINL, &left);
		if (verbose) printf("stepperLeftRight: stop pin states at beginning of travel: right=%d, left=%d\n", right, left);
		if (right == RP_HIGH) {
			for (int i = 0; i <= numSteps; i++) {
				rp_DpinSetState(STEP_PIN, RP_HIGH);
				usleep(DLY);
				rp_DpinSetState(STEP_PIN, RP_LOW);
				usleep(DLY);
				rp_DpinGetState(STOPPINR, &right);
				if (right == RP_LOW) {
					double distCovered = i * mmPerStep;
					if (verbose) printf("stepperLeftRight: motion is stopping after %d steps or %f mm due to hitting the end of travel.\n", i, distCovered);
					break;
				}
			}
			rp_DpinGetState(STOPPINR, &left);
			if (verbose) printf("stepperLeftRight: stop pin states at end of travel: right=%d, left=%d\n", right, left);
		} else {
			printf("stepperLeftRight: no motion is possible, as the stage is already at the rightmost limit of travel.\n");
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
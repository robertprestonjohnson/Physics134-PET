/* PET Red Pitaya routine to execute a stepped scan at a given angle
*  Author: Robert P. Johnson    January 16, 2026
*/

#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <math.h>
#include <chrono>
#include "rp.h"
#include "rp_hw.h"
#include "PET.h"

int PET::linearScan(float angle, char const* fn, float x0, float dx, int nStep, float timePerStep, int coincidenceWindow, 
	float sigThr, float pedA, float pedB, float gamMean, float gamSig, float calibA, float calibB) {
	// angle: the manually set scan angle from the rotary stage (for informational purposes only)
	// fn: file name for the output of the coincidence count per stage location
	// x0: initial position of the stage relative to the right-hand stop position
	// dx: step size for the stage from one data acquisition to the next
	// nStep: number of stage positions at which data acquisition is to occur
	// timePerStep: live time for the data acquisition at each step
	// coincidenceWindow: maximum difference in start points of the two channels to define a coincidence
	// sigThr: threshold in noise sigmas to define the start of a gamma-ray pulse
	// pedA: pedestal value for channel A 
	// pedB: pedestal value for channel B 
	// gamMean: mean of the expected 511 keV gamma-ray signal, for counting purposes, units keV
	// gamSig: 1-sigma width of the 511 keV gamma-ray signal, for counting purposes, units keV
	// calibA: energy calibration for Channel A
	// calibB: energy calibration for Channel B
	if (nStep <= 0) return EXIT_SUCCESS;

	float stageMax = 235.;
	float endPosition = x0 + (nStep-1) * dx;
	if (endPosition > stageMax) {
		printf("linearScan: the stage motion will be out of range: %f > %f.\n", endPosition, stageMax);
		return EXIT_FAILURE;
	}

	// Move the stage all the way to the right, until the optical stop switch halts it at "home".
	int ret = stepperLeftRight("right", 700);
	if (ret != EXIT_SUCCESS) {
		printf("linearScan: failure trying to zero the stage.");
		return ret;
	}

	// Move the stage back to the right by a distance x0
	ret = stepperLeftRight("left", x0);
	if (ret != EXIT_SUCCESS) {
		printf("linearScan: failure moving the stage to the start location.");
		return ret;
	}

	FILE* fp;
	fp = fopen(fn, "w");
	if (fp == NULL) {
		printf("linearScan: failed to open the output file %s\n", fn);
		return EXIT_FAILURE;
	}

	// Iterate over nStep steps, pausing to take data in between the steps.
	float position = x0;
	int step = 1;
	fprintf(fp, "Scan results for angle %f\n", angle);
	fprintf(fp, " Angle, Step, Position, Count\n");
	while (true) {
		int count; 
		UARTmsg("STATUS: starting acquisition at step " + to_string(step) + " Position " + to_string(position) + "\n");
		int rc = acquireData("NULL", &count, "external", "chA", timePerStep, 1000000, false, 0, 1., 0., coincidenceWindow, sigThr, 
			                 pedA, pedB, gamMean, gamSig, calibA, calibB);
		if (rc != EXIT_SUCCESS) {
			printf("linearScan: failure in acquireData at step %d\n", step);
			fclose(fp);
			return rc;
		}
		printf("linearScan: angle %f, step %d, x=%f, count of coincident 511 keV gamma rays = %d\n", angle, step, position, count);
		printf("\n");
		fprintf(fp, " %f, %d, %f, %d\n", angle, step, position, count);
		UARTmsg("STATUS: " + to_string(count) + " 511 keV gamma-ray coincidences seen\n");
		if (step == nStep) break;
		rc = stepperLeftRight("left", dx);
		if (rc != EXIT_SUCCESS) {
			printf("linearScan: failure in stepperLeftRight at step %d\n", step);
			return rc;
		}
		position += dx;
		step++;
	}
	fclose(fp);
	return EXIT_SUCCESS;
}
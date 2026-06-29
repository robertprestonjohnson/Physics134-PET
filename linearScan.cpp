/* PET Red Pitaya routine to execute a stepped scan at each angle
*  in a set of equally spaced angles.
*  Author: Robert P. Johnson    January 16, 2026
*/

#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <math.h>
#include <chrono>
#include <ctime>
#include "rp.h"
#include "rp_hw.h"
#include "PET.h"

int PET::linearScan(float angle0, char const* fn, float x0, float dx, float dTheta, int nStep, int nAngles, float timePerStep, int coincidenceWindow, 
	float sigThr, float pedA, float pedB, float gamMean, float gamSig, float calibA, float calibB) {
	// angle0: the manually set initial scan angle from the rotary stage 
	// fn: file name for the output of the coincidence count per stage location
	// x0: initial position of the stage relative to the right-hand stop position
	// dx: step size for the stage from one data acquisition to the next
	// dTheta: angular step between linear scans
	// nStep: number of stage positions at which data acquisition is to occur
	// nAngles: number of angles at which to scan
	// timePerStep: live time for the data acquisition at each step
	// coincidenceWindow: maximum difference in start points of the two channels to define a coincidence
	// sigThr: threshold in noise sigmas to define the start of a gamma-ray pulse
	// pedA: pedestal value for channel A 
	// pedB: pedestal value for channel B 
	// gamMean: mean of the expected 511 keV gamma-ray signal, for counting purposes, units keV
	// gamSig: 1-sigma width of the 511 keV gamma-ray signal, for counting purposes, units keV
	// calibA: energy calibration for Channel A
	// calibB: energy calibration for Channel B

	if (nStep <= 0 && nAngles <= 0) {
		printf("linearScan: at least one step and one angle are necessary. nStep=%d  nAngles=%d\n", nStep, nAngles);
		UARTmsg("ERROR: at least one step and one angle are necessary in a scan.\n");
		return EXIT_FAILURE;
	}

	float stageMax = 230.0;  // The distance between stops is 230.3 mm
	float endPosition = x0 + (nStep-1) * dx;
	if (endPosition > stageMax) {
		printf("linearScan: the stage motion will be out of range: %f > %f.\n", endPosition, stageMax);
		UARTmsg("ERROR: the stage motion will be out of range by " + to_string(endPosition - stageMax) + " mm.\n");
		return EXIT_FAILURE;
	}

	// Move the stage all the way to the right, until the optical stop switch halts it at "home".
	UARTmsg("STATUS: moving the linear stage to its home position x=0.\n");
	int ret = stepperLeftRight("right", 700);
	if (ret != EXIT_SUCCESS) {
		printf("linearScan: failure trying to zero the stage.\n");
		UARTmsg("ERROR: failure trying to zero the stage.\n");
		return ret;
	}

	// Move the stage back to the right by a distance x0
	ret = stepperLeftRight("left", x0);
	if (ret != EXIT_SUCCESS) {
		printf("linearScan: failure moving the stage to the start location.\n");
		UARTmsg("ERROR: failure moving the stage to the start location.\n");
		return ret;
	}

	FILE* fp;
	fp = fopen(fn, "w");
	if (fp == NULL) {
		printf("linearScan: failed to open the output file %s\n", fn);
		string fns = fn;
		UARTmsg("ERROR: failure opening the output file " + fns + "\n");
		return EXIT_FAILURE;
	}
	
	time_t now = time(0); 
    tm* local_time = localtime(&now);
    char cbuff[100];
    strftime(cbuff, sizeof(cbuff), "%c", local_time); //    
	fprintf(fp, "PET scan results from %s\n", cbuff);
	string sdayTime(cbuff);

	// The SCANDATA messages send temp-file lines to the GUI for plotting counts versus distance for each linear scan
    // The ANALYSIS messages send lines to be written on the PC disk for the final analysis data to be input into image reconstruction
	UARTmsg("ANALYSIS: PET scan results from " + sdayTime + "\n");
	string header = "Day Time Year, Angle, ";
	for (int step=0; step<nStep; ++step) {
		sprintf(cbuff, "%.1f mm, ", x0 + step*dx);
		string tmps(cbuff);
		header = header + tmps;
	}
	header = header + "\n";
	UARTmsg("ANALYSIS: " + header);
	fprintf(fp, "%s", header.c_str());
	
    // Iterate over angles and acquire data
	float angle = angle0;
	for (int k=0; k<nAngles; ++k) {
		// Iterate over nStep steps, pausing to take data in between the steps.
		float position = x0;
		int step = 1;
		now = time(0); 
		local_time = localtime(&now);
		strftime(cbuff, sizeof(cbuff), "%c", local_time);
		sdayTime = cbuff;
		sprintf(cbuff, "%s, %.1f, ", sdayTime.c_str(), angle);
		string dataLine(cbuff);
		UARTmsg("SCANDATA: Scan results for angle " + to_string(angle) + "\n");
		UARTmsg("SCANDATA:  Angle, Step, Position, Count\n");
		while (true) {
			int count; 
			UARTmsg("STATUS: starting acquisition at step " + to_string(step) + " Position " + to_string(position) + "mm.\n");
			int rc = acquireData("NULL", &count, "external", "chA", timePerStep, 1000000, false, 0, 1., 0., coincidenceWindow, sigThr, 
								 pedA, pedB, gamMean, gamSig, calibA, calibB);
			if (rc != EXIT_SUCCESS) {
				printf("linearScan: failure in acquireData at step %d\n", step);
				UARTmsg("ERROR: failure in acquireData at step " + to_string(step) + "\n");
				fclose(fp);
				return rc;
			}
			if (UARTabort()) {
				UARTmsg("STATUS: aborting the scan.\n");
				return -2;
			}
			printf("linearScan: angle %.1f, step %d, x=%.1f, count of coincident 511 keV gamma rays = %d\n", angle, step, position, count);
			printf("\n");
			UARTmsg("SCANDATA: " + to_string(angle) + ", " + to_string(step) + ", " + to_string(position) + ", " + to_string(count) + "\n");
			UARTmsg("HISTOGRAM: sending the spectral histogram.\n");
			sendHistogram(0);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			sendHistogram(1);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			sprintf(cbuff, "%d, ", count);
			string tmps(cbuff);
			dataLine = dataLine + tmps;
			UARTmsg("STATUS: " + to_string(count) + " 511 keV gamma-ray coincidences seen\n");
			if (step == nStep) break;
			rc = stepperLeftRight("left", dx);
			if (rc != EXIT_SUCCESS) {
				printf("linearScan: failure in stepperLeftRight at step %d\n", step);
				UARTmsg("ERROR: failure in stepperLeftRight at step " + to_string(step) + "\n");
				return rc;
			}
			position += dx;
			step++;
		}
		dataLine = dataLine + "\n";
		UARTmsg("ANALYSIS: " + dataLine);
		if (verbose) printf("%s", dataLine.c_str());
		fprintf(fp, "%s", dataLine.c_str());
		fflush(fp);
		
		// Return back to the starting point:
		if (nStep > 1) {
			if (verbose) printf("Returning to the starting point.\n");
			int rc = stepperLeftRight("right", (nStep - 1) * dx);
			if (rc != EXIT_SUCCESS) {
				printf("linearScan: failure in stepperLeftRight returning to start position.\n");
				UARTmsg("ERROR: failure in stepperLeftRight returning to start position.\n");
				return rc;
			}
			position -= (nStep - 1) * dx;
		}
		// Send a message to the GUI that the linear scan plot is ready to display
		if (verbose) printf("Rotate to the %d'th angle. . .\n", k);
		UARTmsg("STATUS: ready to rotate to next angle.\n");
		if (k != nAngles - 1) {
			stepperRotate(dTheta);
			angle += dTheta;
		}
	}
	UARTmsg("STATUS: the scan is complete.\n");
	fclose(fp);
	return EXIT_SUCCESS;
}
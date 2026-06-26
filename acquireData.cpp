/* Red Pitaya C API for Acquiring a signal on external or internal trigger on a specific channel */

#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <math.h>
#include <chrono>

#include "PET.h"
#include "rp.h"
#include "rp_hw.h"

int PET::acquireData(char const* dataFile, int* count, char const* trgtyp, char const* trgCh, float runTime, int numIteration, bool newPeds, int maxWrite, float trglev, 
	float triggerHyst, int coincidenceWindow, float sigThr, float pedA, float pedB, float gamMean, float gamSig, float calibA, float calibB) {
	// dataFile:  file name for the list of pulse integrals that will be calculated from the digitizations and output
	// count: number of 511 keV gamma-ray coincidences detected in the data
	// trgtyp: trigger type = "external", "internal", "pedestal"     (for the "pedestal" case a special external trigger must be provided)
	// trgCh: specifies which channel to use for the internal trigger, "chA" or "chB"
	// runTime: live time in seconds over which to accumulate data. Set to 0 for no limit
	// numIteration: the maximum number of triggers to accept (set to a very large value if a runTime is specified)
	// newPeds: set to true to used pedestals calculated event by event from the digitizations preceding the trigger; false otherwise
	// maxWrite: maximum number of digitized pulses to write to a file "pulse.csv"
	// trglev: trigger level for the internal trigger, to capture a digitized signal
	// triggerHyst: trigger hysteresis for the internal trigger
	// coincidenceWindow: maximum difference in start points of the two channels to define a coincidence
	// sigThr: threshold in noise sigmas to define the start of a gamma-ray pulse
	// pedA: pedestal value for channel A (not used if newPeds is true)
	// pedB: pedestal value for channel B (not used if newPeds is true)
	// gamMean: mean of the expected 511 keV gamma-ray signal, for counting purposes, units keV
	// gamSig: 1-sigma width of the 511 keV gamma-ray signal, for counting purposes, units keV
    // calibA: energy calibration for Channel A
	// calibB: energy calibration for Channel B

	printf("The data acquisition will run for %d triggers or time %f.\n", numIteration, runTime);
	
	if (strcmp(trgtyp,"external") == 0 || strcmp(trgtyp,"internal") == 0 || strcmp(trgtyp,"pedestal") == 0) {
		printf("acquireData: the trigger type is set to %s \n", trgtyp);
	} else {
		printf("acquireData: invalid trigger type %s", trgtyp);
        return EXIT_FAILURE;
    }
    	
	if (strcmp(trgCh,"chA") == 0 || strcmp(trgCh,"chB") == 0) {
		if (strcmp(trgtyp, "internal") == 0) printf("acquireData: the trigger channel is set to %s\n", trgCh);
	} else {
		printf("acquireData: invalid trigger channel %s", trgCh);
        return EXIT_FAILURE;
    }
	
    /* Reset Acquisition */
    rp_AcqReset();

	/* Open the file into which pulse integrals will be written */
    FILE *fp;
	if (strcmp(trgtyp, "pedestal") == 0) {
		fp = fopen("pedestal.csv", "w");
		if (fp == NULL) {
			printf("acquireData: failed to open the output file.\n");
			return EXIT_FAILURE;
		}
	} else {
		if (strcmp(dataFile,"NULL") == 0) {     // For many occassions there is no need to write out the data---only the gamma-ray coincidence counts get recorded
			fp = NULL;
		}
		else {
			fp = fopen(dataFile, "w");
			if (fp == NULL) {
				printf("acquireData: failed to open the output file.\n");
				return EXIT_FAILURE;
			}
		}	
	}
		
    /* Allocate memory for the digitzed data */
    uint32_t buff_size = 16384;
    float *buff[2]; 
	if (verbose) printf("acquireData: allocate buffers\n");
	buff[0] = (float *)malloc(buff_size * sizeof(float));
	buff[1] = (float *)malloc(buff_size * sizeof(float));
	memset(buff[0], 0, buff_size * sizeof(float));
	memset(buff[1], 0, buff_size * sizeof(float));
	if (verbose) {
		printf("acquireData: buffers allocated and zeroed\n");
		printf("acquireData: the coincidence window is set to %d time samples.\n", coincidenceWindow);
		printf("acquireData: threshold to detect a signal is %f times the rms noise.\n", sigThr);
	}

	/* Initialize the histogram bins */
	for (int i = 0; i < acq.nBins; ++i) {
		histA[i] = 0;
		histB[i] = 0;
	}

	/* Set parameters for the data acquisition */
    rp_AcqSetDecimation(RP_DEC_1);             // Maximum digitizer speed is RP_DEC_1
	float triggerLevel[2] = {trglev,trglev};   // Set discriminator levels for the internal trigger	
    rp_AcqSetTriggerLevel(RP_T_CH_1, triggerLevel[0]); //Trig level is set in Volts while in SCPI
	rp_AcqSetTriggerLevel(RP_T_CH_2, triggerLevel[1]);
	rp_AcqSetTriggerHyst(triggerHyst);         // Set internal trigger hysteresis
    rp_AcqSetTriggerDelay(0);                  // Set internal trigger delay
	
	/* Verify the trigger level and hysteresis settings */
	float voltage = 0.;
	float hyst = 0.;
	rp_AcqGetTriggerLevel(RP_T_CH_1, &voltage);
	if (verbose) printf("acquireData: trigger voltage for channel 1 is set to %f\n",voltage);
	rp_AcqGetTriggerHyst(&hyst);
	if (verbose) printf("acquireData: trigger hysteresis is set to %f\n",hyst);

    rp_AcqSetGain(RP_CH_1, RP_LOW);            // By default low level gain is selected
    rp_AcqSetGain(RP_CH_2, RP_LOW); 
	if (verbose) printf("acquireData: the gain has been set to 'low' on both channels\n");

	/* Set the trigger source to external or internal channel A or internal channel B */
	/* The external trigger comes from the custom coincidence board                   */
    rp_acq_trig_src_t trgSrc = RP_TRIG_SRC_EXT_PE;
	if (strcmp(trgCh,"chA") == 0) {
		if (strcmp(trgtyp,"internal") == 0) trgSrc = RP_TRIG_SRC_CHA_PE;
	} else {
		if (strcmp(trgtyp,"internal") == 0) trgSrc = RP_TRIG_SRC_CHB_PE;
	} 
    rp_acq_trig_state_t state = RP_TRIG_STATE_WAITING;

	/* Open a file for writing out full pulses with all digitizations */
	FILE* fp2;
	if (maxWrite > 0) {
		fp2 = fopen("pulse.csv","w");
		if (fp2 == NULL) {
			printf("acquireData: failed to open the pulse output file. No pulse will be written.\n");
			maxWrite = 0;
		}
	}
	int nWrite = 0;    // number of events written out as digitizations
	
	// Set the point in the data acqusition buffer where the search for a pulse will begin (i.e. not long before the trigger point)
	uint32_t startSearch = (buff_size - 384) / 2;
	if (verbose) printf("acquireData: searches for the signal will start at time sample %d\n", startSearch);

	float nSigSignal = 2.0;     // plus or minus number of standard deviations to cut around the mean to define the 511 keV signal
	if (verbose) printf("acquireData: 511 keV gammas will be counted if their pulse integral falls between %f and %f\n", gamMean - nSigSignal * gamSig, gamMean + nSigSignal * gamSig);

	/* Begin the loop over triggers */
	int ret;
	int nPeds = 0;
	double sumPeds[2] = { 0.,0. };
	double sumRms[2] = { 0., 0. };
	double maxPulse = 0.;
	double sumPed[2] = {0.,0.};
	double sumPed2[2] = {0.,0.};
	int nPed[2] = {0,0};
	*count = 0;
	std::chrono::duration<double, std::milli> elapsed_time = std::chrono::milliseconds::zero();
	auto start_iter = std::chrono::high_resolution_clock::now();
    for (int iter=0; iter<numIteration; ++iter) {		
		if (verbose) {
			printf("************************\n");
		    printf("****** Trigger iteration %d\n",iter);
		    printf("************************\n");
		}
		rp_AcqStart();
		auto start = std::chrono::high_resolution_clock::now();
		ret = rp_AcqSetTriggerSrc(trgSrc);
		if (ret != RP_OK) {
			fprintf(stderr,"Error returned from rp_AcqSetTriggerSrc = %d: %s\n", ret, rp_HwGetError(ret));
			break;
		}
		
		rp_acq_trig_src_t source = RP_TRIG_SRC_EXT_PE;
		ret = rp_AcqGetTriggerSrc(&source);
		if (ret != RP_OK) {
			fprintf(stderr,"Error returned from rp_AcdGetTriggerSrc = %d: %s\n", ret, rp_HwGetError(ret));
			break;
		}
		if (verbose) printf("acquireData: the trigger source was set to %d\n",source);
		/*
		rp_AcqGetTriggerLevel(RP_T_CH_1, &voltage);
		printf("Trigger voltage for channel 1 is set to %f\n",voltage);	
		rp_AcqGetTriggerLevel(RP_T_CH_2, &voltage);
		printf("Trigger voltage for channel 2 is set to %f\n",voltage);	
		rp_AcqGetTriggerHyst(&hyst);
		printf("Trigger hysteresis is set to %f\n",hyst);
		*/
		// Give the DAQ buffer time to fill with digitizations. At decimation 1 this requires 131 microseconds.
		std::this_thread::sleep_for(std::chrono::microseconds(131));   
		int trial = 0;
		while(1){
			rp_AcqGetTriggerState(&state);
			if (trial%100 == 0) {
				auto end = std::chrono::high_resolution_clock::now();
				std::chrono::duration<double, std::milli> elapsed_ms = end - start;
				if (runTime > 0.) {
					if (elapsed_ms.count() >= runTime*1000.) {
						printf("Ending acquisition at iteration %d because of specified time limit.\n", iter);
						goto end_loops;
					}
				}
			}
			if(state == RP_TRIG_STATE_TRIGGERED){
				break;    // A trigger was detected
			}
			trial++;
		}
		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> elapsed_ms = end - start;
		if (verbose) printf("acquireData: trigger detected at trial %d, elapsed time= %f ms\n", trial, elapsed_ms.count());
		elapsed_time += elapsed_ms;

		bool fillState = false;
		trial = 0;
		while(!fillState){
			rp_AcqGetBufferFillState(&fillState);			
			trial++;
		}
		if (verbose) printf("acquireData: fillState good at trial %d = %d\n",trial,fillState);

		UARTmsg("STATUS: " + to_string(iter) + " triggers received thus far. Continuing...\n");

		// Fill the buffers with digitized data from the latest trigger
		uint32_t dataSize = buff_size;
		rp_AcqGetLatestDataV(RP_CH_1, &dataSize, buff[0]);   // Does this program modify the 2nd argument??
		rp_AcqGetLatestDataV(RP_CH_2, &dataSize, buff[1]);
		if (dataSize != buff_size) printf("acquireData: rp_AcqGetLattestDataV dataSize = %d, buff_size = %d\n", dataSize, buff_size);
		
		// Stop the acquisition while we process these data
		rp_AcqStop();
		
		// Loop over the digitizations to calculate pedestals, noise levels, and search for the gamma-ray pulses
		double noise[2] = {0.,0.};
		double noise2[2] = {0.,0.};
		double minSig[2] = {999.,999.};
		double sigN[2] = {0.000931, 0.001268};
		double pedestal[2] = { pedA, pedB }; 
		double integral[2] = { 0., 0. };
		int nNoise[2] = {0,0};
		bool found[2];
		int nSamp[2];
		double sampMean[2];
		double period = 8.01;                  // Digitizer period in ns
		double gain = (3.3/(16384/2))*1000.;   // lsb of 14-bit (signed) digitizer, in mV
		int npulseFnd[2] = {0,0};
		int t0[2] = { 0, 0 };
		double clip = 0.5;
		double pmax;
		double calibration[2] = { calibA, calibB };
		if (strcmp(trgtyp,"pedestal") == 0) clip = 0.1;
		for (uint32_t i=0; i < buff_size; ++i){
			if (buff[0][i] > maxPulse) maxPulse = buff[0][i];
			if (buff[1][i] > maxPulse) maxPulse = buff[1][i];

			// Calculate pedestals and noise levels from the digizations that preceed the signal pulse
			if (i < startSearch || strcmp(trgtyp,"pedestal") == 0){
				for (int j=0; j<2; ++j) {
					if (buff[j][i] < minSig[j]) minSig[j] = buff[j][i];
					if (buff[j][i] < clip && buff[j][i] > -clip){
						nNoise[j]++;
						noise[j] += buff[j][i];
						noise2[j] += buff[j][i]*buff[j][i];
					}
				}
			} else {       // Start searching for pulses just before the trigger point
				if (i == startSearch) {
					for (int j=0; j<2; ++j) {     // Loop over the two channels
						noise[j] = noise[j]/nNoise[j];
						double pedes = noise[j];
						sumPed[j] += pedes;
						sumPed2[j] += pedes*pedes;
						nPed[j]++;
						noise2[j] = noise2[j]/nNoise[j];
						double sigmaN = sqrt(noise2[j] - noise[j]*noise[j]);
						if (verbose) printf("acquireData: RMS noise level from channel %d = %f, pedestal = %f \n",j,sigmaN,pedes);
						if (newPeds) {
							pedestal[j] = pedes;
							sigN[j] = sigmaN;
						}
                        integral[j] = 0.0;
						nSamp[j] = 0;
						found[j] = false;
					}	
				    pmax = 0.;
				}
				for (int j=0; j<2; ++j) {
					if (!found[j] && buff[j][i] > pedestal[j]+sigThr*sigN[j]) {  // Detect the start of the pulse
						found[j] = true;
						if (verbose) if (verbose) printf("acquireData: found signal on channel %d at sample %d\n",j,i);
						nSamp[j] = 1;
						t0[j] = i;
						sampMean[j] = buff[j][i];
						integral[j] = buff[j][i] - pedestal[j];    
						integral[j] += buff[j][i-1] - pedestal[j];
						integral[j] += buff[j][i-2] - pedestal[j];
						integral[j] += buff[j][i-3] - pedestal[j];
					}
				}
				for (int j=0; j<2; ++j) {
					if (found[j]) {       // Add up the signal above pedestal in the pulse
						nSamp[j]++;
						sampMean[j] += buff[j][i];
						integral[j] += buff[j][i] - pedestal[j];
						if (buff[j][i] > pmax) pmax = buff[j][i];
					}
					if (found[j] && buff[j][i] < pedestal[j]+3.0*sigN[j]) {   // Detect the end of the pulse
						found[j] = false;
						npulseFnd[j]++;
						nSamp[j]--;
						sampMean[j] -= buff[j][i];
						if (i+3 < buff_size) {
						    integral[j] += buff[j][i+1] - pedestal[j];
							integral[j] += buff[j][i+2] - pedestal[j];
							integral[j] += buff[j][i+3] - pedestal[j];
						}
						sampMean[j] = sampMean[j]/nSamp[j];
						integral[j] *= period * gain * calibration[j];   // Converts to energy units
						if (verbose) {
						    printf("acquireData: signal on channel %d ended at sample %d, length = %d = %f ns\n",j,i,nSamp[j],period * nSamp[j]);
						    printf("acquireData: mean of signal on channel %d was %f\n",j,sampMean[j]);
						    printf("acquireData: maximum of signal on both channels was %f\n",pmax);
						    printf("acquireData: integral of signal on channel %d was %f mV ns\n",j,integral[j]);
						}
					}
				}
			}
			// For the external trigger, exit the loop over digitizations early only if a pulse was found in both channels.
			// For the internal trigger, exit the loop early as soon as a single pulse is found.
			if (strcmp(trgtyp,"external") == 0) {
				if (npulseFnd[0]>0 && npulseFnd[1]>0) break;
			} else {
				if (strcmp(trgtyp,"internal") == 0) {
					if (npulseFnd[0]>0 || npulseFnd[1]>0) break;
				} 
			}
		} 
		// Write out the pulse integral results
		if (strcmp(trgtyp,"external") == 0) {
			if (abs(t0[0]-t0[1]) < coincidenceWindow) {
				if (verbose) {
					printf("acquireData: coincidence found with t0 = %d and %d\n", t0[0], t0[1]);
					printf("acquireData: integrals = %f and %f keV\n", integral[0], integral[1]);
				}
				if (fp != NULL) fprintf(fp,"%f, %f\n",integral[0],integral[1]);
				histo(integral[0], 0);
				histo(integral[1], 1);
				int nCnt = 0;
				for (int j = 0; j < 2; ++j) {
					if (integral[j] > gamMean - nSigSignal * gamSig && integral[j] < gamMean + nSigSignal * gamSig) {
						nCnt++;
					}
				}
				if (nCnt == 2) *count = *count + 1;    // Counting 511 keV coincidences
			} 
		} else {
			if (strcmp(trgtyp,"internal") == 0) {
				if (verbose) {
					printf("acquireData: trigger with t0 = %d and %d\n", t0[0], t0[1]);
					printf("acquireData: integrals = %f and %f keV\n", integral[0], integral[1]);
				}
				if (fp != NULL) fprintf(fp,"%f, %f\n",integral[0],integral[1]);
				histo(integral[0], 0);
				histo(integral[1], 1);
			} else {
				nPeds = nPeds + 1;
				for (int j=0; j<2; ++j) {
					noise[j] = noise[j]/nNoise[j];
					pedestal[j] = noise[j];
					noise2[j] = noise2[j]/nNoise[j];
					sigN[j] = sqrt(noise2[j] - noise[j]*noise[j]);
					if (verbose) {
						printf("acquireData: RMS noise level from channel %d = %f, pedestal = %f \n", j, sigN[j], pedestal[j]);
						printf("acquireData: minimum signal is %f.\n", minSig[j]);
					}
					sumRms[j] = sumRms[j] + sigN[j];
					sumPeds[j] = sumPeds[j] + pedestal[j];
				}
				if (fp != NULL) fprintf(fp,"%f, %f, %f, %f, %f, %f\n",-minSig[0],-minSig[1],sigN[0],sigN[1],pedestal[0],pedestal[1]);
				histo(integral[0], 0);
				histo(integral[1], 1);
			}
		}
		// Write out the full list of digitizations if requested
		if (nWrite < maxWrite) {
			if (integral[0]>10. && integral[0]<2000.) {
				if (integral[1]>-9999.) {
					nWrite++;
					fprintf(fp2,"Ch1=%f Ch2=%f\n",integral[0],integral[1]);
					for(uint32_t i = 0; i < buff_size; ++i){
						fprintf(fp2,"%d , %f , %f\n",i,buff[0][i],buff[1][i]);
					}
				}
			}
		}

		if (runTime > 0.) {
			if (elapsed_time.count() >= runTime*1000.) {
				if (verbose) printf("Ending acquisition at iteration %d because of specified time limit.\n", iter);
				break;
			}
		}
	}
	end_loops: 
	auto end_iter = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::milli> elapsed_total = end_iter - start_iter;
	printf("Total time for the iterations was %f ms. The total live time was %f ms\n", elapsed_total.count(), elapsed_time.count());
    double liveTime = 100.0*(elapsed_time.count()/elapsed_total.count());
	printf("The live time fraction was %f percent.\n", liveTime);
	if (strcmp(trgtyp,"pedestal") == 0) {
		printf("acquireData: after %d trials, the average pedestal results are\n", nPeds);
		for (int j=0; j<2; ++j) {
			printf("acquireData: channel %d, RMS = %f, pedestal = %f\n",j,sumRms[j]/nPeds,sumPeds[j]/nPeds);
	
		}
	}

    printf("acquireData: maximum pulse height encountered = %f\n",maxPulse);
	if (nPed[0] > 0 && nPed[1] > 0) {
		for (int j=0; j<2; ++j) {
			sumPed[j] = sumPed[j]/nPed[j];
			sumPed2[j] = sumPed2[j]/nPed[j];
			double sigma = sqrt(sumPed2[j] - sumPed[j]*sumPed[j]);
			printf("acquireData: the mean of %d pedestal measurements for channel %d is %f. The rms = %f.\n",nPed[j],j,sumPed[j],sigma);
		}
	}
	
	/* Write the histograms to a file */
	FILE* fph = fopen("histogram.csv", "w");
	if (fph != NULL) {
		fprintf(fph, "Pulse height histograms:\n");
		fprintf(fph, "Bin, Channel-A, Channel-B\n");
		for (int i = 0; i < acq.nBins; ++i) {
			fprintf(fph, "%d, %d, %d\n", i, histA[i], histB[i]);
		}
		fclose(fph);
	}

    /* Releasing resources */
	if (fp != NULL) fclose(fp);
	if (maxWrite > 0 && fp2 != NULL) fclose(fp2);
    free(buff[0]);
	free(buff[1]);
	if (verbose) printf("exiting acquireData\n");
    return EXIT_SUCCESS;
}
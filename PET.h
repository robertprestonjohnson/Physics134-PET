#ifndef PET_HH
#define PET_HH 1
//
// Top level class for the Physics 134 PET experiment at UCSC. This C++ code runs
// on the Red Pitaya. It executes as a command-line driven code under the main
// program in PET_RP.cpp or as constantly running code in PET_RP_Slave that receives 
// commands from the PET Python GUI via a UART connection to the PC.
//
// Author: Robert P. Johnson,  January 2026
//
// V 2.0: modified to work with the new Generation-2 Red Pitaya.
// V 2.2: abort scan if the stage gets a stop signal when not expected. Also, check the stop signal twice, to help avoid noise
//
#include "arg.h"
#include "rp.h"
#include "rp_hw.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string>
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <cstring>

//#define VERBOSE   // define this macro to get lots of debug printout

using namespace std;

class PET
{
private:
	const double Version = 2.2;   // Version of this code, 11/9/2026
	string version;
    arg::Parser* parser;          // Pointer to an instance of the arg command line parser

    std::string action;           // The action specified by the command line
	
	float maxTemp = 70.0;         // Maximum allowed CPU temperature to continue execution of a scan

    int defaultNbins = 100;
	float stageTravelMax = 230.3; // Maximum stage travel distance
    int UARTbufSize;
    char *uart_buf;
	
	FILE *flog;

    // Command parameters
    struct scan_parms {
        float angle;
        int nStep;
		int nAngles;
        float X0;
        float stepSize;
        float dwellTime;
		float angleStep;
        string fileName;
    } scn;

    struct acquire_parms {
        int num_triggers;
        float runTime;
        string trgtype;
        string fileName;
        string calcNewPeds;
        int maxWrite;
        int maxScope;
        float trglev;
        string trgchan;
        float trghyst;
        int coincWin;
        float sigThr;
        float pedA;
        float pedB;
        float gamMean;
        float gamSig;
        float calibA;
        float calibB;
        float gamMax;
        int nBins;
		string histType;
    } acq;

    struct time_parms {
        int timingA;
        int timingB;
    } tim;

    struct dac_parms {
        string thrCh;
        string HVch;
        int DAC;
        int voltage;
        string writeEE;
    } dac;

    struct stage_parms {
        string direction;
        float distance;
		float angle;
		string sense;
    } stg;

    bool uartIsActive = false;

    uint32_t* histA;
    uint32_t* histB;

public:
	char logmsg[256];
	
    string getTemp() {
		int rc = system("/opt/redpitaya/bin/monitor -ams | /bin/grep \"Temp\" > /tmp/temperature.txt");
		if (rc != 0) printf("getTemp: Error from monitor + grep commands = %d\n", rc);
		ifstream infile("/tmp/temperature.txt");
		string temp = "-99.";
		if (infile.is_open()) {
			string line;
			getline(infile, line);
			istringstream iss(line);
			while (iss >> temp) {
				#ifdef VERBOSE
				    cout << temp << endl;
				#endif
			}
			#ifdef VERBOSE
			    cout << "getTemp: the temperature is " << temp << " degrees C" << endl;
			#endif
		} else {
			cerr << "Failed to open /tmp/temperature.txt! Reason: " << strerror(errno) << endl;
		}
		return temp;
	}
    bool isUartActive() const {
        return uartIsActive;
    }
    void setUartActive(bool F) {
        uartIsActive = F;
    }
	void logger(const char* s) {
		printf("%s", s);
		if (flog != NULL) fprintf(flog, "%s", s); else printf("logger: null file pointer\n");
	}

    uint32_t* getHistogram(int det, int* nBins) {
        *nBins = acq.nBins;
        #ifdef VERBOSE
            printf("Getting histogram for detector %d with %d bins.\n", det, acq.nBins);
        #endif
        if (det == 0) return histA;
        else if (det == 1) return histB;
        else return NULL;
    }
	void sendHistogram(int det) {
		if (uartIsActive) {
			int nHistBins;
			uint32_t* histoGram = getHistogram(det, &nHistBins);
            #ifdef VERBOSE
			    printf("Number of bytes per histogram bin = %d.\n", sizeof(histoGram[0]));
            #endif
			memcpy(uart_buf, histoGram, nHistBins * sizeof(histoGram[0]));
			uart_buf[nHistBins * sizeof(histoGram[0])] = '\n';  // This \n is pointless, as readline won't work, since \n=\x0A
			int res = rp_UartWrite((unsigned char*)uart_buf, nHistBins * sizeof(histoGram[0]) + 1);
			if (res != EXIT_SUCCESS) {
				printf("sendHistogram: error writing histogram to UART: %s\n", rp_HwGetError(res));
			}
			#ifdef VERBOSE
			    printf("sendHistogram: sent histogram for detector %d with %d bytes.\n", det, nHistBins * sizeof(histoGram[0]) + 1); 
            #endif				
		}
	}
    void sendPulse(int det, int nPoints, float* buff) {
        if (uartIsActive) {
            memcpy(uart_buf, buff, nPoints * sizeof(buff[0]));
            int res = rp_UartWrite((unsigned char*)uart_buf, nPoints * sizeof(buff[0]));
            if (res != EXIT_SUCCESS) {
                printf("sendPulse: error writing pulse to UART: %s\n", rp_HwGetError(res));
            }
			#ifdef VERBOSE
                printf("sendPulse: sent pulse for detector %d with %d bytes.\n", det, nPoints * sizeof(buff[0]));
		    #endif
        }
    }
    /* The PET class constructor */
	PET(int inputInt) {
        UARTbufSize = inputInt;
		uart_buf = (char *)malloc(UARTbufSize * sizeof(char));
		memset(uart_buf, 0, UARTbufSize * sizeof(char));
		
		version = to_string(Version);
		cout << "Creating an instance of PET, version " << version << endl;

        parser = new arg::Parser;  // Create a class instance for parsing the command line (see the program arg.cc and header arg.h)
        parser->set_header(" \n ******** PET Red Pitaya code version " + version + " **********");
        parser->add_help("");
        parser->add_help(" There is one positional argument: action");
        parser->add_help(" action = 'setTHR' 'setHV' 'setTiming' 'moveStage' 'rotateStage' 'acquireData' 'scan' 'getTemp' ");
        parser->add_help("   The command line option syntax can take either a short or long form, e.g.:");
        parser->add_help("          -n 3000000");
        parser->add_help("               or equivalently");
        parser->add_help("          --number=3000000");
        parser->add_help("   Use the string NULL to represent a null or empty string.");
        parser->add_help(" Available options are:");

        // Define all of the command line options
        scn.angle = 0.;
        parser->add_opt('G', "angle0").stow(scn.angle)
            .help("input the initial stage angle for the upcoming scan", "FLOAT")
            .show_default();

        scn.nStep = 1;
        parser->add_opt('N', "nSteps").stow(scn.nStep)
            .help("set the number of spatial steps in a linear scan", "INT")
            .show_default();

        scn.X0 = 5.;
        parser->add_opt('0', "x0").stow(scn.X0)
            .help("set the initial position for a linear scan", "FLOAT")
            .show_default();

        scn.stepSize = 5.;
        parser->add_opt('X', "stepSize").stow(scn.stepSize)
            .help("set the step size for a linear scan, in mm", "FLOAT")
            .show_default();

        scn.dwellTime = 600.;
        parser->add_opt('W', "dwellTime").stow(scn.dwellTime)
            .help("set the live time at each step of a linear scan, in seconds", "FLOAT")
            .show_default();

        scn.fileName = "/tmp/scanData.csv";
        parser->add_opt('F', "fileName").stow(scn.fileName)
            .help("file name for results of a linear scan", "STRING")
            .show_default();

        scn.nAngles = 3;
        parser->add_opt('L', "numAngles").stow(scn.nAngles)
            .help("number of angle steps in scan", "INT")
            .show_default();

		scn.angleStep = 5.0;
		parser->add_opt('s', "angleStep").stow(scn.angleStep)
		    .help("angular step in scan", "FLOAT")
			.show_default();

        acq.num_triggers = 1000;
        parser->add_opt('n', "number").stow(acq.num_triggers)
            .help("set the maximum number of triggers to acquire", "INT")
            .show_default();

        acq.runTime = 0.;
        parser->add_opt('T', "Time").stow(acq.runTime)
            .help("set the maximum run time in seconds (0=forever)", "FLOAT")
            .show_default();

        acq.trgtype = "external";
        parser->add_opt('t', "trgType").stow(acq.trgtype)
            .help("trigger type: 'external' 'internal' 'pedestal'", "STRING")
            .show_default();

        acq.histType = "integral";
		parser->add_opt('Q', "histType").stow(acq.histType)
		    .help("histogram type: 'integral' vs 'peak'", "STRING")
			.show_default();

        acq.fileName = "/tmp/coincidences.csv";
        parser->add_opt('f', "dataFile").stow(acq.fileName)
            .help("file name for pulse integrals from channels A and B; 'NULL' for none.", "STRING")
            .show_default();

        acq.trgchan = "chA";
        parser->add_opt('c', "trigChan").stow(acq.trgchan)
            .help("internal trigger channel: 'chA' or 'chB'", "STRING")
            .show_default();

        acq.calcNewPeds = "no";
        parser->add_opt('p', "newPeds").stow(acq.calcNewPeds)
            .help("calculate new pedestals per event, 'yes' or 'no'", "STRING")
            .show_default();

        acq.maxWrite = 0;
        parser->add_opt('z', "mxwrite").stow(acq.maxWrite)
            .help("set the maximum number of digitized pulses to write to file", "INT")
            .show_default();

        acq.maxScope = 0;
        parser->add_opt('y', "Nscope").stow(acq.maxScope)
            .help("number of triggers to display on oscilloscope", "INT")
            .show_default();

        acq.trglev = 0.4;
        parser->add_opt('e', "trglev").stow(acq.trglev)
            .help("set the internal trigger level", "FLOAT")
            .show_default();

        acq.trghyst = 0.;
        parser->add_opt('H', "trghyst").stow(acq.trghyst)
            .help("set the internal trigger hysteresis", "FLOAT")
            .show_default();

        acq.coincWin = 1;
        parser->add_opt('i', "coincWindow").stow(acq.coincWin)
            .help("set the size of the software coincidence window", "INT")
            .show_default();

        acq.sigThr = 10.0;
        parser->add_opt('r', "pulseThresh").stow(acq.sigThr)
            .help("set the threshold to detect a signal as a multiple of the rms noise.")
            .show_default();

        acq.pedA = 0.038738;
        acq.pedB = 0.042397;
        parser->add_opt('A', "pedestalA").stow(acq.pedA)
            .help("set the fixed pedestal for channel A", "FLOAT")
            .show_default();
        parser->add_opt('B', "pedestalB").stow(acq.pedB)
            .help("set the fixed pedestal for channel B", "FLOAT")
            .show_default();

        acq.gamMean = 511.0;
        acq.gamSig = (0.05) * 511.0;
        parser->add_opt('M', "Mean").stow(acq.gamMean)
            .help("mean location of the 511 keV gamma peak (keV)", "FLOAT")
            .show_default();
        parser->add_opt('S', "Sigma").stow(acq.gamSig)
            .help("rms width of the 511 keV gamma peak (keV)", "FLOAT")
            .show_default();

        acq.calibA = 10.;
        acq.calibB = 10.;
        parser->add_opt('Y', "calibA").stow(acq.calibA)
            .help("energy calibration for channel A", "FLOAT")
            .show_default();
        parser->add_opt('Z', "calibB").stow(acq.calibB)
            .help("energy calibration for channel B", "FLOAT")
            .show_default();

        acq.gamMax = 150.;
        acq.nBins = defaultNbins;
        parser->add_opt('m', "gamMax").stow(acq.gamMax)
            .help("Maximum gamma-ray energy for the histogram","FLOAT")
            .show_default();
        parser->add_opt('U', "numBins").stow(acq.nBins)
            .help("Number of bins for the histogram", "INT")
            .show_default();

        tim.timingA = 36;
        parser->add_opt('a', "timingA").stow(tim.timingA)
            .help("width of pulse on channel A for coincidence timing", "INT")
            .show_default();

        tim.timingB = 36;
        parser->add_opt('b', "timingB").stow(tim.timingB)
            .help("width of pulse on channel B for coincidence timing", "INT")
            .show_default();

        dac.thrCh = "chA";
        parser->add_opt('D', "thrDAC").stow(dac.thrCh)
            .help("Threshold DAC channel: 'chA' or 'chB'", "STRING")
            .show_default();

        dac.HVch = "chA";
        parser->add_opt('C', "HVDAC").stow(dac.HVch)
            .help("High Voltage DAC channel: 'chA' or 'chB'", "STRING")
            .show_default();

        dac.voltage = 20;
        parser->add_opt('v', "mvolts").stow(dac.voltage)
            .help("DAC voltage setting, in millivolts", "INT")
            .show_default();

        dac.writeEE = "no";
        parser->add_opt('w', "write").stow(dac.writeEE)
            .help("write DAC setting to the EEPROM, yes or no", "STRING")
            .show_default();

        stg.direction = "left";
        parser->add_opt('d', "direction").stow(stg.direction)
            .help("direction of stage motion: 'right' or 'left'", "STRING")
            .show_default();

        stg.distance = 5;
        parser->add_opt('x', "distance").stow(stg.distance)
            .help("distance of stage translation", "FLOAT")
            .show_default();
			
		stg.angle = 5;
		parser->add_opt('g', "angleRot").stow(stg.angle)
		    .help("angle of stage rotation", "FLOAT")
			.show_default();
			
		stg.sense = "forward";
		parser->add_opt('o', "sense").stow(stg.sense)
			.help("sense of stage rotation: 'forward' or 'backward'", "STRING")
			.show_default();

        parser->add_opt_help();
        parser->add_opt_version(version);

        #ifdef VERBOSE
            printf("PET_RP is running with debug printout enabled.\n");
		#endif

        // Open a file to log normal printout
		flog = fopen("/tmp/RP_PET_log.txt", "w");
		if (flog == NULL) printf("PET constructor failed to open the log file\n");

        // Allocate memory for the histograms
        if (acq.nBins > UARTbufSize / sizeof(int) - 1) {
            acq.nBins = UARTbufSize / sizeof(int) - 1;
            snprintf(logmsg, sizeof(logmsg), "PET: the number of histograms bins is reduced to %d, in order to fit in the UART buffer.\n", acq.nBins);
			logger(logmsg);
        }
        histA = new uint32_t[acq.nBins];
        histB = new uint32_t[acq.nBins];
        for (int i = 0; i < acq.nBins; ++i) {
            histA[i] = 0;
            histB[i] = 0;
        }

        // Initialization of API
        int ret = rp_Init();
        if (ret != RP_OK) {
            fprintf(stderr, "Red Pitaya API init failed with code %d!\n", ret);
        }
	}

    /* The PET class destructor */
	~PET() {
		free(uart_buf);
        delete parser;
        delete[] histA;
        delete[] histB;
		fclose(flog);
        rp_Release();
	}

    /* The rest of the class methods follow here */
    int parseCMD(int argc, char* argv[]);
    int linearScan(float angle0, char const* fn, float x0, float dx, float dTheta, int nStep, int nAngles, float timePerStep, int coincidenceWindow,
        float sigThr, float pedA, float pedB, float gamMean, float gamSig, float calibA, float calibB);
    int setTiming(int chanA, int chanB);
    int dacLoad(int DAC, bool writeEEPROM, int volt);
    int acquireData(char const* dataFile, int* count, char const* trgtyp, char const* trgCh, float runTime, int numIteration, bool newPeds, int maxWrite, float trglev, 
	                float triggerHyst, int coincidenceWindow, float sigThr, float pedA, float pedB, float gamMean, float gamSig, float calibA, float calibB);
    int stepperLeftRight(char const* direction, float distance);
	int stepperRotate (float angle, char const* sense);

private:
    int histo(float pulseSize, int det);
    void UARTmsg(string msg) const {
        if (uartIsActive) {
            int res = rp_UartWrite((unsigned char*)msg.c_str(), strlen(msg.c_str()));
            if (res != EXIT_SUCCESS) {
                printf("Error writing '%s' to UART, error = %s.\n", msg.c_str(), rp_HwGetError(res));
            }
        }
    }
	bool UARTabort() const {
		if (uartIsActive) {
			string msg = "ABORT?\n";
			int res = rp_UartWrite((unsigned char*)msg.c_str(), strlen(msg.c_str()));
            if (res != EXIT_SUCCESS) {
                printf("Error writing '%s' to UART, error = %s.\n", msg.c_str(), rp_HwGetError(res));
				return false;
            }
			int rdSize = UARTbufSize;
			res = rp_UartRead((unsigned char*)uart_buf, &rdSize);  
			if (res != EXIT_SUCCESS) {
				#ifdef VERBOSE
				    printf("UARTabort: error return from rp_UartRead = %d, %s\n", res, rp_HwGetError(res));
				#endif
				return false;
			}
			string reply(uart_buf, rdSize);
			if (reply == "YES") {
				printf("UARTabort: YES reply received. Will abort current process.\n");
				return true;
			}
		}
		return false;
	}
};
#endif // PET_HH

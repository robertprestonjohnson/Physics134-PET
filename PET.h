#ifndef PET_HH
#define PET_HH 1
#include "arg.h"
#include "rp.h"

#include <iostream>
#include <stdio.h>
#include <string>
#include <cmath>

using namespace std;

class PET
{
private:
	const double Version = 1.0;   // Version of this code
	string version;
    arg::Parser* parser;          // Pointer to an instance of the arg command line parser

    std::string action;           // The action specified by the command line

    // Command parameters
    struct scan_parms {
        float angle;
        int nStep;
        float X0;
        float stepSize;
        float dwellTime;
        string fileName;
    } scn;

    struct acquire_parms {
        int num_triggers;
        float runTime;
        string trgtype;
        string fileName;
        string calcNewPeds;
        int maxWrite;
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
    } acq;

    struct time_parms {
        int timingA;
        int timingB;
    } tim;

    struct dac_parms {
        int DAC;
        int voltage;
        string writeEE;
    } dac;

    struct state_parms {
        string direction;
        float distance;
    } stg;

    bool uartIsActive = false;
    bool verbose = true;

    uint32_t* histA;
    uint32_t* histB;

public:
    bool isUartActive() const {
        return uartIsActive;
    }
    void setUartActive(bool F) {
        uartIsActive = F;
    }
    void setVerbose(bool F) {
        verbose = F;
    }
    uint32_t* getHistogram(int det, int* nBins) {
        *nBins = acq.nBins;
        if (det == 0) return histA;
        else if (det == 1) return histB;
        else return NULL;
    }
    
    /* The PET class constructor */
	PET(int UARTbufSize) {
		version = to_string(Version);
		cout << "Creating an instance of PET, version " << version << endl;

        parser = new arg::Parser;  // Create a class instance for parsing the command line (see the program arg.cc and header arg.h)
        parser->set_header(" \n ******** PET Red Pitaya code version " + version + " **********");
        parser->add_help("");
        parser->add_help(" There is one positional argument: action");
        parser->add_help(" action = 'setDAC' 'setTiming' 'moveStage' 'acquireData' 'scan' ");
        parser->add_help("   The command line option syntax can take either a short or long form, e.g.:");
        parser->add_help("          -n 3000000");
        parser->add_help("               or equivalently");
        parser->add_help("          --number=3000000");
        parser->add_help("   Use the string NULL to represent a null or empty string.");
        parser->add_help(" Available options are:");

        // Define all of the command line options
        scn.angle = 0.;
        parser->add_opt('G', "angle").stow(scn.angle)
            .help("input the stage angle for the upcoming scan", "FLOAT")
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

        scn.fileName = "scanData.csv";
        parser->add_opt('F', "fileName").stow(scn.fileName)
            .help("file name for results of a linear scan", "STRING")
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

        acq.fileName = "coincidences.csv";
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

        acq.trglev = 0.2;
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

        acq.pedA = 0.038980;
        acq.pedB = 0.042450;
        parser->add_opt('A', "pedestalA").stow(acq.pedA)
            .help("set the fixed pedestal for channel A", "FLOAT")
            .show_default();
        parser->add_opt("B", "pedestalB").stow(acq.pedB)
            .help("set the fixed pedestal for channel B", "FLOAT")
            .show_default();

        acq.gamMean = 511.0;
        acq.gamSig = (2.140 / 45.596) * 511.0;
        parser->add_opt('M', "Mean").stow(acq.gamMean)
            .help("mean location of the 511 keV gamma peak (keV)", "FLOAT")
            .show_default();
        parser->add_opt('S', "Sigma").stow(acq.gamSig)
            .help("rms width of the 511 keV gamma peak (keV)", "FLOAT")
            .show_default();

        acq.calibA = 511. / 45.596;
        acq.calibB = 511. / 45.596;
        parser->add_opt('Y', "calibA").stow(acq.calibA)
            .help("energy calibration for channel A", "FLOAT")
            .show_default();
        parser->add_opt('Z', "calibB").stow(acq.calibA)
            .help("energy calibration for channel B", "FLOAT")
            .show_default();

        acq.gamMax = 150.;
        acq.nBins = 100;
        parser->add_opt('m', "gamMax").stow(acq.gamMax)
            .help("Maximum gamma-ray energy for the histogram","FLOAT")
            .show_default();
        parser->add_opt('B', "numBins").stow(acq.nBins)
            .help("Number of bins for the histogram", "INT")
            .show_default();

        tim.timingA = 47;
        parser->add_opt('a', "timingA").stow(tim.timingA)
            .help("width of pulse on channel A for coincidence timing", "INT")
            .show_default();

        tim.timingB = 47;
        parser->add_opt('b', "timingB").stow(tim.timingB)
            .help("width of pulse on channel B for coincidence timing", "INT")
            .show_default();

        dac.DAC = 1;
        parser->add_opt('D', "DAC").stow(dac.DAC)
            .help("DAC number: 1=threashold-A 2=threshold-B 3=HV-A 4=HV-B", "INT")
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

        parser->add_opt_help();
        parser->add_opt_version(version);

        // Allocate memory for the histograms
        if (acq.nBins > UARTbufSize / sizeof(int) - 1) {
            acq.nBins = UARTbufSize / sizeof(int) - 1;
            printf("PET: the number of histograms bins is reduced to %d, in order to fit in the UART buffer.\n", acq.nBins);
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
        delete parser;
        delete[] histA;
        delete[] histB;
        rp_Release();
	}

    /* The rest of the class methods follow here */
    int parseCMD(int argc, char* argv[]);
    int linearScan(float angle, char const* fn, float x0, float dx, int nStep, float timePerStep, int coincidenceWindow,
        float sigThr, float pedA, float pedB, float gamMean, float gamSig, float calibA, float calibB);
    int setTiming(int chanA, int chanB);
    int dacLoad(int DAC, bool writeEEPROM, int volt);
    int acquireData(char const* dataFile, int* count, char const* trgtyp, char const* trgCh, float runTime, int numIteration, bool newPeds, int maxWrite, float trglev, float triggerHyst,
        int coincidenceWindow, float sigThr, float pedA, float pedB, float gamMean, float gamSig, float calibA, float calibB);
    int stepperLeftRight(char const* direction, float distance);

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
};
#endif // PET_HH

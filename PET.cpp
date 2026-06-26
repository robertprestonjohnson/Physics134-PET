#include "PET.h"

int PET::parseCMD(int argc, char* argv[]) {    // Parse the command options from GUI or from command line    
    try {
        parser->parse(argc, argv);
    }
    catch (arg::Error e) {
        printf(" Error parsing command line: %s\n", e.get_msg().c_str());
        return EXIT_FAILURE;
    }

    // Get the list of required, position-sensitive arguments, in this case just the action
    std::vector<std::string> requiredArgs = parser->args();

    if (requiredArgs.size() == 0) {
        printf("PET_RP: no action was specified!\n");
        return EXIT_FAILURE;
    }
    else {
        action = requiredArgs[0];
    }

    // Now, execute the requestions action
    if (action == "setTiming") {
        int rc = setTiming(tim.timingA, tim.timingB);

        return rc;
    }
    if (action == "setDAC") {
        bool writeEEPROM = (dac.writeEE == "yes");
        int rc = dacLoad(dac.DAC, writeEEPROM, dac.voltage);
        return rc;
    }
    if (action == "moveStage") {
        char* stgd = new char(stg.direction.length() + 1);
        strcpy(stgd, stg.direction.c_str());
        int rc = stepperLeftRight(stgd, stg.distance);
        delete stgd;
        return rc;
    }
    bool newPeds = acq.calcNewPeds == "yes";
    char* trgt = new char[acq.trgtype.length() + 1];
    memcpy(trgt, acq.trgtype.c_str(), acq.trgtype.length() + 1);
    char* trgc = new char[acq.trgchan.length() + 1];
    memcpy(trgc, acq.trgchan.c_str(), acq.trgchan.length() + 1);
	int rc = 0;	
    if (action == "acquireData") {
        int count;
        rc = acquireData(acq.fileName.c_str(), &count, trgt, trgc, acq.runTime, acq.num_triggers, newPeds, acq.maxWrite,
                         acq.coincWin, acq.sigThr, acq.pedA, acq.pedB, acq.gamMean, acq.gamSig, acq.calibA, acq.calibB);
        printf("Counts of coincident 511 keV gamma rays = %d.\n", count);
        delete[] trgt;
        delete[] trgc;
        return rc;
    }
    if (action == "scan") {
        rc = linearScan(scn.angle, scn.fileName.c_str(), scn.X0, scn.stepSize, scn.nStep, scn.dwellTime,
                        acq.trglev, acq.trghyst, acq.coincWin, acq.sigThr, acq.pedA, acq.pedB, acq.gamMean, acq.gamSig, acq.calibA, acq.calibB);
        delete[] trgt;
        delete[] trgc;
        return rc;
    }

    printf(" PET_RP: action %s is not understood.\n", action.c_str());
    return EXIT_FAILURE;
}

// A simple function for accumulating histograms of the pulse heights
int PET::histo(float pulseSize, int det) {
    if (det != 0 && det != 1) {
        printf("PET::histo: bad detector selection %d\n", det);
        return EXIT_FAILURE;
    }
    int bin = floor((pulseSize / acq.gamMax) * (float)acq.nBins);
    if (bin < 0) bin = 0;
    if (bin >= acq.nBins) bin = acq.nBins - 1;  // Put over and underflows into the edge bins
    if (det == 0) histA[bin]++;
    if (det == 1) histB[bin]++;
    return EXIT_SUCCESS;
}

/* Include here the code for all of the other PET class methods */
#include "dacLoad.cpp"
#include "acquireData.cpp"
#include "stepperLeftRight.cpp"
#include "setTiming.cpp"
#include "linearScan.cpp"
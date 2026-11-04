// Load the voltage setting into a DAC on the PET coincidence board.
// The DAC settings are all in millivolts
// The actual thresholds will no equal the threshold DAC settings due to built-in hysteresis, with the rising threshold being higher
// than the falling threshold.
// The high voltage values, in Volts, equal the mV DAC settings times 0.40, so a DAC setting of 2500 will deliver the maximum high
// voltage of 1000 Volts.
// 

#include "PET.h"
#include "rp.h"
#include "rp_hw.h"
#include "rp_hw_calib.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>

// DAC I2C addresses
#define U3DAC_ADDR  0b1100111
#define U4DAC_ADDR  0b1100110
#define U19DAC_ADDR 0b1100001
#define U20DAC_ADDR 0b1100000

int PET::dacLoad(int DAC, bool writeEEPROM, int volt) {
    // DAC:  1= Channel A threshold    2= Channel B threshold     3= Channel A HV    4 = Channel B HV
    // writeEEPROM:  set non-zero to write the provided DAC value into the non-volatile EEPROM memory
    // volt: DAC setting in millivolts

    auto HVcorr = [](float x, char chan) {
	    if (chan == 'A') return (x - 33.43)/0.7453;       
	    else return (x - 33.45)/0.7501;
    };

    float step = 0.806f; // mV per DAC step
    uint16_t vsteps;
    if (volt > 0 && volt < 2500) {
        vsteps = roundf(volt / step);
    } else {
        snprintf(logmsg, sizeof(logmsg), "dacLoad: the requested voltage setting %d is out of range. The old setting remains.\n", volt);
        logger(logmsg);
        UARTmsg("ERROR: the requested DAC setting " + to_string(volt) + " is out of range.\n");
        return EXIT_FAILURE;
    }

    // --- Initialize I2C DAC ---
    int res, address = 0;
    uint8_t buf[5];

    int tpause = 0;
    switch (DAC) {
    case 1: res = rp_I2C_InitDevice("/dev/i2c-0", U3DAC_ADDR); tpause = 1;  break;
    case 2: res = rp_I2C_InitDevice("/dev/i2c-0", U4DAC_ADDR); tpause = 1; break;
    case 3: res = rp_I2C_InitDevice("/dev/i2c-0", U19DAC_ADDR); tpause = 3; break;
    case 4: res = rp_I2C_InitDevice("/dev/i2c-0", U20DAC_ADDR); tpause = 3; break;
        default: snprintf(logmsg, sizeof(logmsg), " dacLoad: bad DAC choice %d\n", DAC); logger(logmsg); return EXIT_FAILURE;
    }

    snprintf(logmsg, sizeof(logmsg), "dacLoad: I2C Init result: %d\n", res);
    logger(logmsg);
    char initresult[64];
    snprintf(initresult, sizeof(initresult), "|%d|", res);

    rp_I2C_getDevAddress(&address);
    snprintf(logmsg, sizeof(logmsg), "dacLoad: dev address: %x\n", address);
    logger(logmsg);
    char devcaddydec[64];
    snprintf(devcaddydec, sizeof(devcaddydec), "|%x|", address);

    rp_I2C_setForceMode(true);

    // Prepare buffer for DAC write
    buf[0] = (writeEEPROM) ? 0b01100000 : 0b01000000;
    buf[1] = vsteps >> 4;
    buf[2] = (vsteps & 0x000F) << 4;

    // --- Write and read from DAC ---
    res = rp_I2C_IOCTL_WriteBuffer(buf, 3);
    #ifdef VERBOSE
        printf("dacLoad: written string: %x %x %x\n", buf[0], buf[1], buf[2]);
    #endif
    char writehex[64];
    snprintf(writehex, sizeof(writehex), "%x %x %x|", buf[0], buf[1], buf[2]);
    
    #ifdef VERBOSE
        printf("dacLoad: DAC Write result: %d\n", res);
    #endif
    char writedec[64];
    snprintf(writedec, sizeof(writedec), "|INT: %d  HEX:", res);

    usleep(10000);

    res = rp_I2C_IOCTL_ReadBuffer(buf, 5);
    #ifdef VERBOSE
        printf("dacLoad: DAC Read result: %d\n", res);
    #endif
    char readdec[64];
    snprintf(readdec, sizeof(readdec), "|INT: %d   HEX: ", res);

    #ifdef VERBOSE
        printf("dacLoad: read bytes: %02X %02X %02X %02X %02X\n", buf[0], buf[1], buf[2], buf[3], buf[4]);
    #endif
    char readhex[64];
    snprintf(readhex, sizeof(readhex), "%02X %02X %02X %02X %02X|",
             buf[0], buf[1], buf[2], buf[3], buf[4]);

    if (writeEEPROM) {
        snprintf(logmsg, sizeof(logmsg), "dacLoad: the DAC and EEPROM were written to the value %d mV.\n", volt);
        logger(logmsg);
    } else {
        snprintf(logmsg, sizeof(logmsg), "dacLoad: the DAC was written to the value %d mV. No change to EEPROM.\n", volt);
        logger(logmsg);
    }
    
    // Read analog input pin (AIN)
    snprintf(logmsg, sizeof(logmsg), "dacLoad: pause %d seconds for the voltage to stabilize, and then measure it. . .\n",tpause);
    logger(logmsg);
    sleep(tpause);
    float vain3;
    uint32_t raw3;
    switch (DAC) {
        case 1: rp_ApinGetValue(RP_AIN0, &vain3, &raw3); break;
        case 2: rp_ApinGetValue(RP_AIN1, &vain3, &raw3); break;
        case 3: rp_ApinGetValue(RP_AIN2, &vain3, &raw3); break;
        case 4: rp_ApinGetValue(RP_AIN3, &vain3, &raw3); break;
    }
    snprintf(logmsg, sizeof(logmsg), "dacLoad: read Voltage: %.3f V, Raw: %u\n", vain3, raw3);
    logger(logmsg);
    if (DAC < 3) {
        UARTmsg("STATUS: measured voltage for DAC " + to_string(DAC) + " is " + to_string(vain3) + " V\n");
        return EXIT_SUCCESS;
    }
    float r1=10000000;          // first resistance in the voltage divider, in ohms
    float r2=10200;             // second resistance in the voltage divider, in ohms
    float scale=((r1+r2)/r2);
    float r_out=(scale*vain3);
	float r_corr = r_out;
	if (DAC == 3) r_corr = HVcorr(r_out, 'A');
	else r_corr = HVcorr(r_out, 'B');

    snprintf(logmsg, sizeof(logmsg), "dacLoad: high Voltage output is %.4f volts (uncorrected read value is %.4f volts).\n",r_corr, r_out);
    logger(logmsg);
    UARTmsg("STATUS: measured HV for DAC " + to_string(DAC) + " is " + to_string(r_corr) + " V\n");

    return EXIT_SUCCESS;
}

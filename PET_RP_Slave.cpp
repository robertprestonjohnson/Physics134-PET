// PET_RP.cpp : This file contains the 'main' function. Program execution begins and ends there.
// 
// This is the UCSC PET-experiment C code that runs on the Red Pitaya.
// This 'main' function runs continuously in the Red Pitaya and receives commands from the PC GUI via UART.
// It also sends some data back to the PC via UART and larger data files back via Ethernet file-transfer-protocol.
// 
// Robert P. Johnson
// January 2026
//
#include "rp.h"
#include "rp_hw.h"
#include "arg.h"
#include "PET.h"

#include <stdio.h>

int main(int argc, char* argv[]) {  
    // Arguments:
    //     verbose     -- to turn on debug printing
    //     time        -- integer, to set the UART timeout in seconds
    //                 -- if both arguments are given, verbose must come first
    
    bool verbose = false;
    int timeOut = 100;
    if (argc > 1) {
        if (strcmp(argv[1], "verbose") == 0) {
            verbose = true;
            printf("PET_RP_Slave: verbose mode turned on.\n");
        }
        else {
            timeOut = atoi(argv[1]) * 10;
            printf("PET_RP_Slave: UART timeout set to %d seconds\n", timeOut/10);
        }
        if (argc > 2 && verbose) {
            timeOut = atoi(argv[2]) * 10;
            printf("PET_RP_Slave: UART timeout set to %d seconds\n", timeOut/10);
        }
    }

    const int size = 1024;    // Maximum number of characters in the UART buffer
    PET myPET(size);         // Creates an instance of the PET class
    myPET.setVerbose(verbose);

    const int mxArg = 50;    // Maximum number of tokens in the command string

    // Intialize the UART
    char rx_buf[size];
    
    int res = rp_UartInit(); // init uart api
    if (res != EXIT_SUCCESS) {
        printf("UART initialization failed. Abort\n");
        return res;
    }
    myPET.setUartActive(true);

    res = rp_UartSetTimeout(100);   // set the UART timeout to 10 seconds
    if (res != EXIT_SUCCESS) {
        printf("Failure to set UART timeout. Abort\n");
        return res;
    }
    
    res = rp_UartSetSpeed(115200); // set uart speed
    if (res != EXIT_SUCCESS) {
        printf("Failure to set the UART speed. Abort\n");
        return res;
    }

    res = rp_UartSetBits(RP_UART_CS8); // set word size to 8 bytes
    if (res != EXIT_SUCCESS) {
        printf("Failure to set the UART word size. Abort\n");
        return res;
    }

    res = rp_UartSetStopBits(RP_UART_STOP1);  // set one stop bit
    if (res != EXIT_SUCCESS) {
        printf("Failure to set the UART stop bit spec\n");
        return res;
    }

    res = rp_UartSetParityMode(RP_UART_NONE);  // set no parity
    if (res != EXIT_SUCCESS) {
        printf("Failure to set the UART parity spec\n");
        return res;
    }

    res = rp_UartSetSettings(); // apply settings to uart
    if (res != EXIT_SUCCESS) {
        printf("Failure to apply the UART settings\n");
        return res;
    }

    // Go into an infinite loop, at least until a CLOSE command is received
    int Argc = 0;
    char* Argv[mxArg];
    string s = "PET_RP";
    Argv[Argc] = new char[s.length() + 1];
    memcpy(Argv[Argc], s.c_str(), s.length() + 1);
    
    int rdSize = 0;
    while (true) {   // Loop forever or until a "CLOSE" command is received, or somebody kills the process
        memset(rx_buf, '\0', size);   // zero the buffer to avoid confusion in printouts
        
        // Read something from the UART, as sent by Python on the PC
        rdSize = size;    // rp_UartRead overwrites the buffer size argument, so it has to be reset before each call
        res = rp_UartRead((unsigned char*)rx_buf, &rdSize);  // the returned rdSize is the actual size of the string
        if (verbose) printf("PET_RP_Slave: UART read (%s) %d  res=%d\n", rx_buf, rdSize, res);
        if (res != EXIT_SUCCESS) {
            if (verbose) printf("PET_RP_Slave: error return from rp_UartRead = %d, %s\n", res, rp_HwGetError(res));
            continue;
        }

        string someThing(rx_buf, rdSize);
        if (someThing == "CLOSE") {    // This will end execution of the entire program!
            const char* buffer = "Goodbye!\n";
            res = rp_UartWrite((unsigned char*)buffer, strlen(buffer));
            if (res != EXIT_SUCCESS) {
                printf("PET_RP_Slave: error writing '%s' to UART: %s\n",buffer, rp_HwGetError(res));
            }
            break;
        }
        if (someThing == "R U THERE?") {     // Just to check that somebody is on the other end of the line
            const char* buffer = "YES I AM\n";
            res = rp_UartWrite((unsigned char*)buffer, strlen(buffer));
            if (res != EXIT_SUCCESS) {
                printf("PET_RP_Slave: error writing '%s' to UART: %s\n", buffer, rp_HwGetError(res));
            }
            continue;
        }
        // Send back one of the latest pulse-height histograms in binary format
        if (someThing == "Send Histogram A" || someThing == "Send Histogram B") {
            int det = 1;
            if (someThing == "Send Histogram A") det = 0;
            int nHistBins;
            uint32_t* histoGram = myPET.getHistogram(det, &nHistBins);
            memcpy(rx_buf, histoGram, nHistBins * sizeof(histoGram[0]));
            rx_buf[nHistBins * sizeof(histoGram[0])] = '\n';
            res = rp_UartWrite((unsigned char*)rx_buf, nHistBins * sizeof(histoGram[0]) + 1);
            if (res != EXIT_SUCCESS) {
                printf("PET_RP_Slave: error writing histogram to UART: %s\n", rp_HwGetError(res));
            }
            continue;
        }
        if (someThing == "START") {    // Start receiving a command string
            const char* buffer3 = "Starting\n";
            res = rp_UartWrite((unsigned char*)buffer3, strlen(buffer3));
            if (res != EXIT_SUCCESS) {
                printf("PET_RP_Slave: error writing '%s' to UART: %s\n", buffer3, rp_HwGetError(res));
            }
            Argc = 0;         // The zeroth arg is the program name per Linux standard
            while (true) {    // Loop over tokens in the command string, plus the end marker
                Argc++;
                memset(rx_buf, '\0', size);   // Zero the buffer to avoid confusing debug printout
                rdSize = size;
                res = rp_UartRead((unsigned char*)rx_buf, &rdSize);
                if (verbose) printf("PET_RP_Slave: UART read (%s) %d, res = %d\n", rx_buf, rdSize, res);
                if (res != EXIT_SUCCESS) {
                    printf("PET_RP_Slave: error %d reading UART: %s\n", res, rp_HwGetError(res));
                    continue;
                }
                string token(rx_buf, rdSize);  // This string constructor makes a **copy** of rdSize characters
                if (verbose) printf("Token received for Argc=%d is %s\n", Argc, token.c_str());
                string ack = "Ack token '" + token + "'\n";
                if (verbose) printf("ack=%s\n", ack.c_str());
                char* buffer4 = new char[ack.length() + 1];
                memcpy(buffer4, ack.c_str(), ack.length() + 1);    
                res = rp_UartWrite((unsigned char*)buffer4, strlen(buffer4));
                if (res != EXIT_SUCCESS) {
                    printf("PET_RP_Slave: error writing '%s' to UART: %s\n", buffer4, rp_HwGetError(res));
                }
                if (token == "END") break;
                delete[] buffer4;
                if (verbose) printf("For Argc=%d allocate new char for string %s, length %d\n", Argc, token.c_str(),token.length());
                Argv[Argc] = new char[token.length() + 1]; 
                memcpy(Argv[Argc], token.c_str(), token.length() + 1);
                if (Argc == mxArg - 1) {
                    Argc++;
                    break;
                }
            } 
            string nl = "\0";
            Argv[Argc] = new char[nl.length() + 1];  // Put a null character in the next Argv string per Linux std (even though probably not used here)
            memcpy(Argv[Argc], nl.c_str(), nl.length() + 1);

            printf("PET_RP_Slave command submitted: ");   // Print out the full command on one line, including the null
            for (int i = 0; i <= Argc; ++i) {
                printf("%s ", Argv[i]);
            }
            printf("\n");

            // Here, finally, we submit the command to be parsed and executed
            int ret = myPET.parseCMD(Argc, Argv);   // Blocks further execution until the action is complete
            if (ret != EXIT_SUCCESS) {
                printf("Error parsing or executing the command line in PET::parseCMD.\n");
            }

            // Notify the Python program that command execution is complete
            const char* buffer2 = "DONE\n";   
            if (verbose) printf("PET_RP_Slave: command execution finished. Sending 'DONE'\n");
            res = rp_UartWrite((unsigned char*)buffer2, strlen(buffer2));
            if (res != EXIT_SUCCESS) {
                printf("PET_RP_Slave: error writing '%s' to UART: %s\n", buffer2, rp_HwGetError(res));
            }

            // Free the memory used by the character strings, as the pointers will be used again for the next command
            if (verbose) printf("PET_RP_Slave: freeing %d token strings Argv. . .\n", Argc);
            for (int i = 1; i <= Argc; ++i) {
				delete[] Argv[i];  // includes deleting Argv[Argc] = '\0'
			}
            continue;
        }

        printf("PET_RP_Slave: unrecognized command '%s' received.\n", someThing.c_str());
    }
	if (verbose) printf("PET_RP_Slave: deleting Argv[0] %s\n", Argv[0]); 
    delete[] Argv[0];

    printf("PET_RP_Slave is closing.");
    res = rp_UartRelease(); // close uart api
    if (res == EXIT_SUCCESS) myPET.setUartActive(false);
    
    return res;
}

#include "arg.cpp"
#include "PET.cpp"
// PET_RP.cpp : This file contains the 'main' function. Program execution begins and ends there.
// 
// This is the UCSC PET-experiment C code that runs on the Red Pitaya.
// This 'main' function requires command-line input from an operator logged into the Red Pitaya.
// Another code runs continuously in the Red Pitaya and receives commands from the PC GUI via UART.
// Both codes use the same PET class to parse the commands and execute the requested actions.
// 
// Robert P. Johnson
// January 2026
//
#include "arg.h"
#include "PET.h"

#include <stdio.h>

int main(int argc, char* argv[]) {
    
    PET myPET(1024);    // Creates an instance of the PET class

    // Parse the command line and execute the corresponding action
    int ret = myPET.parseCMD(argc, argv);
    if (ret != EXIT_SUCCESS) {
        printf("Error parsing or executing the command line in PET::parseCMD.\n");
        return ret;
    }

    return EXIT_SUCCESS;
}

#include "arg.cpp"
#include "PET.cpp"
# Physics134-PET
Software for the PET experiment of the Physics 134 course at U.C. Santa Cruz

The C++ files are code to run on the Red Pitaya embedded processor.
The Python GUI runs on the PC and communicates with the Red Pitaya.
Other Python files for the PC are for testing or for image reconstruction.

There are two versions of the main program for the Red Pitaya. PET_RP.cpp operates from the command line after logging into the Red Pitaya. It should not normally be used by students. PET_RP_Slave.cpp is meant to run constantly on the Red Pitaya. It should start up automatically when the PET hardware is powered on. It sits idle waiting for commands to arrive from the PC GUI via a UART link. When it receives a command, it executes it and then returns to the idle state. However, some commands, like "Scan" can take multiple days to complete!

import serial
import time

def sndcmd(cmdList):
    for cmd in cmdList:
        data_to_send = cmd;
        print("Sending ", data_to_send.encode('ascii'))
        ser.write(data_to_send.encode('ascii'))
        s = ser.readline()
        print("Reply is ", s)  # This should print out "Ack Token" to acknowledge reception of the token
    while True:     # Print out messages that come back during the command execution
        s = ser.readline()
        print("Message is ", s)     # Empty messages will get printed if the readline() times out
        if s == b'DONE\n': break     # A "DONE" mesage should come back when the execution terminates
 

print("Sending test data")
try:
    ser = serial.Serial(port='COM5',baudrate=115200,stopbits=serial.STOPBITS_ONE,parity=serial.PARITY_NONE,bytesize=serial.EIGHTBITS,timeout=20)
    time.sleep(1)
    print(ser.name)

    # Send a message to see if the RP is alive. . .
    data_to_send = "R U THERE?"
    print("Sending ", data_to_send.encode('ascii'))
    ser.write(data_to_send.encode('ascii'))
    s = ser.readline()       # Everything coming back from the RP should have \n at the end, so that readline will work
    print("Reply is ", s)    # This should print out "Yes I am"
    
    # Send a typical command to be executed, bracketed by 'START' and 'END'

    print("Turn on the HV for Channel A")
    cmdList = ['START', 'setHV', '-v', '20', '-C', 'chA', '-w', 'yes', 'END']
    sndcmd(cmdList)

    print("Turn on the HV for Channel B")
    cmdList = ['START', 'setHV', '-v', '20', '-C', 'chB', '-w', 'yes', 'END']
    sndcmd(cmdList)
    
    # Send a command that will cause the RP program to exit its infinite loop and terminate execution
    #data_to_send = "CLOSE"
    #print("Sending ", data_to_send.encode('ascii'))
    #ser.write(data_to_send.encode('ascii'))
    #s = ser.readline()
    #print("Reply is ", s)   # This should print out "Goodbye!"
   
except serial.SerialException as e:
    print("Serial Connection Failed: {e}") 
    ser = None
    

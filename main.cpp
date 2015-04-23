//**************************************************************************
// PROJECT: pine
// Version: V01.00
// Author: Mehmet Bora YILDIZ
// Update: 24.04.2014
//**************************************************************************
//**************************************************************************


//**************************************************************************
//INCLUDE LIBRARIES
//**************************************************************************
#include "mbed.h"
#include "rtos.h"
#include "EthernetInterface.h"
#include "SerialUART1.h"
#include "SerialUART3.h"
#include "SerialUART2.h"
#include <string>
#include <iostream>
#include <stdlib.h>
using namespace std;

//**************************************************************************
//DEFINITIONS
//**************************************************************************
//UDP
#define UDP_PORT    51984

//RS485
#define RS485_Read   0
#define RS485_Write   1

//**************************************************************************
//GLOBAL VARIABLES
//**************************************************************************

//GLOBAL
char feedbackString[255];

//PACKET DATA
int Packet_DeviceID;
char Packet_DataType;
int Packet_Channel;
int Packet_Data_Length;
char PacketData[255];

//SYSTEM CONFIG
int deviceID = 1;
string IPAddress = "192.168.1.100";
string SubnetMask = "255.255.255.0";
string Gateway = "192.168.1.1";
int RS232Port1BaudRate;
int RS232Port2BaudRate;

//LOCAL FILE SYSTEM
LocalFileSystem local("local"); 
FILE *file;
char line[128];

//IR
char IRCode[1024];

//ETHERNET
EthernetInterface ethernet;

//UDP
UDPSocket UDP_server;
Endpoint UDP_endpoint;
char UDP_buffer[255];

//RELAY
bool statusRelay1 = false;
bool statusRelay2 = false;
bool statusRelay3 = false;

//GPIO
int inputLowcounter[12];
int inputHighcounter[12];
bool inputFlag[12];

//Mutexs
Mutex PacketParser_Mutex;
Mutex PacketHandler_Mutex;
Mutex WriteRelay_Mutex;
Mutex WriteRS_Mutex;
Mutex WriteIR_Mutex;


//**************************************************************************
//HARDWARE CONFIGURATION
//**************************************************************************
//USB
Serial USB(USBTX, USBRX);                      

//RS485
SerialUART1 RS485(p13,p14);                    
DigitalOut RS485_Mode(p12);

//RS232
SerialUART3 RS232_1(p9,p10);                    
SerialUART2 RS232_2(p28,p27);                   

//RELAY
DigitalOut Relay1(p5);                        
DigitalOut Relay2(p6);                         
DigitalOut Relay3(p7);                         

//GPIO
DigitalInOut GPIO1(p15);                        
DigitalInOut GPIO2(p16);                        
DigitalInOut GPIO3(p17);                       
DigitalInOut GPIO4(p18);                      
DigitalInOut GPIO5(p19);                        
DigitalInOut GPIO6(p20);                       
DigitalInOut GPIO7(p11);                         
DigitalInOut GPIO8(p8);                      

//IR
PwmOut IR1(p26);
PwmOut IR2(p25);
PwmOut IR3(p24);
PwmOut IR4(p23);
PwmOut IR5(p22);
PwmOut IR6(p21);

//LED
DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);
DigitalOut led4(LED4);


//**************************************************************************
//FUNCTION DECLERATIONS
//**************************************************************************
//MAIN START
void mainStart();

//LOCAL FILE SYSTEM 
void read_ConfigFile();
string parse_Line(const char* ptrLine);


//PACKET HANDLER FUNCTIONS
void packetParser(char* packet);
void packetHandler(char Packet_DataType, int Packet_Channel, int Packet_Data_Length, char* PacketData);
void writeRS485(char* value);
void writeIR(char channel, char btnID);

//RS232
void writeRS232(char channel, char* data, int length);

//RELAY
void writeRelay(char channel, char value);
void relayStatusFeedback(char channel, char value);

//IR
void writeIR(char IRPort, char IRChannel);
void send_IR_Code(char IRPort, char* IRCode);

//GPIO
void GPIO1_LowEvent();
void GPIO2_LowEvent();
void GPIO3_LowEvent();
void GPIO4_LowEvent();
void GPIO5_LowEvent();
void GPIO6_LowEvent();
void GPIO7_LowEvent();
void GPIO8_LowEvent();
void gpioStatusFeedback(char channel, char value);


//**************************************************************************
//THREADS
//**************************************************************************

//UDP_thread
void UDP_thread(void const *args) 
{
        
    while (true) 
    {    
        //Wait for packet receive       
        printf("Waiting for UDP packet...\n"); Thread::wait(100);         
        UDP_server.receiveFrom(UDP_endpoint, UDP_buffer, sizeof(UDP_buffer));       
        
 
        //Print Data & CheckSum
        char UDP_PacketLength = UDP_buffer[4] + 6;
        char Checksum = 0;
        printf("UDP Data:"); 
        for(int i = 0 ; i < UDP_PacketLength; i++)
        {
            printf("%c", UDP_buffer[i]); 
            if (i != UDP_PacketLength - 1 ) Checksum = Checksum + UDP_buffer[i];
        }
        
        
        //Packet Parser
        if(Checksum == UDP_buffer[UDP_PacketLength - 1])
        { 
            PacketParser_Mutex.lock();                       
            packetParser(UDP_buffer); 
            PacketParser_Mutex.unlock();   
        }
        
        //Packet Handler
        if(Packet_DeviceID == deviceID)
        {   
            PacketHandler_Mutex.lock();   
            packetHandler(Packet_DataType, Packet_Channel, Packet_Data_Length, PacketData); 
            PacketHandler_Mutex.unlock();             
        }
        
                
        //Clear UDP_buffer
        memset(UDP_buffer, 0x00, 255);
        
        //Debug Led
        led2 = !led2;
        
        //Thread wait in ms   
        Thread::wait(100);      
        
    }
}


//RS485_thread
void RS485_thread(const void *args)
{
    while (true) 
    {    
        //RS485 read
        RS485_Mode = RS485_Read; 
        
        //Wait for packet receive       
        printf("Waiting for RS485 data...\n"); 
          
        //Read a line from the large rx buffer from rx interrupt routine
        RS485.read_line();
        
        //Print Data & CheckSum
        char Checksum = 0;
        printf("RS485 Data: "); 
        for(int i = 0 ; i < RS485.packetLength; i++)
        {
            printf("%c", RS485.rx_data_bytes[i]); 
            if (i != RS485.packetLength - 1 ) Checksum = Checksum + RS485.rx_data_bytes[i];
        }
        printf("\n");
                
        //CheckSum
        if(Checksum == RS485.rx_data_bytes[RS485.packetLength - 1])
        {
            //Packet Parser 
            PacketParser_Mutex.lock();  
            packetParser(RS485.rx_data_bytes); 
            PacketParser_Mutex.unlock();  
                    
            //Packet Handler
            PacketHandler_Mutex.lock();  
            packetHandler(Packet_DataType, Packet_Channel, Packet_Data_Length, PacketData);  
            PacketHandler_Mutex.unlock();     
        }
        
        //Clear RS485 Buffer
        memset(RS485.rx_data_bytes, 0x00, 255);   
        
        //Debug Led
        led3 = !led3;
        
        //Thread wait in ms   
        Thread::wait(100);      
    }    
}

//RS232_1_thread
void RS232_1_thread(const void *args)
{
    while (true) 
    {
                        
        //Read a line from the large rx buffer from rx interrupt routine
        RS232_1.read_line();
        
        //Print Data 
        printf("RS232_1 Data: "); 
        for(int i = 0 ; i < RS232_1.packetLength; i++)
        {
            printf("%c", RS232_1.rx_data_bytes[i]); 
        }
        printf("\n"); 
        
        //Send Received Data to TouchPanel
        UDP_endpoint.set_address("192.168.1.51", UDP_PORT); 
        UDP_server.sendTo(UDP_endpoint, RS232_1.rx_data_bytes, RS232_1.packetLength);
             
        //Clear RS232_1 Buffer
        memset(RS232_1.rx_data_bytes, 0x00, 255);  
        
        //Debug Print
        printf("RS232_1_thread\n");
        
        //Thread wait in ms              
        Thread::wait(1000);  
           
    }    
}

//RS232_2_thread
void RS232_2_thread(const void *args)
{
    while (true) 
    {                     
        //Read a line from the large rx buffer from rx interrupt routine
        RS232_2.read_line();
        
        //Print Data 
        printf("RS232_2 Data: "); 
        for(int i = 0 ; i < RS232_2.packetLength; i++)
        {
            printf("%c", RS232_2.rx_data_bytes[i]); 
        }
        printf("\n");
        
        //Send Received Data to TouchPanel
        UDP_endpoint.set_address("192.168.1.51", UDP_PORT); 
        UDP_server.sendTo(UDP_endpoint, RS232_2.rx_data_bytes, RS232_2.packetLength);
               
        //Clear RS232_2 Buffer
        memset(RS232_2.rx_data_bytes, 0x00, 255); 
        
        //Debug Print
        printf("RS232_2_thread\n");
        
        //Thread wait in ms              
        Thread::wait(1000);                 
    }    
}


//GPIO_thread
void GPIO_thread(void const *args)
{
    while (true) 
    {
        //Thread Debug Led
        led4= !led4;  
        
        //Check GPIO
        if( GPIO1 == 0) inputLowcounter[0]++; if( GPIO1 == 1) inputHighcounter[0]++;   
        if( GPIO2 == 0) inputLowcounter[1]++; if( GPIO2 == 1) inputHighcounter[1]++;
        if( GPIO3 == 0) inputLowcounter[2]++; if( GPIO3 == 1) inputHighcounter[2]++;
        if( GPIO4 == 0) inputLowcounter[3]++; if( GPIO4 == 1) inputHighcounter[3]++;
        if( GPIO5 == 0) inputLowcounter[4]++; if( GPIO5 == 1) inputHighcounter[4]++;
        if( GPIO6 == 0) inputLowcounter[5]++; if( GPIO6 == 1) inputHighcounter[5]++;
        if( GPIO7 == 0) inputLowcounter[6]++; if( GPIO7 == 1) inputHighcounter[6]++;
        if( GPIO8 == 0) inputLowcounter[7]++; if( GPIO8 == 1) inputHighcounter[7]++;

        //GPIO State Result
        for(int i = 0; i < 8; i++)
        {
            //GPIO LOW State
            if (inputLowcounter[i] > 45) 
            {                
                //Print status
                printf("GPIO%d LOW \n", (i+1));
                
                //GPIO Events
                if(inputFlag[i] == false)
                {
                    switch (i)
                    {   
                        //GPIO1
                        case 0:
                            GPIO1_LowEvent();
                            break;
                            
                        //GPIO2
                        case 1:
                            GPIO2_LowEvent();
                            break;
                            
                        //GPIO3
                        case 2:
                            GPIO3_LowEvent();
                            break;
                            
                        //GPIO4
                        case 3:
                            GPIO4_LowEvent();
                            break;
                            
                        //GPIO5
                        case 4:
                            GPIO5_LowEvent();
                            break;
                            
                        //GPIO6
                        case 5:
                            GPIO6_LowEvent();
                            break;
                            
                        //GPIO7
                        case 6:
                            GPIO7_LowEvent();
                            break;
                            
                        //GPIO8
                        case 7:
                            GPIO8_LowEvent();
                            break;
                    }
                }
                
                //Set input flag
                inputFlag[i] = true;
                
                //Clear counters
                inputHighcounter[i] = 0;
                inputLowcounter[i] = 0;
                               
            }
    
            if (inputHighcounter[i] > 5) 
            {                
                //Reset input flag
                inputFlag[i] = false;
                
                //Clear counters
                inputHighcounter[i] = 0;
                inputLowcounter[i] = 0;
            }
         }
              
        //Debug Led
        led4 = !led4; 
        
        //Thread wait in ms              
        Thread::wait(5);  
              
    }
}


//**************************************************************************
//MAIN
//**************************************************************************
int main()
{

    //Initialize the System
    mainStart();
    
    //Start Threads (CURRENTLY, IT IS NOT POSSIBLE TO WORK WITH MORE THAN 4 THREADS) ASK TO SUPPORT!
    Thread threadUDP(UDP_thread);
    //Thread threadGPIO(GPIO_thread);
    Thread threadRS485(RS485_thread);
    //Thread threadRS232_1(RS232_1_thread);
    //Thread threadRS232_2(RS232_2_thread);

    
    //Infinite Loop 
    while (1) 
    {
        led1 = !led1;              
        Thread::wait(1000);
    }
}


//**************************************************************************
// MAIN START
//**************************************************************************
void mainStart()
{
    //SYSTEM CONFIGURATION
    read_ConfigFile();
    
    //ETHERNET Use Static
    ethernet.init(IPAddress.c_str(), SubnetMask.c_str(), Gateway.c_str());     
    ethernet.connect();
    
    //UDP Init    
    UDP_server.bind(UDP_PORT);

    
    //RS485 Init
    RS485.baud(9600);
       
    //RS232_1 Init
    RS232_1.baud(9600);
    
    //RS232_2 Init
    RS232_2.baud(9600);
    RS232_2.format(8, Serial::Odd, 1);
    
    //GPIO
    GPIO1.mode(PullUp);
    GPIO2.mode(PullUp);
    GPIO3.mode(PullUp);
    GPIO4.mode(PullUp);
    GPIO5.mode(PullUp);
    GPIO6.mode(PullUp);
    GPIO7.mode(PullUp);
    GPIO8.mode(PullUp);
    
    //IR Init
    IR1 = 0.0f;
    IR2 = 0.0f;
    IR3 = 0.0f;
    IR4 = 0.0f;
    IR5 = 0.0f;
    IR6 = 0.0f;
     
    //System Initialize OK
    printf("System Initialize OK...\n");
    
}


//**************************************************************************
// PACKET HANDLER
//**************************************************************************

//Packet Handler
void packetParser(char* packet)
{

    //Device ID
    Packet_DeviceID = packet[1];

    //Data Type
    Packet_DataType = packet[2];
   
    //Channel
    Packet_Channel = packet[3];
    
    //DataLength
    Packet_Data_Length = packet[4];
    
    //Data
    //printf("PacketData:"); 
    for(int i = 0 ; i < Packet_Data_Length; i++)
    {   
        PacketData[i] = packet[5 + i]; 
        //printf("%c", PacketData[i]); 
    }    
}


//Packet Event Handler
void packetHandler(char Packet_DataType, int Packet_Channel, int Packet_Data_Length, char* PacketData)
{        
    //SystemData   
    if( (0 < Packet_Channel) && (Packet_Channel < 10) )
    {
        
    }                 
    
    //GPIO Data
    else if ( (10 < Packet_Channel) && (Packet_Channel < 20) ) 
    {
        
    }       
    
    //RELAY Data
    else if ( (20 < Packet_Channel) && (Packet_Channel < 30) ) 
    {
        //Write Command
        if(Packet_DataType == 'W')
        {
            Packet_Channel = Packet_Channel - 20;
            writeRelay(Packet_Channel, PacketData[0]);
        }
    }         
    
     //RS232 Data
    else if ( (30 < Packet_Channel) && (Packet_Channel < 40) )                                    
    {
        //Write Command
        if(Packet_DataType == 'W')
        { 
            Packet_Channel = Packet_Channel - 30;
            writeRS232(Packet_Channel, PacketData, Packet_Data_Length);
        }
    }
    
     //IR Data
    if ( (40 < Packet_Channel) && (Packet_Channel < 50) )  
    {
        Packet_Channel = Packet_Channel - 40; 
                
        writeIR(Packet_Channel, PacketData[0]);             
    }  
       
    //RS485 Data
    else if ( Packet_Channel == 50 )                                                                
    {      
        wait_ms(8);
        RS485_Mode = RS485_Write;                                      
        for(int i = 0 ; i < Packet_Data_Length; i++)
            {
                RS485.printf("%c", PacketData[i]); 
            } 
        wait_ms(8);
        RS485_Mode = RS485_Read;     
    }          
}

//**************************************************************************
// RS232 FUNCTIONS
//**************************************************************************
void writeRS232(char channel, char* data, int length)
{
 
    switch(channel)
    {
        case 1:
        for(int i = 0 ; i < length; i++)
            {
                RS232_1.printf("%c", data[i]); 
            }        
            break;   
        case 2:
            for(int i = 0 ; i < length; i++)
            {
                RS232_2.printf("%c", data[i]); 
            }  
            break;
    }
           
}



//**************************************************************************
// RELAY FUNCTIONS
//**************************************************************************

//Write Relay
void writeRelay(char channel, char value)
{ 
    switch(channel){
        case 1:
            Relay1 = value;  
            statusRelay1 = value;  
            relayStatusFeedback(channel, value); 
            break;   
        case 2:
            Relay2 = value; 
            statusRelay2 = value; 
            relayStatusFeedback(channel, value); 
            break; 
        case 3:
            Relay3 = value; 
            statusRelay3 = value; 
            relayStatusFeedback(channel, value); 
            break; 
            
        //All Channels
        case 255:
            Relay1 = Relay2 = Relay3 =  value;  
            statusRelay1 = statusRelay2 = statusRelay3  = value;  
            relayStatusFeedback(channel, value); 
            break;  
        default:
    }  
}

//Relay Status Feedback
void relayStatusFeedback(char channel, char value)
{
    
    //Clear feedbackstring
    memset(feedbackString, 0x00, 255);   
    
    //Feedback received packet
    feedbackString[0] = 62;                                                                     //Start of Data ('>')                                        
    feedbackString[1] = deviceID;                                                               //Device ID
    feedbackString[2] = 'S';                                                                    //Data Type : Status
    feedbackString[3] = channel + 20;                                                           //Channel
    feedbackString[4] = 1;                                                                      //DataLength
    feedbackString[5] = value;                                                                  //Data
    feedbackString[6] = feedbackString[0] + feedbackString[1] + feedbackString[2] +             //Checksum                                                     
                        feedbackString[3] + feedbackString[4] + feedbackString[5];    
                        
                        
    //Send feedback data to UDP 
    UDP_endpoint.set_address("192.168.1.51", UDP_PORT);   
    UDP_server.sendTo(UDP_endpoint, feedbackString, 7);
      
    //Send feedback data to RS485 
    wait_ms(8);
    RS485_Mode = RS485_Write;                                      
    RS485.send_line(feedbackString); wait_ms(8);
    RS485_Mode = RS485_Read;
    
     //Send feedback data to USB
    /*
    printf("Status Message: "); 
    for(int i = 0 ; i < 7; i++){
        printf("%c", feedbackString[i]); 
    }
    */
    
}

//**************************************************************************
// IR FUNCTIONS
//**************************************************************************

//Write IR
void writeIR(char IRPort, char IRChannel)
{        
    //Open the file
    switch(IRPort)
    {
        case 1:
            file = fopen("/local/IR1.txt", "r");
            break;   
        case 2:
            file = fopen("/local/IR2.txt", "r");
            break; 
        case 3:
            file = fopen("/local/IR3.txt", "r");
            break; 
        case 4:
            file = fopen("/local/IR4.txt", "r");
            break;
        case 5:
            file = fopen("/local/IR5.txt", "r");
            break;
        case 6:
            file = fopen("/local/IR6.txt", "r");
            break;
    } 
    
    
    //Read IR Code and Send IR Code
     if (file != NULL) 
     {        
        //read the line #IRChannel
        for (int i = 0; i < IRChannel; i++) fgets(IRCode, sizeof IRCode, file);

        //parse Line & send IR Blinks
        send_IR_Code(IRPort, IRCode);

        //Close the file
        fclose(file);
        
    }
    else
    {
        //printf("Error: There is no IR file\n");   
    }   
}


//Parse Line & send IR Blinks
void send_IR_Code(char IRPort, char* ptrIRCode)
{
    char field[8];
    int n;
    int index = 0;
    int IRCodeIndex = 0; 
    int decimal;
    int frequency;
    int period_us;
    int blink_time_on_index = 0;
    int blink_time_off_index = 0;
    int pair_count_index = 0;
    int pair_count = 0;
    int Blink_time;
    int Blink_time_On[128];
    int Blink_time_Off[128];
    
    //printf("Parse IR Code:\n");
    
    //Parse Line with " " delimiter
    while ( sscanf(ptrIRCode, "%1023[^ ]%n", field, &n) == 1 ) 
    {
        index++;
        ptrIRCode += n;                     
        if ( *ptrIRCode != ' ' ) 
        {
            break;                          
        }
        ++ptrIRCode;                       

        //printf("Field_%d:%s\n", index,field);

        //Get Frequency and set PWM
        if (index == 2) 
        {
            
            sscanf(field, "%x", &decimal );         
            printf("Decimal:%d\n", decimal);
            
            //Calculate Frequency
            //frequency = 100000000 / (decimal * 24);
            period_us = (decimal * 0.24);
            //printf("Frequency:%d\n", frequency);
            //printf("Period_us:%d\n", period_us);
            
            //Set PWM Period -  Note: If you change one of the ports, all of them will change 
            switch(IRPort)
            {
                case 1:
                    IR1.period_us(period_us);
                    break;   
                case 2:
                    IR2.period_us(period_us);
                    break; 
                case 3:
                    IR3.period_us(period_us);
                    break; 
                case 4:
                    IR4.period_us(period_us);
                    break;
                case 5:
                    IR5.period_us(period_us);
                    break;
                case 6:
                    IR6.period_us(period_us);
                    break;
            }      
        }
        

        //Get total pair count
        if (index == 4) 
        {
            
            sscanf(field, "%x", &decimal );        
            //printf("Decimal:%d\n", decimal);
            
            //print PairCount
            pair_count = decimal;
            //printf("Pair Count:%d\n", pair_count);
              
        }

        //Get IRCode
        if (index > 4) 
        {
                                    
            switch(IRCodeIndex)
            {
                //TurnOnBlink
                case 0:
                    IRCodeIndex = 1;
                    sscanf(field, "%x", &decimal);         
                    Blink_time = decimal*period_us;
                    //printf("TurnOnBlink[%d]:%d us\n", blink_time_on_index, Blink_time);
                    Blink_time_On[blink_time_on_index] = Blink_time;
                    blink_time_on_index++;
                    break;
                    
                //TurnOffBlink
                case 1:
                    IRCodeIndex = 0;
                    sscanf(field, "%x", &decimal);         
                    Blink_time = decimal*period_us;
                    //printf("TurnOffBlink[%d]:%d us\n", blink_time_off_index, Blink_time);
                    Blink_time_Off[blink_time_off_index] = Blink_time;
                    blink_time_off_index++;
                    break;
            }  
              
            pair_count_index++;       
        }            
    } 
    
    //Send IR Signal    
    switch(IRPort)
    {
        case 1:
            //Send Pronto IR Blinks - x times
            for(int k = 0; k  < 1; k++)
            {
                for(int i = 0; i < pair_count; i++)
                {
                    //send IR Blinks On
                    IR1 = 0.5f;
                    wait_us(Blink_time_On[i]);
                    IR1= 0.0f;   
                
                    //send IR Blinks Off 
                    wait_us(Blink_time_Off[i]);
                }
            }
            break;   
        case 2:
            //Send Pronto IR Blinks - x times
            for(int k = 0; k  < 1; k++)
            {
                for(int i = 0; i < pair_count; i++)
                {
                    //send IR Blinks On
                    IR2 = 0.5f;
                    wait_us(Blink_time_On[i]);
                    IR2= 0.0f;   
                
                    //send IR Blinks Off 
                    wait_us(Blink_time_Off[i]);
                }
            }
            break;   
        case 3:
            //Send Pronto IR Blinks - 3 times
            for(int k = 0; k  < 3; k++)
            {
                for(int i = 0; i < pair_count; i++)
                {
                    //send IR Blinks On
                    IR3 = 0.5f;
                    wait_us(Blink_time_On[i]);
                    IR3= 0.0f;   
                
                    //send IR Blinks Off 
                    wait_us(Blink_time_Off[i]);
                }
            }
            break;   
        case 4:
            //Send Pronto IR Blinks - 5 times
            for(int k = 0; k  < 5; k++)
            {
                for(int i = 0; i < pair_count; i++)
                {
                    //send IR Blinks On
                    IR4 = 0.5f;
                    wait_us(Blink_time_On[i]);
                    IR4= 0.0f;   
                
                    //send IR Blinks Off 
                    wait_us(Blink_time_Off[i]);
                }
            }
            break;   
        case 5:
            //Send Pronto IR Blinks - x times
            for(int k = 0; k  < 1; k++)
            {
                for(int i = 0; i < pair_count; i++)
                {
                    //send IR Blinks On
                    IR5 = 0.5f;
                    wait_us(Blink_time_On[i]);
                    IR5= 0.0f;   
                
                    //send IR Blinks Off 
                    wait_us(Blink_time_Off[i]);
                }
            }
            break;   
        case 6:
            //Send Pronto IR Blinks - x times
            for(int k = 0; k  < 1; k++)
            {
                for(int i = 0; i < pair_count; i++)
                {
                    //send IR Blinks On
                    IR6 = 0.5f;
                    wait_us(Blink_time_On[i]);
                    IR6= 0.0f;   
                
                    //send IR Blinks Off 
                    wait_us(Blink_time_Off[i]);
                }
            }
            break;   
    }   

    //Clear Buffers
    memset(Blink_time_On, 0x00, 128);
    memset(Blink_time_Off, 0x00, 128);
}


//**************************************************************************
// GPIO EVENTS
//**************************************************************************

//GPIO1
void GPIO1_LowEvent()
{
    gpioStatusFeedback(1, 0);    
}

//GPIO2
void GPIO2_LowEvent()
{
    gpioStatusFeedback(2, 0);   
}

//GPIO3
void GPIO3_LowEvent()
{
    gpioStatusFeedback(3, 0);     
}

//GPIO4
void GPIO4_LowEvent()
{
    gpioStatusFeedback(4, 0);     
}

//GPIO5
void GPIO5_LowEvent()
{
    gpioStatusFeedback(5, 0);     
}

//GPIO6
void GPIO6_LowEvent()
{
    gpioStatusFeedback(6, 0);     
}

//GPIO7
void GPIO7_LowEvent()
{
    gpioStatusFeedback(7, 0);    
}

//GPIO8
void GPIO8_LowEvent()
{
    gpioStatusFeedback(8, 0);  
}


//GPIO Status Feedback
void gpioStatusFeedback(char channel, char value)
{
    
    //Clear feedbackstring
    memset(feedbackString, 0x00, 255);   
    
    //Feedback received packet
    feedbackString[0] = 62;                                                                     //Start of Data ('>')                                        
    feedbackString[1] = deviceID;                                                               //Device ID
    feedbackString[2] = 'S';                                                                    //Data Type : Status
    feedbackString[3] = channel + 10;                                                           //Channel
    feedbackString[4] = 1;                                                                      //DataLength
    feedbackString[5] = value;                                                                  //Data
    feedbackString[6] = feedbackString[0] + feedbackString[1] + feedbackString[2] +             //Checksum                                                     
                        feedbackString[3] + feedbackString[4] + feedbackString[5];    
    
    //Set UDP Endpoint
    UDP_endpoint.set_address("192.168.1.51", UDP_PORT);
                                                    
    //Send feedback data to UDP    
    UDP_server.sendTo(UDP_endpoint, feedbackString, 7);    
}


//**************************************************************************
// SYSTEM CONFIG 
//**************************************************************************

//Read Local File System Config File and set the deviceID, IP, RS232 settings
void read_ConfigFile()
{
    printf("Reading Config File...\n");
     
    file = fopen("/local/Config.txt", "r");              
    char line[64];
       
    if (file != NULL) 
    {
        
        //read the line #1,#2,#3,#4,#5,#6
        for (int i = 0; i < 6; i++)
        { 
            fgets(line, 64, file);
            
            switch(i)
            {
                case 0: 
                    deviceID = atoi(parse_Line(line).c_str());
                    //cout << "deviceID: " << deviceID << endl;
                    printf("deviceID: %d\n", deviceID);
                    break;
                case 1: 
                    IPAddress = parse_Line(line);
                    //cout << "IPAddress: " << IPAddress << endl;
                    printf("IPAddress: %s\n", IPAddress);
                    break;
                case 2: 
                    SubnetMask = parse_Line(line);
                    //cout << "SubnetMask: " << SubnetMask << endl;
                    printf("SubnetMask: %s\n", SubnetMask);
                    break;
                case 3: 
                    Gateway = parse_Line(line); 
                    //cout << "Gateway: " << Gateway << endl;
                    printf("Gateway: %s\n", Gateway);
                    break;
                case 4: 
                    RS232Port1BaudRate = atoi(parse_Line(line).c_str());
                    //cout << "RS232Port1BaudRate: " << RS232Port1BaudRate << endl;  
                    printf("RS232Port1BaudRate: %d\n", RS232Port1BaudRate);
                    break;
                case 5: 
                    RS232Port2BaudRate = atoi(parse_Line(line).c_str());  
                    //cout << "RS232Port2BaudRate: " << RS232Port2BaudRate << endl;
                    printf("RS232Port2BaudRate: %d\n", RS232Port2BaudRate);
                    break;
            }
            
        }

        //Close the file
        fclose(file);
              
    }
    else
    {
        printf("Error: There is no Config file\n");   
    }
}


//Parse Line
string parse_Line(const char* ptrLine)
{
    char field[64];
    int n;
    int index = 0;
    string returnStr;

    //Parse Line with ":" delimiter
    while ( sscanf(ptrLine, "%63[^:]%n", field, &n) == 1 ) 
    {
        index++;
        ptrLine += n;                       /* advance the pointer by the number of characters read */
        
        if ( *ptrLine != ':' ) 
        {
            break;                          /* didn't find an expected delimiter, done? */
        }
        
        ++ptrLine;                          /* skip the delimiter */

        //Get desired string
        if (index == 2) 
        {
            returnStr = field;
        }
    }
    
    return returnStr;
}
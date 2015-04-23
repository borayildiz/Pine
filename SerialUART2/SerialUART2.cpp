#include "SerialUART2.h"


//**************************************************************************
//CONSTRUCTOR
//**************************************************************************
SerialUART2::SerialUART2(PinName tx, PinName rx) : Serial(tx,rx), rx_sem(0), tx_sem(0)
{
    tx_in=0;
    tx_out=0;
    rx_in=0;
    rx_out=0;

    device_irqn = UART2_IRQn;

    // attach the interrupts
    Serial::attach(this, &SerialUART2::Rx_interrupt, Serial::RxIrq);
    Serial::attach(this, &SerialUART2::Tx_interrupt, Serial::TxIrq);
}

//**************************************************************************
//SEND
//**************************************************************************
void SerialUART2::send_line(char *c)
{
    int packetLength;
    int i = 0;
    char tempChar;
    bool empty;
    
    //Set Tx Data   
    for(int k = 0; k < LINE_SIZE; k++)
    {
        tx_line[k] = c[k]; 
    }
    
    //Set Datalength
    packetLength = tx_line[4] + 6;
    
    // Start Critical Section - don't interrupt while changing global buffer variables
    NVIC_DisableIRQ(device_irqn);
    
        // Check if buffer is empty
        empty = (tx_in == tx_out);
        
        //Set Tx Buffer
        while (i < packetLength) 
        {
            if (IS_TX_FULL) 
            { 
                NVIC_EnableIRQ(device_irqn);    // End Critical Section - need to let interrupt routine empty buffer by sending
                tx_sem.wait();                  
                NVIC_DisableIRQ(device_irqn);   // Start Critical Section - don't interrupt while changing global buffer variables
            }
            
            tx_buffer[tx_in] = tx_line[i];
            tx_in = NEXT(tx_in);
            
            i++;
        }
        
        //Send Tx Data
        if (Serial::writeable() && (empty)) 
        {
            tempChar = tx_buffer[tx_out];
            tx_out = NEXT(tx_out);
            LPC_UART2->THR = tempChar;          // Send first character to start tx interrupts, if stopped
        }
    
    // End Critical Section
    NVIC_EnableIRQ(device_irqn);
}



// Interupt Routine to write out data to serial port
void SerialUART2::Tx_interrupt()
{
    while ((writeable()) && (tx_in != tx_out)) { // while serial is writeable and there are still characters in the buffer
        LPC_UART2->THR = tx_buffer[tx_out];      // send the character
        tx_out = NEXT(tx_out);
    }
    if(!IS_TX_FULL)                              // if not full
        tx_sem.release();

}

//**************************************************************************
//READ
//**************************************************************************
void SerialUART2::read_line()
{
    int i = 0;
    packetLength = 255;
    
    NVIC_DisableIRQ(device_irqn);               // Start Critical Section - don't interrupt while changing global buffer variables
    
    
    // Loop reading rx buffer characters 
    while (i < packetLength) 
    {
        //Wait for Rx Byte
        NVIC_EnableIRQ(device_irqn);        // End Critical Section - need to allow rx interrupt to get new characters for buffer
        rx_sem.wait();
        NVIC_DisableIRQ(device_irqn);       // Start Critical Section - don't interrupt while changing global buffer variables
                    
        //Get Rx Data Byte
        rx_data_bytes[i] = rx_buffer;
                
        //Check first byte
        if(rx_data_bytes[0] == 62)
        {            
            //Get packetLength
            if(i == 4) packetLength = rx_data_bytes[4] + 6;
            
            //Next Byte
            i++;               
        }  
    }
    
    // End reading
    NVIC_EnableIRQ(device_irqn);                // End Critical Section
}

// Interupt Routine to read in data from serial port
void SerialUART2::Rx_interrupt()
{
    while (readable()) 
    {
        rx_buffer = LPC_UART2->RBR;
        rx_sem.release();
    }
}

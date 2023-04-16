#include "bcm2837/rpi_gpio.h"
#include "bcm2837/rpi_uart1.h"
#include "uart1.h"
#include "exception.h"
#include "u_string.h"
#include "bcm2837/rpi_irq.h"

//implement first in first out buffer with a read index and a write index
char uart_tx_buffer[VSPRINT_MAX_BUF_SIZE]={};
unsigned int uart_tx_buffer_widx = 0;  //write index
unsigned int uart_tx_buffer_ridx = 0;  //read index
char uart_rx_buffer[VSPRINT_MAX_BUF_SIZE]={};
unsigned int uart_rx_buffer_widx = 0;
unsigned int uart_rx_buffer_ridx = 0;

void uart_init()
{
    register unsigned int r;

    /* initialize UART */
    *AUX_ENABLES     |= 1;       // enable UART1
    *AUX_MU_CNTL_REG  = 0;       // disable TX/RX

    /* configure UART */
    *AUX_MU_IER_REG   = 0;       // disable interrupt
    *AUX_MU_LCR_REG   = 3;       // 8 bit data size
    *AUX_MU_MCR_REG   = 0;       // disable flow control
    *AUX_MU_BAUD_REG  = 270;     // 115200 baud rate
    *AUX_MU_IIR_REG   = 0xC6;    // disable FIFO

    /* map UART1 to GPIO pins */
    r = *GPFSEL1;
    r &= ~(7<<12);               // clean gpio14
    r |= 2<<12;                  // set gpio14 to alt5
    r &= ~(7<<15);               // clean gpio15
    r |= 2<<15;                  // set gpio15 to alt5
    *GPFSEL1 = r;

    /* enable pin 14, 15 - ref: Page 101 */
    *GPPUD = 0;
    r=150; while(r--) { asm volatile("nop"); }
    *GPPUDCLK0 = (1<<14)|(1<<15);
    r=150; while(r--) { asm volatile("nop"); }
    *GPPUDCLK0 = 0;

    *AUX_MU_CNTL_REG = 3;      // enable TX/RX
}

char uart_recv() {
    char r;
    while(!(*AUX_MU_LSR_REG & 0x01)){};
    r = (char)(*AUX_MU_IO_REG);
    return r=='\r'?'\n':r;
}

void uart_send(char c) {
    while(!(*AUX_MU_LSR_REG & 0x20)){};
    *AUX_MU_IO_REG = c;
}

void uart_2hex(unsigned int d) {
    unsigned int n;
    int c;
    for(c=28;c>=0;c-=4) {
        n=(d>>c)&0xF;
        n+=n>9?0x37:0x30;
        uart_send(n);
    }
}

int  uart_sendline(char* fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    char buf[VSPRINT_MAX_BUF_SIZE];

    char *str = (char*)buf;
    int count = vsprintf(str,fmt,args);

    while(*str) {
        if(*str=='\n')
            uart_send('\r');
        uart_send(*str++);
    }
    __builtin_va_end(args);
    return count;
}

// uart_async_getc read from buffer
// uart_r_irq_handler write to buffer then output
// This function is used to read a character from the UART receive buffer asynchronously
char uart_async_getc() {
    *AUX_MU_IER_REG |=1; // Enable read interrupt by setting bit 0 of the Interrupt Enable Register

    // Wait in a loop until there is data in the receive buffer
    while (uart_rx_buffer_ridx == uart_rx_buffer_widx) 
        *AUX_MU_IER_REG |=1; // Enable read interrupt

    el1_interrupt_disable(); // Disable interrupts to prevent data corruption
    char r = uart_rx_buffer[uart_rx_buffer_ridx++]; // Read the next character from the buffer
    if (uart_rx_buffer_ridx >= VSPRINT_MAX_BUF_SIZE) // Wrap the buffer index if it exceeds its maximum size
        uart_rx_buffer_ridx = 0;
    el1_interrupt_enable(); // Re-enable interrupts
    return r; // Return the character that was read
}


// uart_async_putc writes to buffer
// uart_w_irq_handler read from buffer then output
void uart_async_putc(char c) {

    // If the buffer is full, wait for uart_w_irq_handler
    while( (uart_tx_buffer_widx + 1) % VSPRINT_MAX_BUF_SIZE == uart_tx_buffer_ridx )  *AUX_MU_IER_REG |=2;  // enable write interrupt

    // Disable interrupts
    el1_interrupt_disable();

    // Add the character to the transmit buffer
    uart_tx_buffer[uart_tx_buffer_widx++] = c;

    // Wrap around the buffer index when it reaches the end
    if(uart_tx_buffer_widx >= VSPRINT_MAX_BUF_SIZE) uart_tx_buffer_widx=0;

    // Enable interrupts
    el1_interrupt_enable();

    // Enable write interrupt
    *AUX_MU_IER_REG |=2;  // enable write interrupt
}


int  uart_puts(char* fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    char buf[VSPRINT_MAX_BUF_SIZE];

    char *str = (char*)buf;
    int count = vsprintf(str,fmt,args);

    while(*str) {
        if(*str=='\n')
            uart_async_putc('\r');
        uart_async_putc(*str++);
    }
    __builtin_va_end(args);
    return count;
}


// AUX_MU_IER_REG -> BCM2837-ARM-Peripherals.pdf - Pg.12
void uart_interrupt_enable(){
    *AUX_MU_IER_REG |=1;  // enable read interrupt
    *AUX_MU_IER_REG |=2;  // enable write interrupt
    *ENABLE_IRQS_1 |= 1 << 29;    // Pg.112
}

void uart_interrupt_disable(){
    *AUX_MU_IER_REG &= ~(1);  // disable read interrupt
    *AUX_MU_IER_REG &= ~(2);  // disable write interrupt
}

void uart_r_irq_handler(){

    // If the buffer is full, disable read interrupt and return
    if((uart_rx_buffer_widx + 1) % VSPRINT_MAX_BUF_SIZE == uart_rx_buffer_ridx)
    {
        *AUX_MU_IER_REG &= ~(1);  // disable read interrupt
        return;
    }

    // Read data from UART receive buffer and add it to the buffer
    uart_rx_buffer[uart_rx_buffer_widx++] = uart_recv();

    // Echo the received data back to UART
    uart_send(uart_rx_buffer[uart_rx_buffer_widx-1]);

    // Wrap around the buffer index when it reaches the end
    if(uart_rx_buffer_widx>=VSPRINT_MAX_BUF_SIZE) uart_rx_buffer_widx=0;

    // Enable read interrupt
    *AUX_MU_IER_REG |=1;
}

void uart_w_irq_handler(){

    // If the buffer is empty, disable write interrupt and return
    if(uart_tx_buffer_ridx == uart_tx_buffer_widx)
    {
        *AUX_MU_IER_REG &= ~(2);  // disable write interrupt
        return;  // buffer empty
    }

    // Send data from the transmit buffer to UART
    uart_send(uart_tx_buffer[uart_tx_buffer_ridx++]);

    // Wrap around the buffer index when it reaches the end
    if(uart_tx_buffer_ridx>=VSPRINT_MAX_BUF_SIZE) uart_tx_buffer_ridx=0;

    // Enable write interrupt
    *AUX_MU_IER_REG |=2;
}


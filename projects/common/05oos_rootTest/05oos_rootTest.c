/**
\brief This is a program which shows how to use the "openserial" driver module.

Since the driver modules for different platforms have the same declaration, you
can use this project with any platform.

This application allows you to test that the openserial driver it working fine.
Once your board is running this application, use the serialTesterCli Python
application (part of the openwsn-sw repo) to issue serial echo commands, making
sure all is well.

\author Xavi Vilajosana <xvilajosana@eecs.berkeley.edu>, January 2013.
\author Thomas Watteyne <watteyne@eecs.berkeley.edu>, January 2014.
*/

#include "stdint.h"
#include "stdio.h"
// bsp modules required
#include "board.h"
#include "debugpins.h"
#include "leds.h"
#include "uart.h"
#include "sctimer.h"

// driver modules required
#include "openserial.h"
#include "mysf_schedule.h"

//=========================== defines =========================================

//static char stringToPrint[]       = "05oos_rootTest\r\n";

//=========================== variables =======================================


typedef struct {
    bool        timerFired;
    bool        fInhibit;
    open_addr_t addr;
} app_vars_t;

app_vars_t app_vars;

//=========================== prototypes ======================================


//=========================== main ============================================

/**
\brief The program starts executing here.
in order to echo chunks of bytes, each chunk needs to start with character 'H' as
openserial takes different actions according to the initial character of the stream.
*/
int mote_main(void) {

    //memset(&app_vars,0,sizeof(app_vars_t));

    board_init();

    openserial_init();
    openqueue_init();
    mysf_init(NODE_ROOT);
    //while(1){
    openserial_printf("mote_main\n");
    //  some_delay();
    //}
    
    return 0;
}
void some_delay(void) {
   volatile uint16_t delay;
   for (delay=0xffff;delay>0;delay--);
}


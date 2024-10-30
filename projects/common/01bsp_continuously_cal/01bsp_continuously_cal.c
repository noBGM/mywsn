/**
\brief This program shows the use of the "radio" bsp module.

Since the bsp modules for different platforms have the same declaration, you
can use this project with any platform.

This application has two phases:

    1. first the motes keeps sending frames for 10 seconds,
    2. then mote goes to listen mode.
    
During listen mode, the mote replies with an ACK when a valid frame is received.

The frame sent by mote includes temperature data.
The ACK replied includes tempearture data and the frequency offset
corresponding to the incoming frame.

\author Tengfei Chang <tengfei.chang@inria.fr>, June 2020.
*/

#include "stdint.h"
#include "string.h"
#include "board.h"
#include "radio.h"
#include "leds.h"
#include "sctimer.h"
#include "sensors.h"

//=========================== defines =========================================

#define LENGTH_PACKET           3+LENGTH_CRC // maximum length is 127 bytes
#define MAX_PKT_LEN             125+LENGTH_CRC
#define CHANNEL                 11            // 24ghz: 11 = 2.405GHz, subghz: 11 = 865.325 in  FSK operating mode #1
#define BEACON_PERIOD           20             // in seconds
#define TICKS_IN_ONE_SECOND     32768         // (32768>>1) = 500ms @ 32kHz
#define TICKS_IN_ONE_MS         33            // (32768>>1) = 500ms @ 32kHz
#define RX_TIMEOUT              10            // 10         = 300us @ 32kHz
#define DELAY_TX                20            // 20         = 605us @ 32kHz

//=========================== variables =======================================

typedef enum {
    S_SEND_BEACON       = 0x00,
    S_LISTEN_PROBE      = 0x01,
    S_REPLY_ACK         = 0x02
} app_state_t;

typedef struct {
    uint8_t              time_in_second;
    uint8_t              txpk_txDone;
    uint8_t              txack_txDone;
    uint8_t              rxpk_rxDone;
    uint8_t              rx_timeout;
    uint8_t              txack_started;
    uint8_t              txpk_buf[LENGTH_PACKET];
    uint8_t              txpk_len;
    app_state_t          state;
    callbackRead_cbt     read_temperature;
    
    uint8_t              schedule_ack;
    uint8_t              ok_to_send_ack;
} app_vars_t;

app_vars_t app_vars;

//=========================== prototypes ======================================

void cb_scTimerCompare(void);
void cb_startFrame(PORT_TIMER_WIDTH timestamp);
void cb_endFrame(PORT_TIMER_WIDTH timestamp);
void prepare_frame(uint8_t data);

void delay(void);

//=========================== main ============================================

/**
\brief The program starts executing here.
*/
int mote_main(void) {
    
    uint8_t freq_offset;

    // clear local variables
    memset(&app_vars,0,sizeof(app_vars_t));

    // initialize board
    board_init();

    // add radio callback functions
    sctimer_set_callback(cb_scTimerCompare);
    radio_setStartFrameCb(cb_startFrame);
    radio_setEndFrameCb(cb_endFrame);

    // prepare radio
    radio_rfOn();
    // freq type only effects on scum port
    radio_setFrequency(CHANNEL, FREQ_TX);
    radio_rfOff();

    // start the one second timer
    app_vars.time_in_second = 0;
    sctimer_setCompare(sctimer_readCounter()+ TICKS_IN_ONE_SECOND);
    sctimer_enable();
    
    app_vars.read_temperature = sensors_getCallbackRead(SENSOR_TEMPERATURE);
    
    app_vars.txpk_txDone = 1;
    app_vars.rx_timeout  = 1;
    
    while(1) {
        
        switch(app_vars.state){
            case S_SEND_BEACON:
                        
                if (app_vars.txpk_txDone){
                    
                    if (app_vars.time_in_second == BEACON_PERIOD) {
                        
                        app_vars.state = S_LISTEN_PROBE;
                        break;
                    }
                    
                    app_vars.txpk_txDone = 0;
                    // led
                    leds_error_toggle();
                    
                    prepare_frame(app_vars.time_in_second);
                    radio_txNow();
                } else {
                    board_sleep();
                }
            break;
            case S_LISTEN_PROBE:
                if (
                    app_vars.rx_timeout   == 1 || 
                    app_vars.txack_txDone == 1
                ) {
                    
                    app_vars.txack_txDone = 0;
                    app_vars.rxpk_rxDone  = 0;
                    app_vars.rx_timeout   = 0;
                    
                    radio_rfOff();
                    radio_rxEnable();
                    radio_rxNow();
                    
                } else {
                    if (app_vars.rxpk_rxDone == 1) {
                        
                        app_vars.rxpk_rxDone    = 0;
                        app_vars.txack_txDone   = 0;
                        app_vars.state = S_REPLY_ACK;
                    } else {
                        board_sleep();
                    }
                }
            break;
            case S_REPLY_ACK:
                if (app_vars.txack_txDone == 1){
                    
                    app_vars.txack_started  = 0;
                    app_vars.state          = S_LISTEN_PROBE;
                    
                    sensors_getCallbackReset();
                } else {
                    
                    if (app_vars.txack_started == 1) {
                        board_sleep();
                    } else {
                        freq_offset = radio_getFrequencyOffset();
                        prepare_frame(freq_offset);
                        while(app_vars.ok_to_send_ack==0);
                        radio_txNow();
                        app_vars.txack_started = 1;
                    }
                }
            break;
        }
    }
}

//=========================== callbacks =======================================

void cb_scTimerCompare(void) {
    
    if (app_vars.state == S_SEND_BEACON) {
            
        app_vars.time_in_second++;
        sctimer_setCompare(sctimer_readCounter()+ TICKS_IN_ONE_SECOND);
    } else {
        if (
            app_vars.state == S_REPLY_ACK && 
            app_vars.schedule_ack == 1
        ) {
            app_vars.schedule_ack   = 0;
            app_vars.ok_to_send_ack = 1;
        } else {
            
            app_vars.rx_timeout     = 1;
        }        
    }
}

void cb_startFrame(PORT_TIMER_WIDTH timestamp) {

    // led
    leds_sync_on();
    
    if (app_vars.state == S_LISTEN_PROBE){
        
        // set a timeout for endFrame
        sctimer_setCompare(sctimer_readCounter()+ RX_TIMEOUT);
    }

}

void cb_endFrame(PORT_TIMER_WIDTH timestamp) {
    
    uint8_t packet[MAX_PKT_LEN];
    uint8_t pkt_len;
     int8_t rssi;
    uint8_t lqi;
       bool crc;

   if (app_vars.state == S_SEND_BEACON) {
       
       app_vars.txpk_txDone = 1;
   } else {
       
       if (app_vars.state == S_LISTEN_PROBE) {
           
            radio_getReceivedFrame(
                packet,
                &pkt_len,
                sizeof(packet),
                &rssi,
                &lqi,
                &crc
            );
            
            if (
                crc                     && 
                pkt_len==LENGTH_PACKET  && 
                packet[0]=='C'          &&
                packet[1]=='F'
            ) {
            
                sctimer_disable();
                app_vars.rxpk_rxDone = 1;
                
                app_vars.schedule_ack   = 1;
                app_vars.ok_to_send_ack = 0;
                sctimer_setCompare(timestamp+((uint16_t)packet[2])*TICKS_IN_ONE_MS-DELAY_TX);
            }
       } else {
           
           if (app_vars.state == S_REPLY_ACK) {
               
                app_vars.txack_txDone = 1;
           }
       }
        
   }

   // led
   leds_sync_off();
}

// ========================== private =========================================

void prepare_frame(uint8_t data) {
    
    uint16_t temperature;
    uint16_t read;
    float result;
    
    radio_rfOff();
    // prepare packet
    app_vars.txpk_len           = sizeof(app_vars.txpk_buf);
    
    // calculate the temperature only when replying ack
    if (app_vars.state == S_REPLY_ACK) {
        
        read                        = app_vars.read_temperature();
        result  = -46.85;
        result += 175.72 * read / 65536;
        temperature = (int16_t)(result);
        
        app_vars.txpk_buf[0]        = (uint8_t)((temperature & 0xff00)>>8);
        app_vars.txpk_buf[1]        = (uint8_t)(temperature & 0x00ff);
    }
    
    // app_vars.txpk_buf[0]        = (uint8_t)((temperature & 0xff00)>>8);
    // app_vars.txpk_buf[1]        = (uint8_t)(temperature & 0x00ff);
    app_vars.txpk_buf[2]        = data;
    
    // send packet
    radio_loadPacket(app_vars.txpk_buf,app_vars.txpk_len);
    radio_txEnable();
}

// 0x0cff indicates rougly 2.3ms running on OpenMote-B

void delay(void) {
    uint16_t i;
    for (i=0;i<0x70ff;i++);
}

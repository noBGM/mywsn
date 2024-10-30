/**
\brief Definition of the "openserial" driver.

\author Fabien Chraim <chraim@eecs.berkeley.edu>, March 2012.
\author Thomas Watteyne <thomas.watteyne@inria.fr>, August 2016.
*/
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "opendefs.h"
#include "openserial.h"
#include "scheduler.h"
#include "IEEE802154E.h"
#include "neighbors.h"
#include "sixtop.h"
#include "icmpv6echo.h"
#include "idmanager.h"
#include "openqueue.h"
#include "openbridge.h"
#include "leds.h"
#include "schedule.h"
#include "uart.h"
#include "opentimers.h"
#include "openhdlc.h"
#include "schedule.h"
#include "icmpv6rpl.h"
#include "icmpv6echo.h"
#include "msf.h"
#include "debugpins.h"

//=========================== variables =======================================

openserial_vars_t openserial_vars;

#define STATUSPRINT_PERIOD 100 // in ms

//=========================== prototypes ======================================


// printing
owerror_t internal_openserial_print(
        char severity,
        uint8_t calling_component,
        uint8_t error_code,
        errorparameter_t arg1,
        errorparameter_t arg2
);

// command handlers
void openserial_handleRxFrame(void);

void openserial_handleEcho(uint8_t *but, uint8_t bufLen);

// misc
void openserial_debugPrint_timer_cb(opentimers_id_t id);

void openserial_board_reset_cb(opentimers_id_t id);

// HDLC output
void outputHdlcOpen(void);

void outputHdlcWrite(uint8_t b);

void outputHdlcClose(void);

// HDLC input
void inputHdlcOpen(void);

void inputHdlcWrite(uint8_t b);

void inputHdlcClose(void);

// task
void task_printWrongCRCInput(void);

void task_printInputBufferOverflow(void);

//=========================== public ==========================================

//===== admin

void openserial_init(void) {
    // reset variable
    memset(&openserial_vars, 0, sizeof(openserial_vars_t));

    // admin
    openserial_vars.fInhibited = FALSE;
    openserial_vars.ctsStateChanged = FALSE;
    openserial_vars.debugPrintCounter = 0;

    // input
    openserial_vars.hdlcBusyReceiving = FALSE;
    openserial_vars.hdlcInputEscaping = FALSE;
    openserial_vars.inputBufFillLevel = 0;

    // ouput
    openserial_vars.outputBufIdxR = 0;
    openserial_vars.outputBufIdxW = 0;
    openserial_vars.fBusyFlushing = FALSE;

    openserial_vars.reset_timerId = opentimers_create(TIMER_GENERAL_PURPOSE, TASKPRIO_OPENSERIAL);
    openserial_vars.debugPrint_timerId = opentimers_create(TIMER_GENERAL_PURPOSE, TASKPRIO_OPENSERIAL);
    opentimers_scheduleIn(
            openserial_vars.debugPrint_timerId,
            STATUSPRINT_PERIOD,
            TIME_MS,
            TIMER_PERIODIC,
            openserial_debugPrint_timer_cb
    );

    // UART
    uart_setCallbacks(isr_openserial_tx, isr_openserial_rx);
    uart_enableInterrupts();
}

//===== transmitting

owerror_t openserial_printStatus(uint8_t statusElement, uint8_t *buffer, uint8_t length) {
    uint8_t i;

    outputHdlcOpen();
    outputHdlcWrite(SERFRAME_MOTE2PC_STATUS);
    outputHdlcWrite(idmanager_getMyID(ADDR_16B)->addr_16b[0]);
    outputHdlcWrite(idmanager_getMyID(ADDR_16B)->addr_16b[1]);
    outputHdlcWrite(statusElement);
    for (i = 0; i < length; i++) {
        outputHdlcWrite(buffer[i]);
    }
    outputHdlcClose();

    // start TX'ing
    openserial_flush();

    return E_SUCCESS;
}

owerror_t openserial_printLog(
        uint8_t log_level,
        uint8_t calling_component,
        uint8_t error_code,
        errorparameter_t arg1,
        errorparameter_t arg2
) {
    uint32_t reference;
    char severity;

    switch (log_level) {
        case L_VERBOSE:
            severity = SERFRAME_MOTE2PC_VERBOSE;
            break;
        case L_INFO:
            severity = SERFRAME_MOTE2PC_INFO;
            break;
        case L_WARNING:
            severity = SERFRAME_MOTE2PC_WARNING;
            break;
        case L_SUCCESS:
            severity = SERFRAME_MOTE2PC_SUCCESS;
            break;
        case L_ERROR:
            severity = SERFRAME_MOTE2PC_ERROR;
            break;
        case L_CRITICAL:
            severity = SERFRAME_MOTE2PC_CRITICAL;
            // blink error LED, this is serious
            leds_error_blink();

            // schedule for the mote to reboot in 10s
            reference = opentimers_getValue();
            opentimers_scheduleAbsolute(
                    openserial_vars.reset_timerId,  // timerId
                    10000,                          // duration
                    reference,                      // reference
                    TIME_MS,                        // timetype
                    openserial_board_reset_cb       // callback
            );
            break;
        default:
            // unknown logging level
            return E_FAIL;
    }

    return internal_openserial_print(severity, calling_component, error_code, arg1, arg2);
}

owerror_t openserial_printData(uint8_t *buffer, uint8_t length) {
    uint8_t i;
    uint8_t asn[5];

    // retrieve ASN
    ieee154e_getAsn(asn);

    outputHdlcOpen();
    outputHdlcWrite(SERFRAME_MOTE2PC_DATA);
    outputHdlcWrite(idmanager_getMyID(ADDR_16B)->addr_16b[0]);
    outputHdlcWrite(idmanager_getMyID(ADDR_16B)->addr_16b[1]);
    outputHdlcWrite(asn[0]);
    outputHdlcWrite(asn[1]);
    outputHdlcWrite(asn[2]);
    outputHdlcWrite(asn[3]);
    outputHdlcWrite(asn[4]);
    for (i = 0; i < length; i++) {
        outputHdlcWrite(buffer[i]);
    }
    outputHdlcClose();

    // start TX'ing
    openserial_flush();

    return E_SUCCESS;
}

owerror_t openserial_printSniffedPacket(uint8_t *buffer, uint8_t length, uint8_t channel) {
#if BOARD_OPENSERIAL_SNIFFER
    uint8_t i;

    outputHdlcOpen();
    outputHdlcWrite(SERFRAME_MOTE2PC_SNIFFED_PACKET);
    outputHdlcWrite(idmanager_getMyID(ADDR_16B)->addr_16b[0]);
    outputHdlcWrite(idmanager_getMyID(ADDR_16B)->addr_16b[1]);
    for (i = 0; i < length; i++) {
        outputHdlcWrite(buffer[i]);
    }
    outputHdlcWrite(channel);
    outputHdlcClose();

    // start TX'ing
    openserial_flush();

#endif
    return E_SUCCESS;
}

owerror_t openserial_printf(char *buffer, ...) {
#if BOARD_OPENSERIAL_PRINTF
    uint8_t  i;
    char *ptr, *tmp;
    char c;
    void* p;
    int d;
    char buf[16];
    char *fail = " - unknown format specifier - ";

    uint8_t  asn[5];

    va_list ap;
    va_start(ap, buffer);

    // retrieve ASN
    ieee154e_getAsn(asn);

    outputHdlcOpen();
    outputHdlcWrite(SERFRAME_MOTE2PC_PRINTF);
    outputHdlcWrite(idmanager_getMyID(ADDR_16B)->addr_16b[0]);
    outputHdlcWrite(idmanager_getMyID(ADDR_16B)->addr_16b[1]);

    for(i = 0; i < 5; i++) {
        outputHdlcWrite(asn[i]);
    }

    for (ptr = buffer; *ptr != '\0'; ptr++){
        if (*ptr == '%') {
              ptr++;
              switch (*ptr) {
                  case 'c':
                      c = va_arg(ap, int);
                      outputHdlcWrite(c);
                      break;
                  case 's':
                      tmp = va_arg(ap, char*);
                      while (*tmp != '\0'){
                          outputHdlcWrite(*tmp);
                          tmp++;
                      }
                      break;
                  case 'd':
                      d = va_arg(ap, int);
                      snprintf(buf, 16, "%d", d);
                      tmp = buf;
                      while (*tmp != '\0'){
                          outputHdlcWrite(*tmp);
                          tmp++;
                      }
                      break;
                  case 'x':
                      d = va_arg(ap, int);
                      snprintf(buf, 16, "%x", d);
                      tmp = buf;
                      while (*tmp != '\0'){
                          outputHdlcWrite(*tmp);
                          tmp++;
                      }
                      break;
                  case 'p':
                      p = va_arg(ap, void*);
                      snprintf(buf, 16, "%p", p);
                      tmp = buf;
                      while (*tmp != '\0'){
                          outputHdlcWrite(*tmp);
                          tmp++;
                      }
                      break;
                  case '%':
                      outputHdlcWrite('%');
                      break;
                  default:
                      for(tmp = fail; *tmp != '\0'; tmp++){
                          outputHdlcWrite(*tmp);
                      }
              }
        } else {
            outputHdlcWrite(*ptr);
        }
    }

    va_end(ap);

    outputHdlcClose();

    // start TX'ing
    openserial_flush();
#endif
    return E_SUCCESS;
}

//===== retrieving inputBuffer

void task_openserial_debugPrint(void) {
    uint8_t debugPrintCounter;
    INTERRUPT_DECLARATION();

    //<<<<<<<<<<<<<<<<<<<<<<<
    DISABLE_INTERRUPTS();
    debugPrintCounter = openserial_vars.debugPrintCounter;
    ENABLE_INTERRUPTS();
    //>>>>>>>>>>>>>>>>>>>>>>>

    if (openserial_vars.outputBufIdxW != openserial_vars.outputBufIdxR) {
        return;
    }

    debugPrintCounter++;
    if (debugPrintCounter == STATUS_MAX) {
        debugPrintCounter = 0;
    }

    switch (debugPrintCounter) {
        case STATUS_ISSYNC:
            if (debugPrint_isSync() == TRUE) {
                break;
            }
        case STATUS_ID:
            if (debugPrint_id() == TRUE) {
                break;
            }
        case STATUS_DAGRANK:
            if (debugPrint_myDAGrank() == TRUE) {
                break;
            }
        case STATUS_OUTBUFFERINDEXES:
            if (debugPrint_outBufferIndexes() == TRUE) {
                break;
            }
        case STATUS_ASN:
            if (debugPrint_asn() == TRUE) {
                break;
            }
        case STATUS_MACSTATS:
            if (debugPrint_macStats() == TRUE) {
                break;
            }
        case STATUS_SCHEDULE:
            if (debugPrint_schedule() == TRUE) {
                break;
            }
        case STATUS_BACKOFF:
            if (debugPrint_backoff() == TRUE) {
                break;
            }
        case STATUS_QUEUE:
            if (debugPrint_queue() == TRUE) {
                break;
            }
        case STATUS_NEIGHBORS:
            if (debugPrint_neighbors() == TRUE) {
                break;
            }
        case STATUS_KAPERIOD:
            if (debugPrint_kaPeriod() == TRUE) {
                break;
            }
        case STATUS_JOINED:
            if (debugPrint_joined() == TRUE) {
                break;
            }
        case STATUS_MSF:
            if (debugPrint_msf() == TRUE) {
                break;
            }
        default:
            debugPrintCounter = 0;
    }

    //<<<<<<<<<<<<<<<<<<<<<<<
    DISABLE_INTERRUPTS();
    openserial_vars.debugPrintCounter = debugPrintCounter;
    ENABLE_INTERRUPTS();
    //>>>>>>>>>>>>>>>>>>>>>>>
}

//===== receiving

uint8_t openserial_getInputBufferFillLevel() {
    uint8_t inputBufFillLevel;
    INTERRUPT_DECLARATION();

    //<<<<<<<<<<<<<<<<<<<<<<<
    DISABLE_INTERRUPTS();
    inputBufFillLevel = openserial_vars.inputBufFillLevel;
    ENABLE_INTERRUPTS();
    //>>>>>>>>>>>>>>>>>>>>>>>

    return inputBufFillLevel - 1; // removing the command byte
}

uint8_t openserial_getInputBuffer(uint8_t *bufferToWrite, uint8_t maxNumBytes) {
    uint8_t numBytesWritten;
    uint8_t inputBufFillLevel;
    INTERRUPT_DECLARATION();

    //<<<<<<<<<<<<<<<<<<<<<<<
    DISABLE_INTERRUPTS();
    inputBufFillLevel = openserial_vars.inputBufFillLevel;
    ENABLE_INTERRUPTS();
    //>>>>>>>>>>>>>>>>>>>>>>>

    if (maxNumBytes < inputBufFillLevel - 1) {
        LOG_ERROR(COMPONENT_OPENSERIAL, ERR_GETDATA_ASKS_TOO_FEW_BYTES,
                  (errorparameter_t) maxNumBytes,
                  (errorparameter_t) inputBufFillLevel - 1);
        numBytesWritten = 0;
    } else {
        numBytesWritten = inputBufFillLevel - 1;
        //<<<<<<<<<<<<<<<<<<<<<<<
        DISABLE_INTERRUPTS();
        memcpy(bufferToWrite, &(openserial_vars.inputBuf[1]), numBytesWritten);
        ENABLE_INTERRUPTS();
        //>>>>>>>>>>>>>>>>>>>>>>>
    }

    return numBytesWritten;
}

//===== scheduling

void openserial_flush(void) {
    INTERRUPT_DECLARATION();

    //<<<<<<<<<<<<<<<<<<<<<<<
    DISABLE_INTERRUPTS();
    if (openserial_vars.fBusyFlushing == FALSE) {
        if (openserial_vars.ctsStateChanged == TRUE) {
            // send CTS
#if BOARD_FASTSIM_ENABLED
#else
            if (openserial_vars.fInhibited == TRUE) {
                uart_setCTS(FALSE);
            } else {
                uart_setCTS(TRUE);
            }
#endif
            openserial_vars.ctsStateChanged = FALSE;
        } else {
            if (openserial_vars.fInhibited == TRUE) {
                // currently inhibited
            } else {
                // not inhibited
                if (openserial_vars.outputBufIdxW != openserial_vars.outputBufIdxR) {
                    // I have some bytes to transmit

#if BOARD_FASTSIM_ENABLED
                    uart_writeCircularBuffer_FASTSIM(
                        openserial_vars.outputBuf,
                        &openserial_vars.outputBufIdxR,
                        &openserial_vars.outputBufIdxW
                    );
#else
                    uart_writeByte(openserial_vars.outputBuf[OUTPUT_BUFFER_MASK & (openserial_vars.outputBufIdxR++)]);
                    openserial_vars.fBusyFlushing = TRUE;
#endif
                }
            }
        }
    }
    ENABLE_INTERRUPTS();
    //>>>>>>>>>>>>>>>>>>>>>>>
}

void openserial_inhibitStart(void) {
    // this function needs to run in non-interrupt mode
    // since the inhibitStart is always called in an interrupt mode,
    // DISABLE_INTERRUPT is not necessary here.

    openserial_vars.fInhibited = TRUE;
#if BOARD_FASTSIM_ENABLED
#else
    openserial_vars.ctsStateChanged = TRUE;
#endif

    // it's openserial_flush() which will set CTS
    openserial_flush();
}

void openserial_inhibitStop(void) {
    INTERRUPT_DECLARATION();

    //<<<<<<<<<<<<<<<<<<<<<<<
    DISABLE_INTERRUPTS();
    openserial_vars.fInhibited = FALSE;
#if BOARD_FASTSIM_ENABLED
#else
    openserial_vars.ctsStateChanged = TRUE;
#endif
    ENABLE_INTERRUPTS();
    //>>>>>>>>>>>>>>>>>>>>>>>

    // it's openserial_flush() which will set CTS
    openserial_flush();
}

//===== debugprint

/**
\brief Trigger this module to print status information, over serial.

debugPrint_* functions are used by the openserial module to continuously print
status information about several modules in the OpenWSN stack.

\returns TRUE if this function printed something, FALSE otherwise.
*/
bool debugPrint_outBufferIndexes(void) {
    uint16_t temp_buffer[2];
    INTERRUPT_DECLARATION();

    //<<<<<<<<<<<<<<<<<<<<<<<
    DISABLE_INTERRUPTS();
    temp_buffer[0] = openserial_vars.outputBufIdxW;
    temp_buffer[1] = openserial_vars.outputBufIdxR;
    ENABLE_INTERRUPTS();
    //>>>>>>>>>>>>>>>>>>>>>>>

    openserial_printStatus(
            STATUS_OUTBUFFERINDEXES,
            (uint8_t *) temp_buffer,
            sizeof(temp_buffer)
    );

    return TRUE;
}

//=========================== private =========================================

//===== printing

owerror_t internal_openserial_print(
        char severity,
        uint8_t calling_component,
        uint8_t error_code,
        errorparameter_t arg1,
        errorparameter_t arg2
) {

    outputHdlcOpen();
    outputHdlcWrite(severity);
    outputHdlcWrite(idmanager_getMyID(ADDR_16B)->addr_16b[0]);
    outputHdlcWrite(idmanager_getMyID(ADDR_16B)->addr_16b[1]);
    outputHdlcWrite(calling_component);
    outputHdlcWrite(error_code);
    outputHdlcWrite((uint8_t)((arg1 & 0xff00) >> 8));
    outputHdlcWrite((uint8_t)(arg1 & 0x00ff));
    outputHdlcWrite((uint8_t)((arg2 & 0xff00) >> 8));
    outputHdlcWrite((uint8_t)(arg2 & 0x00ff));
    outputHdlcClose();

    // start TX'ing
    openserial_flush();

    return E_SUCCESS;
}

//===== command handlers

// executed in ISR
void openserial_handleRxFrame() {
    uint8_t cmdByte;

    cmdByte = openserial_vars.inputBuf[0];
    // call hard-coded commands
    // FIXME: needs to be replaced by registered commands only
    switch (cmdByte) {
        case SERFRAME_PC2MOTE_SETROOT:
            idmanager_triggerAboutRoot();
            break;
        case SERFRAME_PC2MOTE_RESET:
            board_reset();
            break;
        case SERFRAME_PC2MOTE_DATA:
            openbridge_triggerData();
            break;
        case SERFRAME_PC2MOTE_TRIGGERSERIALECHO:
            openserial_handleEcho(&openserial_vars.inputBuf[1], openserial_vars.inputBufFillLevel - 1);
            break;
    }
}

void openserial_handleEcho(uint8_t *buf, uint8_t bufLen) {
    // echo back what you received
    openserial_printData(buf, bufLen);
}

//===== misc

void openserial_debugPrint_timer_cb(opentimers_id_t id) {
    // calling the task directly as the timer_cb function is executed in
    // task mode by opentimer already
    task_openserial_debugPrint();
}

void openserial_board_reset_cb(opentimers_id_t id) {
    (void) id;
    board_reset();
}

//===== hdlc (output)

/**
\brief Start an HDLC frame in the output buffer.
*/
port_INLINE void outputHdlcOpen(void) {
    INTERRUPT_DECLARATION();

    //<<<<<<<<<<<<<<<<<<<<<<<
    DISABLE_INTERRUPTS();

    // initialize the value of the CRC

    openserial_vars.hdlcOutputCrc = HDLC_CRCINIT;

    // write the opening HDLC flag
    openserial_vars.outputBuf[OUTPUT_BUFFER_MASK & (openserial_vars.outputBufIdxW++)] = HDLC_FLAG;

    ENABLE_INTERRUPTS();
    //>>>>>>>>>>>>>>>>>>>>>>>
}

/**
\brief Add a byte to the outgoing HDLC frame being built.
*/
port_INLINE void outputHdlcWrite(uint8_t b) {
    INTERRUPT_DECLARATION();

    //<<<<<<<<<<<<<<<<<<<<<<<
    DISABLE_INTERRUPTS();

    // iterate through CRC calculator
    openserial_vars.hdlcOutputCrc = crcIteration(openserial_vars.hdlcOutputCrc, b);

    // add byte to buffer
    if (b == HDLC_FLAG || b == HDLC_ESCAPE) {
        openserial_vars.outputBuf[OUTPUT_BUFFER_MASK & (openserial_vars.outputBufIdxW++)] = HDLC_ESCAPE;
        b = b ^ HDLC_ESCAPE_MASK;
    }
    openserial_vars.outputBuf[OUTPUT_BUFFER_MASK & (openserial_vars.outputBufIdxW++)] = b;

    ENABLE_INTERRUPTS();
    //>>>>>>>>>>>>>>>>>>>>>>>
}

/**
\brief Finalize the outgoing HDLC frame.
*/
port_INLINE void outputHdlcClose(void) {
    uint16_t finalCrc;
    INTERRUPT_DECLARATION();

    //<<<<<<<<<<<<<<<<<<<<<<<
    DISABLE_INTERRUPTS();

    // finalize the calculation of the CRC
    finalCrc = ~openserial_vars.hdlcOutputCrc;

    // write the CRC value
    outputHdlcWrite((finalCrc >> 0) & 0xff);
    outputHdlcWrite((finalCrc >> 8) & 0xff);

    // write the closing HDLC flag
    openserial_vars.outputBuf[OUTPUT_BUFFER_MASK & (openserial_vars.outputBufIdxW++)] = HDLC_FLAG;

    ENABLE_INTERRUPTS();
    //>>>>>>>>>>>>>>>>>>>>>>>
}

//===== hdlc (input)

/**
\brief Start an HDLC frame in the input buffer.
*/
port_INLINE void inputHdlcOpen(void) {
    // reset the input buffer index
    openserial_vars.inputBufFillLevel = 0;

    // initialize the value of the CRC
    openserial_vars.hdlcInputCrc = HDLC_CRCINIT;
}

/**
\brief Add a byte to the incoming HDLC frame.
*/
port_INLINE void inputHdlcWrite(uint8_t b) {
    if (b == HDLC_ESCAPE) {
        openserial_vars.hdlcInputEscaping = TRUE;
    } else {
        if (openserial_vars.hdlcInputEscaping == TRUE) {
            b = b ^ HDLC_ESCAPE_MASK;
            openserial_vars.hdlcInputEscaping = FALSE;
        }

        // add byte to input buffer
        openserial_vars.inputBuf[openserial_vars.inputBufFillLevel] = b;
        openserial_vars.inputBufFillLevel++;

        // iterate through CRC calculator
        openserial_vars.hdlcInputCrc = crcIteration(openserial_vars.hdlcInputCrc, b);
    }
}

/**
\brief Finalize the incoming HDLC frame.
*/
port_INLINE void inputHdlcClose(void) {

    // verify the validity of the frame
    if (openserial_vars.hdlcInputCrc == HDLC_CRCGOOD) {
        // the CRC is correct

        // remove the CRC from the input buffer
        openserial_vars.inputBufFillLevel -= 2;
    } else {
        // the CRC is incorrect

        // drop the incoming frame
        openserial_vars.inputBufFillLevel = 0;
    }
}

//=========================== task ============================================

void task_printInputBufferOverflow(void) {
    // input buffer overflow
    LOG_ERROR(COMPONENT_OPENSERIAL, ERR_BUFFER_OVERFLOW, (errorparameter_t) 0, (errorparameter_t) 0);
}

void task_printWrongCRCInput(void) {
    // invalid HDLC frame
    LOG_ERROR(COMPONENT_OPENSERIAL, ERR_WRONG_CRC_INPUT, (errorparameter_t) 0, (errorparameter_t) 0);
}

//=========================== interrupt handlers ==============================

// executed in ISR, called from scheduler.c
void isr_openserial_tx(void) {
    if (openserial_vars.ctsStateChanged == TRUE) {
        // set CTS

        if (openserial_vars.fInhibited == TRUE) {
            uart_setCTS(FALSE);
            openserial_vars.fBusyFlushing = FALSE;
        } else {
            uart_setCTS(TRUE);
        }
        openserial_vars.ctsStateChanged = FALSE;
    } else if (openserial_vars.fInhibited == TRUE) {
        // currently inhibited

        openserial_vars.fBusyFlushing = FALSE;
    } else {
        // not inhibited

        if (openserial_vars.outputBufIdxW != openserial_vars.outputBufIdxR) {
            // I have some bytes to transmit

            uart_writeByte(openserial_vars.outputBuf[OUTPUT_BUFFER_MASK & (openserial_vars.outputBufIdxR++)]);
            openserial_vars.fBusyFlushing = TRUE;
        } else {
            // I'm done sending bytes

            openserial_vars.fBusyFlushing = FALSE;
        }
    }
}

/**
\pre executed in ISR, called from scheduler.c

\returns 1 if don't receiving frame, 0 if not
*/

uint8_t isr_openserial_rx(void) {
    uint8_t rxbyte;
    uint8_t returnVal;

    returnVal = 0;

    // read byte just received
    rxbyte = uart_readByte();

    if (
            openserial_vars.hdlcBusyReceiving == FALSE &&
            openserial_vars.hdlcLastRxByte == HDLC_FLAG &&
            rxbyte != HDLC_FLAG
            ) {
        // start of frame

        // I'm now receiving
        openserial_vars.hdlcBusyReceiving = TRUE;

        // create the HDLC frame
        inputHdlcOpen();

        // add the byte just received
        inputHdlcWrite(rxbyte);
    } else if (
            openserial_vars.hdlcBusyReceiving == TRUE &&
            rxbyte != HDLC_FLAG
            ) {
        // middle of frame

        // add the byte just received
        inputHdlcWrite(rxbyte);
        if (openserial_vars.inputBufFillLevel + 1 > SERIAL_INPUT_BUFFER_SIZE) {
            // push task
            scheduler_push_task(task_printInputBufferOverflow, TASKPRIO_OPENSERIAL);
            openserial_vars.inputBufFillLevel = 0;
            openserial_vars.hdlcBusyReceiving = FALSE;
        }
    } else if (
            openserial_vars.hdlcBusyReceiving == TRUE &&
            rxbyte == HDLC_FLAG
            ) {
        // end of frame

        // finalize the HDLC frame
        inputHdlcClose();
        openserial_vars.hdlcBusyReceiving = FALSE;

        if (openserial_vars.inputBufFillLevel == 0) {
            // push task
            scheduler_push_task(task_printWrongCRCInput, TASKPRIO_OPENSERIAL);
        } else {
            openserial_handleRxFrame();
            openserial_vars.inputBufFillLevel = 0;
            returnVal = 1;
        }
    }

    openserial_vars.hdlcLastRxByte = rxbyte;

    return returnVal;
}

#ifndef __UEXPIRATION_H
#define __UEXPIRATION_H

/**
\addtogroup AppUdp
\{
\addtogroup uexpiration
\{
*/

#include "config.h"
#include "opentimers.h"
#include "udp.h"

//=========================== define ==========================================
//=========================== typedef =========================================

//=========================== variables =======================================

typedef struct {
    opentimers_id_t timerId;  ///< periodic timer which triggers transmission
    uint16_t period;  ///< uinject packet sending period>
} uexpiration_vars_t;

//=========================== prototypes ======================================

void uexpiration_init(void);

void uexpiration_receive(OpenQueueEntry_t *msg);

void uexpiration_sendDone(OpenQueueEntry_t *msg, owerror_t error);

/**
\}
\}
*/

#endif

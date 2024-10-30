#include "config.h"

#include "opendefs.h"
#include "icmpv6.h"
#include "icmpv6echo.h"
#include "icmpv6rpl.h"
#include "forwarding.h"
#include "openqueue.h"
#include "openserial.h"

//=========================== variables =======================================

//=========================== prototypes ======================================

//=========================== public ==========================================

void icmpv6_init(void) {
}

owerror_t icmpv6_send(OpenQueueEntry_t *msg) {
    msg->owner = COMPONENT_ICMPv6;
    msg->l4_protocol = IANA_ICMPv6;
    return forwarding_send(msg);
}

void icmpv6_sendDone(OpenQueueEntry_t *msg, owerror_t error) {
    msg->owner = COMPONENT_ICMPv6;
    switch (msg->l4_sourcePortORicmpv6Type) {
#if OPENWSN_ICMPV6ECHO_C
        case IANA_ICMPv6_ECHO_REQUEST:
        case IANA_ICMPv6_ECHO_REPLY:
            icmpv6echo_sendDone(msg, error);
            break;
#endif
        case IANA_ICMPv6_RPL:
            icmpv6rpl_sendDone(msg, error);
            break;
        default:
            // free the corresponding packet buffer
            LOG_CRITICAL(COMPONENT_ICMPv6, ERR_UNSUPPORTED_ICMPV6_TYPE,
                         (errorparameter_t) msg->l4_sourcePortORicmpv6Type,
                         (errorparameter_t) 0);
            openqueue_freePacketBuffer(msg);
            break;
    }
}

void icmpv6_receive(OpenQueueEntry_t *msg) {
    msg->owner = COMPONENT_ICMPv6;
    msg->l4_sourcePortORicmpv6Type = ((ICMPv6_ht *) (msg->payload))->type;
    switch (msg->l4_sourcePortORicmpv6Type) {
#if OPENWSN_ICMPV6ECHO_C
        case IANA_ICMPv6_ECHO_REQUEST:
        case IANA_ICMPv6_ECHO_REPLY:
            icmpv6echo_receive(msg);
            break;
#endif
        case IANA_ICMPv6_RPL:
            icmpv6rpl_receive(msg);
            break;
        default:
            LOG_WARNING(COMPONENT_ICMPv6, ERR_UNSUPPORTED_ICMPV6_TYPE,
                        (errorparameter_t) msg->l4_sourcePortORicmpv6Type,
                        (errorparameter_t) 1);
            // free the corresponding packet buffer
            openqueue_freePacketBuffer(msg);
            break;
    }
}

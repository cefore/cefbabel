/*
 * Copyright (c) 2016-2025, National Institute of Information and Communications
 * Technology (NICT). All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the NICT nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NICT AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE NICT OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
 
/*
Copyright (c) 2007, 2008 by Juliusz Chroboczek

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "babeld.h"
#include "util.h"
#include "net.h"
#include "interface.h"
#include "source.h"
#include "neighbour.h"
#include "route.h"
#include "kernel.h"
#include "xroute.h"
#include "resend.h"
#include "message.h"
#include "configuration.h"

unsigned char packet_header[4] = {42, 2};

int split_horizon = 1;

unsigned short myseqno = 0;
struct timeval seqno_time = {0, 0};

extern const unsigned char v4prefix[16];

#define MAX_CHANNEL_HOPS 20

/* Parse a network prefix, encoded in the somewhat baroque compressed
   representation used by Babel.  Return the number of bytes parsed. */
static int
network_prefix(int ae, int plen, unsigned int omitted,
               const unsigned char *p, const unsigned char *dp,
               unsigned int len, unsigned char *p_r)
{
    unsigned pb;
    unsigned char prefix[16];
    int ret = -1;

    if(plen >= 0)
        pb = (plen + 7) / 8;
    else if(ae == 1)
        pb = 4;
    else
        pb = 16;

    if(pb > 16)
        return -1;

    memset(prefix, 0, 16);

    switch(ae) {
    case 0:
        ret = 0;
        break;
    case 1:
        if(omitted > 4 || pb > 4 || (pb > omitted && len < pb - omitted))
            return -1;
        memcpy(prefix, v4prefix, 12);
        if(omitted) {
            if(dp == NULL || !v4mapped(dp)) return -1;
            memcpy(prefix, dp, 12 + omitted);
        }
        if(pb > omitted) memcpy(prefix + 12 + omitted, p, pb - omitted);
        ret = pb - omitted;
        break;
    case 2:
        if(omitted > 16 || (pb > omitted && len < pb - omitted)) return -1;
        if(omitted) {
            if(dp == NULL || v4mapped(dp)) return -1;
            memcpy(prefix, dp, omitted);
        }
        if(pb > omitted) memcpy(prefix + omitted, p, pb - omitted);
        ret = pb - omitted;
        break;
    case 3:
        if(pb > 8 && len < pb - 8) return -1;
        prefix[0] = 0xfe;
        prefix[1] = 0x80;
        if(pb > 8) memcpy(prefix + 8, p, pb - 8);
        ret = pb - 8;
        break;
    default:
        return -1;
    }

    normalize_prefix(p_r, prefix, plen < 0 ? 128 : ae == 1 ? plen + 96 : plen);
    return ret;
}
#ifndef BABELD_CODE //+++++ ADD +++++
/* Parse a cefore prefix, encoded in the somewhat baroque compressed
   representation used by Babel.  Return the number of bytes parsed. */
static int
cefore_prefix(int plen, const unsigned char *p, unsigned char *p_r)
{
    if (plen == 0) {
        return (-1);
    }
    memcpy (p_r, p, plen);
    
    return plen;
}
#endif //----- ADD -----
#ifdef BABELD_CODE //+++++ DEL +++++
static int
parse_update_subtlv(struct interface *ifp, int metric, int ae,
                    const unsigned char *a, int alen,
                    unsigned char *channels, int *channels_len_return,
                    unsigned char *src_prefix, unsigned char *src_plen)
{
    int type, len, i = 0;
    int channels_len;

    /* This will be overwritten if there's a DIVERSITY_HOPS sub-TLV. */
    if(*channels_len_return < 1 || (ifp->flags & IF_FARAWAY)) {
        channels_len = 0;
    } else {
        if(metric < 256) {
            /* Assume non-interfering (wired) link. */
            channels_len = 0;
        } else {
            /* Assume interfering. */
            channels[0] = IF_CHANNEL_INTERFERING;
            channels_len = 1;
        }
    }

    *src_plen = 0;

    while(i < alen) {
        type = a[i];
        if(type == SUBTLV_PAD1) {
            i++;
            continue;
        }

        if(i + 2 > alen)
            goto fail;
        len = a[i + 1];
        if(i + len + 2 > alen)
            goto fail;

        if(type == SUBTLV_PADN) {
            /* Nothing. */
        } else if(type == SUBTLV_DIVERSITY) {
            memcpy(channels, a + i + 2, MIN(len, *channels_len_return));
            channels_len = MIN(len, *channels_len_return);
        } else if(type == SUBTLV_SOURCE_PREFIX) {
            int rc;
            if(len < 1)
                goto fail;
            if(a[i + 2] == 0)   /* source prefix cannot be default */
                goto fail;
            if(*src_plen != 0)  /* source prefix can only be specified once */
                goto fail;
            *src_plen = a[i + 2];
            rc = network_prefix(ae, *src_plen, 0, a + i + 3, NULL,
                                len - 1, src_prefix);
            if(rc < 0)
                goto fail;
            if(ae == 1)
                (*src_plen) += 96;
        } else {
            debugf("Received unknown%s Update sub-TLV %d.\n",
                   (type & 0x80) != 0 ? " mandatory" : "", type);
            if((type & 0x80) != 0)
                return -1;
        }

        i += len + 2;
    }
    *channels_len_return = channels_len;
    return 1;

 fail:
    fprintf(stderr, "Received truncated sub-TLV on Update.\n");
    return -1;
}
#endif //----- DEL -----

static int
parse_hello_subtlv(const unsigned char *a, int alen,
                   unsigned int *timestamp_return, int *have_timestamp_return)
{
    int type, len, i = 0, have_timestamp = 0;
    unsigned int timestamp = 0;

    while(i < alen) {
        type = a[0];
        if(type == SUBTLV_PAD1) {
            i++;
            continue;
        }

        if(i + 2 > alen) {
            fprintf(stderr, "Received truncated sub-TLV on Hello.\n");
            return -1;
        }
        len = a[i + 1];
        if(i + len + 2 > alen) {
            fprintf(stderr, "Received truncated sub-TLV on Hello.\n");
            return -1;
        }

        if(type == SUBTLV_PADN) {
            /* Nothing to do. */
        } else if(type == SUBTLV_TIMESTAMP) {
            if(len >= 4) {
                DO_NTOHL(timestamp, a + i + 2);
                have_timestamp = 1;
            } else {
                fprintf(stderr,
                        "Received incorrect RTT sub-TLV on Hello.\n");
                /* But don't break. */
            }
        } else {
            debugf("Received unknown%s Hello sub-TLV %d.\n",
                   (type & 0x80) != 0 ? " mandatory" : "", type);
            if((type & 0x80) != 0)
                return -1;
        }

        i += len + 2;
    }
    if(have_timestamp && timestamp_return)
        *timestamp_return = timestamp;
    if(have_timestamp_return)
        *have_timestamp_return = have_timestamp;
    return 1;
}

static int
parse_ihu_subtlv(const unsigned char *a, int alen,
                 unsigned int *timestamp1_return,
                 unsigned int *timestamp2_return,
                 int *have_timestamp_return)
{
    int type, len, i = 0;
    int have_timestamp = 0;
    unsigned int timestamp1, timestamp2;

    while(i < alen) {
        type = a[0];
        if(type == SUBTLV_PAD1) {
            i++;
            continue;
        }

        if(i + 2 > alen) {
            fprintf(stderr, "Received truncated sub-TLV on IHU.\n");
            return -1;
        }

        len = a[i + 1];
        if(i + len + 2 > alen) {
            fprintf(stderr, "Received truncated sub-TLV on IHU.\n");
            return -1;
        }

        if(type == SUBTLV_PADN) {
            /* Nothing to do. */
        } else if(type == SUBTLV_TIMESTAMP) {
            if(len >= 8) {
                DO_NTOHL(timestamp1, a + i + 2);
                DO_NTOHL(timestamp2, a + i + 6);
                have_timestamp = 1;
            } else {
                fprintf(stderr,
                        "Received incorrect RTT sub-TLV on IHU.\n");
                /* But don't break. */
            }
        } else {
            debugf("Received unknown%s IHU sub-TLV %d.\n",
                   (type & 0x80) != 0 ? " mandatory" : "", type);
            if((type & 0x80) != 0)
                return -1;
        }

        i += len + 2;
    }
    if(have_timestamp && timestamp1_return && timestamp2_return) {
        *timestamp1_return = timestamp1;
        *timestamp2_return = timestamp2;
    }
    if(have_timestamp_return) {
        *have_timestamp_return = have_timestamp;
    }
    return 1;
}

#ifdef BABELD_CODE //+++++ DEL +++++
static int
parse_request_subtlv(int ae, const unsigned char *a, int alen,
                     unsigned char *src_prefix, unsigned char *src_plen)
{
    int type, len, i = 0;

    *src_plen = 0;

    while(i < alen) {
        type = a[0];
        if(type == SUBTLV_PAD1) {
            i++;
            continue;
        }

        if(i + 2 > alen)
            goto fail;

        len = a[i + 1];
        if(i + 2 + len > alen)
            goto fail;

        if(type == SUBTLV_PADN) {
            /* Nothing to do. */
        } else if(type == SUBTLV_SOURCE_PREFIX) {
            int rc;
            if(len < 1)
                goto fail;
            if(a[i + 2] == 0)
                goto fail;
            if(*src_plen != 0)
                goto fail;
            *src_plen = a[i + 2];
            rc = network_prefix(ae, *src_plen, 0, a + i + 3, NULL,
                                len - 1, src_prefix);
            if(rc < 0)
                goto fail;
            if(ae == 1)
                (*src_plen) += 96;
        } else {
            debugf("Received unknown%s Route Request sub-TLV %d.\n",
                   ((type & 0x80) != 0) ? " mandatory" : "", type);
            if((type & 0x80) != 0)
                return -1;
        }

        i += len + 2;
    }
    return 1;

 fail:
    fprintf(stderr, "Received truncated sub-TLV on Route Request.\n");
    return -1;
}
#endif //----- DEL -----

#ifdef BABELD_CODE //+++++ DEL +++++
static int
parse_seqno_request_subtlv(int ae, const unsigned char *a, int alen,
                           unsigned char *src_prefix, unsigned char *src_plen)
{
    int type, len, i = 0;

    while(i < alen) {
        type = a[0];
        if(type == SUBTLV_PAD1) {
            i++;
            continue;
        }

        if(i + 2 > alen)
            goto fail;
        len = a[i + 1];
        if(i + len + 2 > alen)
            goto fail;

        if(type == SUBTLV_PADN) {
            /* Nothing to do. */
        } else if(type == SUBTLV_SOURCE_PREFIX) {
            int rc;
            if(len < 1)
                goto fail;
            *src_plen = a[i + 2];
            rc = network_prefix(ae, *src_plen, 0, a + i + 3, NULL,
                                len - 1, src_prefix);
            if(rc < 0)
                goto fail;
            if(ae == 1)
                (*src_plen) += 96;
        } else {
            debugf("Received unknown%s Route Request sub-TLV %d.\n",
                   ((type & 0x80) != 0) ? " mandatory" : "", type);
            if((type & 0x80) != 0)
                return -1;
        }

        i += len + 2;
    }
    return 1;
 fail:
    fprintf(stderr, "Received truncated sub-TLV on Route Request.\n");
    return -1;
}
#endif //----- DEL -----

#ifdef BABELD_CODE //+++++ DEL +++++
static int
parse_other_subtlv(const unsigned char *a, int alen)
{
    int type, len, i = 0;

    while(i < alen) {
        type = a[0];
        if(type == SUBTLV_PAD1) {
            i++;
            continue;
        }

        if(i + 2 > alen)
            goto fail;
        len = a[i + 1];
        if(i + 2 + len > alen)
            goto fail;

        if((type & 0x80) != 0) {
            debugf("Received unknown mandatory sub-TLV %d.\n", type);
            return -1;
        }

        i += len + 2;
    }
    return 1;
 fail:
    fprintf(stderr, "Received truncated sub-TLV.\n");
    return -1;
}
#endif //----- DEL -----

static int
network_address(int ae, const unsigned char *a, unsigned int len,
                unsigned char *a_r)
{
    return network_prefix(ae, -1, 0, a, NULL, len, a_r);
}

void
parse_packet(const unsigned char *from, struct interface *ifp,
             const unsigned char *packet, int packetlen)
{
    int i;
    const unsigned char *message;
#ifdef BABELD_CODE //+++++ REPLACE +++++
    unsigned char type, len;
#else // CEFBABELD
    unsigned char type;
#endif //----- REPLACE -----
    int bodylen;
    struct neighbour *neigh;
#ifdef BABELD_CODE //+++++ REPLACE +++++
    int have_router_id = 0, have_v4_prefix = 0, have_v6_prefix = 0,
        have_v4_nh = 0, have_v6_nh = 0;
    unsigned char router_id[8], v4_prefix[16], v6_prefix[16],
        v4_nh[16], v6_nh[16];
#else // CEFBABELD
    unsigned char router_id[8], v4_nh[16], v6_nh[16];
    int have_v4_nh = 0, have_v6_nh = 0;
    unsigned short nh_port = 0;
#endif //----- REPLACE -----
    int have_hello_rtt = 0;
    /* Content of the RTT sub-TLV on IHU messages. */
    unsigned int hello_send_us = 0, hello_rtt_receive_time = 0;
#ifndef BABELD_CODE //+++++ ADD +++++
    uint16_t    length, plength;
#endif //----- ADD -----

    if((ifp->flags & IF_TIMESTAMPS) != 0) {
        /* We want to track exactly when we received this packet. */
        gettime(&now);
    }

    if(!linklocal(from)) {
        fprintf(stderr, "Received packet from non-local address %s.\n",
                format_address(from));
        return;
    }

    if(packet[0] != 42) {
        fprintf(stderr, "Received malformed packet on %s from %s.\n",
                ifp->name, format_address(from));
        return;
    }

    if(packet[1] != 2) {
        fprintf(stderr,
                "Received packet with unknown version %d on %s from %s.\n",
                packet[1], ifp->name, format_address(from));
        return;
    }

    DO_NTOHS(bodylen, packet + 2);

    if(bodylen + 4 > packetlen) {
        fprintf(stderr, "Received truncated packet (%d + 4 > %d).\n",
                bodylen, packetlen);
        bodylen = packetlen - 4;
    }

    neigh = find_neighbour(from, ifp);
    if(neigh == NULL) {
        fprintf(stderr, "Couldn't allocate neighbour.\n");
        return;
    }

    i = 0;
    while(i < bodylen) {
        message = packet + 4 + i;
        type = message[0];
        if(type == MESSAGE_PAD1) {
            debugf("Received pad1 from %s on %s.\n",
                   format_address(from), ifp->name);
            i++;
            continue;
        }
        if(i + 2 > bodylen) {
            fprintf(stderr, "Received truncated message.\n");
            break;
        }
#ifdef BABELD_CODE //+++++ REPLACE +++++
        len = message[1];
        if(i + len + 2 > bodylen) {
#else // CEFBABELD
        DO_NTOHS(length, message + 1);
        if(i + length + 2  > bodylen) {
#endif //----- REPLACE -----
            fprintf(stderr, "Received truncated message.\n");
            break;
        }

        if(type == MESSAGE_PADN) {
#ifdef BABELD_CODE //+++++ REPLACE +++++
            debugf("Received pad%d from %s on %s.\n",
                   len, format_address(from), ifp->name);
#else // CEFBABELD
            debugf("Received pad%d from %s on %s.\n",
                   length, format_address(from), ifp->name);
#endif //----- REPLACE -----
        } else if(type == MESSAGE_ACK_REQ) {
#ifndef BABELD_CODE //+++++ ADD +++++
            /*
                0                   1                   2                   3
                0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |    Type = 2   |            Length             |    Rserved    |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |            Nonce              |          Interval             |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            */
#endif //----- ADD -----
            unsigned short nonce, interval;
            
#ifdef BABELD_CODE //+++++ DEL +++++
            int rc;
#endif //----- DEL -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
            if(len < 6) goto fail;
#else // CEFBABELD
            if(length < 6) {
                goto fail;
            }
#endif //----- REPLACE -----
            DO_NTOHS(nonce, message + 4);
            DO_NTOHS(interval, message + 6);
            debugf("Received ack-req (%04X %d) from %s on %s.\n",
                   nonce, interval, format_address(from), ifp->name);
#ifdef BABELD_CODE //+++++ DEL +++++
            rc = parse_other_subtlv(message + 8, len - 6);
            if(rc < 0)
                goto done;
#endif //----- DEL -----
            send_ack(neigh, nonce, interval);
        } else if(type == MESSAGE_ACK) {
#ifdef BABELD_CODE //+++++ DEL +++++
            int rc;
#endif //----- DEL -----
#ifndef BABELD_CODE //+++++ ADD +++++
            /*
                0                   1                   2                   3
                0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |    Type = 3   |            Length             |    Rserved    |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |            Nonce              |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            */
#endif
            debugf("Received ack from %s on %s.\n",
                   format_address(from), ifp->name);
#ifdef BABELD_CODE //+++++ DEL +++++
            rc = parse_other_subtlv(message + 4, len - 2);
            if(rc < 0)
                goto done;
#endif //----- DEL -----
            /* Nothing right now */
        } else if(type == MESSAGE_HELLO) {
#ifndef BABELD_CODE //+++++ ADD +++++
            /*
                0                   1                   2                   3
                0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |    Type = 4   |            Length             |    Rserved    |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |            Seqno              |          Interval             |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            */
#endif
            unsigned short seqno, interval;
#ifdef BABELD_CODE //+++++ REPLACE +++++
            int unicast, changed, have_timestamp, rc;
#else // CEFBABELD
            int unicast, changed, have_timestamp;
#endif //----- REPLACE -----
            unsigned int timestamp;
#ifdef BABELD_CODE //+++++ REPLACE +++++
            if(len < 6) goto fail;
#else // CEFBABELD
#ifdef DEB_UPDATE   //+++++ for DEB +++++
fprintf(stderr, ">>>>> Received HELLO\n");
#endif              //----- for DEB -----
            if(length < 5) {
                goto fail;
            }
#endif //----- REPLACE -----
            unicast = !!(message[2] & 0x80);
            DO_NTOHS(seqno, message + 4);
            DO_NTOHS(interval, message + 6);
            debugf("Received hello %d (%d) from %s on %s.\n",
                   seqno, interval,
                   format_address(from), ifp->name);
#ifdef BABELD_CODE //+++++ DEL +++++
            /* Sub-TLV handling. */
            rc = parse_hello_subtlv(message + 8, len - 6,
                                    &timestamp, &have_timestamp);
            if(rc < 0)
                goto done;
#endif //----- DEL -----
            changed =
                update_neighbour(neigh,
                                 unicast ? &neigh->uhello : &neigh->hello,
                                 unicast, seqno, interval);
            update_neighbour_metric(neigh, changed);
            if(interval > 0)
                /* Multiply by 3/2 to allow hellos to expire. */
                schedule_neighbours_check(interval * 15, 0);
#ifdef BABELD_CODE //+++++ REPLACE +++++
            if(have_timestamp) {
                neigh->hello_send_us = timestamp;
                neigh->hello_rtt_receive_time = now;
                have_hello_rtt = 1;
            }
#else // CEFBABELD
            /* Sub-TLV handling. */
            if(length > 8) {
                int rc;
                rc = parse_hello_subtlv(message + 8, length - 5,
                                        &timestamp, &have_timestamp);
                if(rc >= 0 && have_timestamp) {
                    neigh->hello_send_us = timestamp;
                    neigh->hello_rtt_receive_time = now;
                    have_hello_rtt = 1;
                }
            }
#endif //----- REPLACE -----
        } else if(type == MESSAGE_IHU) {
#ifndef BABELD_CODE //+++++ ADD +++++
            /*
                0                   1                   2                   3
                0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |    Type = 5   |            Length             |       AE      |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |            Rxcost             |          Interval             |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |       Address...
                +-+-+-+-+-+-+-+-+-+-+-+-
            */
#endif //----- ADD -----
            unsigned short txcost, interval;
            unsigned char address[16];
            int rc;
#ifdef BABELD_CODE //+++++ REPLACE +++++
            if(len < 6) goto fail;
#else // CEFBABELD
            if(length < 5) {
                goto fail;
            }
#endif //----- REPLACE -----
            DO_NTOHS(txcost, message + 4);
            DO_NTOHS(interval, message + 6);
#ifdef BABELD_CODE //+++++ REPLACE +++++
            rc = network_address(message[2], message + 8, len - 6, address);
#else // CEFBABELD
            rc = network_address(message[3], message + 8, length - 5, address);
#endif //----- REPLACE -----
            if(rc < 0) goto fail;
            debugf("Received ihu %d (%d) from %s on %s for %s.\n",
                   txcost, interval,
                   format_address(from), ifp->name,
                   format_address(address));
#ifdef BABELD_CODE //+++++ REPLACE +++++
            if(message[2] == 0 || interface_ll_address(ifp, address)) {
                int changed;
                rc = parse_ihu_subtlv(message + 8 + rc, len - 6 - rc,
                                      &hello_send_us, &hello_rtt_receive_time,
                                      NULL);
                if(rc < 0)
                    goto done;
                changed = txcost != neigh->txcost;
                neigh->txcost = txcost;
                neigh->ihu_time = now;
                neigh->ihu_interval = interval;
                update_neighbour_metric(neigh, changed);
                if(interval > 0)
                    /* Multiply by 3/2 to allow neighbours to expire. */
                    schedule_neighbours_check(interval * 45, 0);
            }
#else // CEFBABELD
            if(message[3] == 0 || interface_ll_address(ifp, address)) {
                /* RTT sub-TLV. */
                int changed;
                rc = parse_ihu_subtlv(message + 8 + rc, length - 5 - rc,
                                     &hello_send_us, &hello_rtt_receive_time,
                                     NULL);
                if(rc < 0)
                     goto done;
                changed = txcost != neigh->txcost;
                neigh->txcost = txcost;
                neigh->ihu_time = now;
                neigh->ihu_interval = interval;
                update_neighbour_metric(neigh, changed);
                if(interval > 0)
                        /* Multiply by 3/2 to allow neighbours to expire. */
                        schedule_neighbours_check(interval * 45, 0);
            }
#endif //----- REPLACE -----
        } else if(type == MESSAGE_ROUTER_ID) {
#ifdef BABELD_CODE //+++++ DEL +++++
            int rc;
#endif //----- DEL -----
#ifndef BABELD_CODE //+++++ ADD +++++
            /*
                0                   1                   2                   3
                0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |    Type = 6   |            Length             |    Rserved    |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |                                                               |
                +                           Router-Id                           +
                |                                                               |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            */
#endif //----- ADD -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
            if(len < 10) {
                have_router_id = 0;
                goto fail;
            }
#else // CEFBABELD
            if(length < 9) {
                goto fail;
            }
#endif //----- REPLACE -----
            memcpy(router_id, message + 4, 8);
#ifdef BABELD_CODE //+++++ DEL +++++
            have_router_id = 1;
#endif //----- DEL -----
            debugf("Received router-id %s from %s on %s.\n",
                   format_eui64(router_id), format_address(from), ifp->name);
#ifdef BABELD_CODE //+++++ DEL +++++
            rc = parse_other_subtlv(message + 12, len - 10);
            if(rc < 0)
                goto done;
#endif //----- DEL -----
#ifdef DEB_UPDATE   //+++++ for DEB +++++
fprintf(stderr, ">>>>> Received ROUTER_ID (%s) \n", format_eui64(router_id));
#endif              //----- for  DEB -----
        } else if(type == MESSAGE_NH) {
#ifndef BABELD_CODE //+++++ ADD +++++
            /*
                0                   1                   2                   3
                0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |    Type = 7   |            Length             |       AE      |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |              Port             |     Next hop...
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
            */
#endif //----- ADD -----
            unsigned char nh[16];
            int rc;
#ifdef BABELD_CODE //+++++ REPLACE +++++
            if(len < 2) {
#else // CEFBABELD
            if(length < 4) {
#endif //----- REPLACE -----

                have_v4_nh = 0;
                have_v6_nh = 0;
                goto fail;
            }
#ifdef BABELD_CODE //+++++ REPLACE +++++
            rc = network_address(message[2], message + 4, len - 2,
                                 nh);
#else // CEFBABELD
            DO_NTOHS(nh_port, message + 4);
            if (nh_port < 1025) {
                goto fail;
            }
            
            if(message[3] == 1){
                rc = network_address(message[3], message + 6, length - 3, 
                                     nh);
            } else {
                rc = 1;
                memcpy(nh, message + 6, 16); // patch for invalid network_address() processing
            }
#endif //----- REPLACE -----
            if(rc < 0) {
                have_v4_nh = 0;
                have_v6_nh = 0;
                goto fail;
            }
#ifdef BABELD_CODE //+++++ REPLACE +++++
            debugf("Received nh %s (%d) from %s on %s.\n",
                   format_address(nh), message[2],
                   format_address(from), ifp->name);
#else // CEFBABELD
            debugf("Received nh %s:%d (%d) from %s on %s.\n",
                   format_address(nh), nh_port, message[3],
                   format_address(from), ifp->name);
#ifdef DEB_UPDATE   //+++++ for DEB +++++
fprintf(stderr, ">>>>> Received NH (%s) port:%d [%d] from %s on %s.\n",
                   format_address(nh), nh_port, message[3],
                   format_address(from), ifp->name);
#endif              //----- for DEB -----

#endif //----- REPLACE -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
            if(message[2] == 1) {
#else // CEFBABELD
            if(message[3] == 1) {
#endif //----- REPLACE -----
                memcpy(v4_nh, nh, 16);
                have_v4_nh = 1;
            } else {
                memcpy(v6_nh, nh, 16);
                have_v6_nh = 1;
            }
#ifdef BABELD_CODE //+++++ DEL +++++
            rc = parse_other_subtlv(message + 4 + rc, len - 2 - rc);
            if(rc < 0)
                goto done;
#endif //----- DEL -----
        } else if(type == MESSAGE_UPDATE) {
#ifndef BABELD_CODE //+++++ ADD +++++
            /*
                0                   1                   2                   3
                0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |    Type = 8   |            Length             |      MBZ1     |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |     Flags     |              Plen             |      MBZ2     |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |             Interval          |            Seqno              |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |             Metric            |         Name Prefix...        /
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                /
                +-+-+-+-+-+-+-+-+-+-+-+-
            */
#endif //----- ADD -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
            unsigned char prefix[16], src_prefix[16], *nh;
            unsigned char plen, src_plen;
#else // CEFBABELD
            unsigned char prefix[NAME_PREFIX_LEN], src_prefix[16], *nh;
            uint16_t plen;
            unsigned char src_plen;
#endif //----- REPLACE -----
            unsigned char channels[MAX_CHANNEL_HOPS];
            int channels_len = MAX_CHANNEL_HOPS;
            unsigned short interval, seqno, metric;
#ifdef BABELD_CODE //+++++ REPLACE +++++
            int rc, parsed_len, is_ss;
            if(len < 10) {
                if(len < 2 || message[3] & 0x80)
                    have_v4_prefix = have_v6_prefix = 0;
#else // CEFBABELD
            if(length < 11) {
#endif //----- REPLACE -----
                goto fail;
            }
#ifdef BABELD_CODE //+++++ REPLACE +++++
            DO_NTOHS(interval, message + 6);
            DO_NTOHS(seqno, message + 8);
            DO_NTOHS(metric, message + 10);
            if(message[5] == 0 ||
               (message[2] == 1 ? have_v4_prefix : have_v6_prefix))
                rc = network_prefix(message[2], message[4], message[5],
                                    message + 12,
                                    message[2] == 1 ? v4_prefix : v6_prefix,
                                    len - 10, prefix);
            else
                rc = -1;
            if(message[2] == 1) {
                v4tov6(src_prefix, zeroes);
                src_plen = 96;
            } else {
                memcpy(src_prefix, zeroes, 16);
                src_plen = 0;
            }
            if(rc < 0) {
                if(message[3] & 0x80)
                    have_v4_prefix = have_v6_prefix = 0;
                goto fail;
            }
            parsed_len = 10 + rc;
#else // CEFBABELD
            DO_NTOHS(interval, message + 8);
            DO_NTOHS(seqno, message + 10);
            DO_NTOHS(metric, message + 12);
            DO_NTOHS(plength, message + 5);
           
            if(plength > 0) {
                cefore_prefix (plength, message + 14, prefix);
            }
#endif //----- REPLACE -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
            plen = message[4] + (message[2] == 1 ? 96 : 0);

            if(message[3] & 0x80) {
                if(message[2] == 1) {
                    memcpy(v4_prefix, prefix, 16);
                    have_v4_prefix = 1;
                } else {
                    memcpy(v6_prefix, prefix, 16);
                    have_v6_prefix = 1;
                }
            }
            if(message[3] & 0x40) {
                if(message[2] == 1) {
                    memset(router_id, 0, 4);
                    memcpy(router_id + 4, prefix + 12, 4);
                } else {
                    memcpy(router_id, prefix + 8, 8);
                }
                have_router_id = 1;
            }
            if(!have_router_id && message[2] != 0) {
                fprintf(stderr, "Received prefix with no router id.\n");
                goto fail;
            }
            debugf("Received update%s%s for %s from %s on %s myIP %s.\n",
                   (message[3] & 0x80) ? "/prefix" : "",
                   (message[3] & 0x40) ? "/id" : "",
                   format_prefix(prefix, plen),
                   format_address(from), ifp->name, format_address(neigh->ifp->ll[0]));
            if(message[2] == 1) {
                if(!have_v4_nh)
                    goto fail;
                nh = v4_nh;
            } else if(have_v6_nh) {
                nh = v6_nh;
            } else {
                nh = neigh->address;
            }

            rc = parse_update_subtlv(ifp, metric, message[2],
                                     message + 2 + parsed_len,
                                     len - parsed_len, channels, &channels_len,
                                     src_prefix, &src_plen);
            if(rc < 0)
                goto done;

            if(message[2] == 0) {
                if(metric < 0xFFFF) {
                    fprintf(stderr,
                            "Received wildcard update with finite metric.\n");
                    goto done;
                }
                if(src_plen > 0) {
                    fprintf(stderr,
                            "Received wildcard update with source prefix.\n");
                    goto done;
                }
                retract_neighbour_routes(neigh);
                goto done;
            }

            is_ss = !is_default(src_prefix, src_plen);
            debugf("Received update%s%s for dst %s%s%s from %s on %s myIP %s.\n",
                   (message[3] & 0x80) ? "/prefix" : "",
                   (message[3] & 0x40) ? "/id" : "",
                   format_prefix(prefix, plen),
                   is_ss ? " src " : "",
                   is_ss ? format_prefix(src_prefix, src_plen) : "",
                   format_address(from), ifp->name, format_address(neigh->ifp->ll[0]));

            if(message[2] == 1) {
                if(!ifp->ipv4)
                    goto done;
            }

            update_route(router_id, prefix, plen, src_prefix, src_plen, seqno,
                         metric, interval, neigh, nh,
                         channels, channels_len);
#else // CEFBABELD
            plen = plength;
            
            debugf("Received update for %s (seqno=%u, metric=%u) from %s on %s myIP %s.\n",
                   (plen > 0) ? format_cefore_prefix(prefix, plen) : "*",
                   seqno, metric,
                   format_address(from), ifp->name, format_address(neigh->ifp->ll[0]));
#ifdef DEB_UPDATE   //+++++ for DEB +++++
fprintf(stderr, ">>>>> Received UPDATE for %s (plen=%u seqno=%u, metric=%u) from %s on %s myIP %s.\n",
                (plen > 0) ? format_cefore_prefix(prefix, plen) : "*",
                plen, seqno, metric,
                format_address(from), ifp->name, format_address(neigh->ifp->ll[0]));
#endif              //----- for DEB -----
            
            if(have_v4_nh) {
                nh = v4_nh;
                memcpy(v4_nh, nh, 16);
            } else if(have_v6_nh) {
                nh = v6_nh;
            } else {
                nh = neigh->address;
            }
            memcpy(src_prefix, zeroes, 16);
            src_plen = 0;
            
            if (route_ctrl_type == ROUTE_CTRL_TYPE_MS) {
                 update_route_mpss(router_id, prefix, plen, src_prefix, src_plen, seqno,
                              metric, interval, neigh, nh, nh_port, 
                              channels, channels_len);
            } else if (route_ctrl_type == ROUTE_CTRL_TYPE_MM) {
                 update_route_mpms(router_id, prefix, plen, src_prefix, src_plen, seqno,
                              metric, interval, neigh, nh, nh_port, 
                              channels, channels_len);
            } else {
                  update_route(router_id, prefix, plen, src_prefix, src_plen, seqno,
                               metric, interval, neigh, nh, nh_port, 
                               channels, channels_len);
            }   
#endif //----- REPLACE -----

        } else if(type == MESSAGE_REQUEST) {
#ifndef BABELD_CODE //+++++ ADD +++++
            /*
                0                   1                   2                   3
                0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |    Type = 9   |            Length             |       PE      |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |              Plen             |          Name Prefix...       /
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            */
#endif //----- ADD -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
            unsigned char prefix[16], src_prefix[16], plen, src_plen;
            int rc, is_ss;
            if(len < 2) goto fail;
            rc = network_prefix(message[2], message[3], 0,
                                message + 4, NULL, len - 2, prefix);
            if(rc < 0) goto fail;
            plen = message[3] + (message[2] == 1 ? 96 : 0);
            if(message[2] == 1) {
                v4tov6(src_prefix, zeroes);
                src_plen = 96;
            } else {
                memcpy(src_prefix, zeroes, 16);
                src_plen = 0;
            }
            rc = parse_request_subtlv(message[2], message + 4 + rc,
                                      len - 2 - rc, src_prefix, &src_plen);
            if(rc < 0)
                goto done;
            is_ss = !is_default(src_prefix, src_plen);
            if(message[2] == 0) {
                if(is_ss) {
                    /* Wildcard requests don't carry a source prefix. */
                    fprintf(stderr,
                            "Received source-specific wildcard request.\n");
                    goto done;
                }
                debugf("Received request for any from %s on %s.\n",
                       format_address(from), ifp->name);
                /* If a neighbour is requesting a full route dump from us,
                   we might as well send it an IHU. */
                send_ihu(neigh, NULL);
                /* Since nodes send wildcard requests on boot, booting
                   a large number of nodes at the same time may cause an
                   update storm.  Ignore a wildcard request that happens
                   shortly after we sent a full update. */
                if(neigh->ifp->last_update_time <
                   now.tv_sec - MAX(neigh->ifp->hello_interval / 100, 1)) {
                    send_update(neigh->ifp, 0, NULL, 0, NULL, 0);
                }
            } else {
                debugf("Received request for dst %s%s%s from %s on %s.\n",
                       message[2] == 0 ? "" : format_prefix(prefix, plen),
                       is_ss ? " src " : "",
                       is_ss ? format_prefix(src_prefix, src_plen) : "",
                       format_address(from), ifp->name);
                send_update(neigh->ifp, 0, prefix, plen, src_prefix, src_plen);
            }
#else // CEFBABELD
            unsigned char prefix[NAME_PREFIX_LEN], src_prefix[16], plen, src_plen;

            if(length < 3) {
                goto fail;
            }
            
            DO_NTOHS(plength, message + 4);
            
            if(plength > 0) {
                cefore_prefix (plength, message + 6, prefix);
            }
            plen = (int) plength;
            
            debugf("Received request for %s from %s on %s.\n",
                   message[3] == 0 ? "any" : format_cefore_prefix(prefix, plen),
                   format_address(from), ifp->name);
#ifdef DEB_REQ   //+++++ for DEB +++++
fprintf(stderr, ">>>>> Received request for %s from %s on %s.\n",
       message[3] == 0 ? "any" : format_cefore_prefix(prefix, plen),
       format_address(from), ifp->name);
#endif   //----- for DEB -----
            
            if(message[3] == 0) {
                debugf("Received request for any from %s on %s.\n",
                       format_address(from), ifp->name);
#ifdef DEB_REQ   //+++++ for DEB +++++
fprintf(stderr, ">>>>> Received request for any from %s on %s.\n",
       format_address(from), ifp->name);
#endif   //----- for DEB -----
                /* If a neighbour is requesting a full route dump from us,
                   we might as well send it an IHU. */
                send_ihu(neigh, NULL);
                /* Since nodes send wildcard requests on boot, booting
                   a large number of nodes at the same time may cause an
                   update storm.  Ignore a wildcard request that happens
                   shortly after we sent a full update. */
                if(neigh->ifp->last_update_time <
                   now.tv_sec - MAX(neigh->ifp->hello_interval / 100, 1))
                    send_update(neigh->ifp, 0, NULL, 0, NULL, 0);
            } else {
                memcpy(src_prefix, zeroes, 16);
                src_plen = 0;
                send_update(neigh->ifp, 0, prefix, plen, src_prefix, src_plen);
            }

#endif //----- REPLACE -----
        } else if(type == MESSAGE_MH_REQUEST) {
#ifndef BABELD_CODE //+++++ ADD +++++
            /*
                0                   1                   2                   3
                0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |    Type = 10  |            Length             |      MBZ      |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |              Plen             |            Seqno              |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |  Hop Count    |                 Reserved                      |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |                                                               |
                +                           Router-Id                           +
                |                                                               |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |   Name Prefix...
                +-+-+-+-+-+-+-+-+-+-+
            */
#endif //----- ADD -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
            unsigned char prefix[16], src_prefix[16], plen, src_plen;
            unsigned short seqno;
            int rc, is_ss;
            if(len < 14) goto fail;
            DO_NTOHS(seqno, message + 4);
            rc = network_prefix(message[2], message[3], 0,
                                message + 16, NULL, len - 14, prefix);
            if(rc < 0) goto fail;
            if(message[2] == 1) {
                v4tov6(src_prefix, zeroes);
                src_plen = 96;
            } else {
                memcpy(src_prefix, zeroes, 16);
                src_plen = 0;
            }
            rc = parse_seqno_request_subtlv(message[2], message + 16 + rc,
                                            len - 14 - rc, src_prefix,
                                            &src_plen);
            if(rc < 0)
                goto done;
            is_ss = !is_default(src_prefix, src_plen);
            plen = message[3] + (message[2] == 1 ? 96 : 0);
            debugf("Received request (%d) for dst %s%s%s from %s on "
                   "%s (%s, %d).\n",
                   message[6],
                   format_prefix(prefix, plen),
                   is_ss ? " src " : "",
                   is_ss ? format_prefix(src_prefix, src_plen) : "",
                   format_address(from), ifp->name,
                   format_eui64(message + 8), seqno);
            handle_request(neigh, prefix, plen, src_prefix, src_plen,
                           message[6], seqno, message + 8);
        } else {
            debugf("Received unknown packet type %d from %s on %s.\n",
                   type, format_address(from), ifp->name);
        }
#else // CEFBABELD
            unsigned char prefix[NAME_PREFIX_LEN], src_prefix[16], plen, src_plen;
            unsigned short seqno;
            if(length < 17) goto fail;
            DO_NTOHS(plength, message + 4);
            DO_NTOHS(seqno, message + 6);
            
            if(plength > 0) {
                cefore_prefix (plength, message + 20, prefix);
            }
            
            plen = (int) plength;
            memcpy(src_prefix, zeroes, 16);
            src_plen = 0;
            
            debugf("Received seq-request (%d) for %s from %s on %s (%s, %d).\n",
                   message[8],
                   format_cefore_prefix(prefix, plen),
                   format_address(from), ifp->name,
                   format_eui64(message + 12), seqno);
#ifdef DEB_REQ   //+++++ for DEB +++++
fprintf(stderr, ">>>>> Received seq-request (%d) for %s from %s on %s (%s, %d).\n",
       message[8],
       format_cefore_prefix(prefix, plen),
       format_address(from), ifp->name,
       format_eui64(message + 12), seqno);
#endif   //----- for DEB -----
            handle_request(neigh, prefix, plen, src_prefix, src_plen,
                           message[8], seqno, message + 12);
        } else {
            debugf("Received unknown packet type %d from %s on %s.\n",
                   type, format_address(from), ifp->name);
        }
#endif //----- REPLACE -----
    done:
#ifdef BABELD_CODE //+++++ REPLACE +++++
        i += len + 2;
#else // CEFBABELD
        i += length + 3;
#endif //----- REPLACE -----
        continue;

    fail:
#ifdef BABELD_CODE //+++++ REPLACE +++++
        fprintf(stderr, "Couldn't parse packet (%d, %d) from %s on %s.\n",
                message[0], message[1], format_address(from), ifp->name);
#else // CEFBABELD
        fprintf(stderr, "Couldn't parse packet (%d, %d) from %s on %s.\n",
                message[0], length, format_address(from), ifp->name);
#endif //----- REPLACE -----
        goto done;
    }

    /* We can calculate the RTT to this neighbour. */
    if(have_hello_rtt && hello_send_us && hello_rtt_receive_time) {
        int remote_waiting_us, local_waiting_us;
        unsigned int rtt, smoothed_rtt;
        unsigned int old_rttcost;
        int changed = 0;
        remote_waiting_us = neigh->hello_send_us - hello_rtt_receive_time;
        local_waiting_us = time_us(neigh->hello_rtt_receive_time) -
            hello_send_us;

        /* Sanity checks (validity window of 10 minutes). */
        if(remote_waiting_us < 0 || local_waiting_us < 0 ||
           remote_waiting_us > 600000000 || local_waiting_us > 600000000)
            return;

        rtt = MAX(0, local_waiting_us - remote_waiting_us);
        debugf("RTT to %s on %s sample result: %d us.\n",
               format_address(from), ifp->name, rtt);

        old_rttcost = neighbour_rttcost(neigh);
        if(valid_rtt(neigh)) {
            /* Running exponential average. */
            smoothed_rtt = (ifp->rtt_decay * rtt +
                            (256 - ifp->rtt_decay) * neigh->rtt);
            /* Rounding (up or down) to get closer to the sample. */
            neigh->rtt = (neigh->rtt >= rtt) ? smoothed_rtt / 256 :
                (smoothed_rtt + 255) / 256;
        } else {
            /* We prefer to be conservative with new neighbours
               (higher RTT) */
            assert(rtt <= 0x7FFFFFFF);
            neigh->rtt = 2*rtt;
        }
        changed = (neighbour_rttcost(neigh) == old_rttcost ? 0 : 1);
        update_neighbour_metric(neigh, changed);
        neigh->rtt_time = now;
    }
    return;
}

static int
fill_rtt_message(struct buffered *buf, struct interface *ifp)
{
    if((ifp->flags & IF_TIMESTAMPS) != 0 && (buf->hello >= 0)) {
        if(buf->buf[buf->hello + 8] == SUBTLV_PADN &&
           buf->buf[buf->hello + 9] == 4) {
            unsigned int time;
            /* Change the type of sub-TLV. */
            buf->buf[buf->hello + 8] = SUBTLV_TIMESTAMP;
            gettime(&now);
            time = time_us(now);
            DO_HTONL(buf->buf + buf->hello + 10, time);
            return 1;
        } else {
            fprintf(stderr,
                    "No space left for timestamp sub-TLV "
                    "(this shouldn't happen)\n");
            return -1;
        }
    }
    return 0;
}

void
flushbuf(struct buffered *buf, struct interface *ifp)
{
    int rc;

    assert(buf->len <= buf->size);

    if(buf->len > 0) {
        debugf("  (flushing %d buffered bytes)\n", buf->len);
        DO_HTONS(packet_header + 2, buf->len);
        fill_rtt_message(buf, ifp);
        rc = babel_send(protocol_socket,
                        packet_header, sizeof(packet_header),
                        buf->buf, buf->len,
                        (struct sockaddr*)&buf->sin6,
                        sizeof(buf->sin6));
        if(rc < 0)
            perror("send");
    }
    VALGRIND_MAKE_MEM_UNDEFINED(buf->buf, buf->size);
    buf->len = 0;
    buf->hello = -1;
    buf->have_id = 0;
    buf->have_nh = 0;
    buf->have_prefix = 0;
    buf->timeout.tv_sec = 0;
    buf->timeout.tv_usec = 0;
}

static void
schedule_flush_ms(struct buffered *buf, int msecs)
{
    if(buf->timeout.tv_sec != 0 &&
       timeval_minus_msec(&buf->timeout, &now) < msecs)
        return;
    set_timeout(&buf->timeout, msecs);
}

static void
schedule_flush(struct buffered *buf)
{
    schedule_flush_ms(buf, jitter(buf, 0));
}

static void
schedule_flush_now(struct buffered *buf)
{
    schedule_flush_ms(buf, roughly(10));
}

static void
ensure_space(struct buffered *buf, struct interface *ifp, int space)
{
    if(buf->size - buf->len < space)
        flushbuf(buf, ifp);
}

static void
start_message(struct buffered *buf, struct interface *ifp, int type, int len)
{
#ifdef BABELD_CODE //+++++ REPLACE +++++
    if(buf->size - buf->len < len + 2)
        flushbuf(buf, ifp);
    buf->buf[buf->len++] = type;
    buf->buf[buf->len++] = len;
#else // CEFBABELD
    uint16_t val;
    if(buf->size - buf->len < len + 3)
        flushbuf(buf, ifp);
    buf->buf[buf->len] = type;
    val = htons (len);
    memcpy (&(buf->buf[buf->len+1]), &val, 2);
    buf->len += 3;
#endif //----- REPLACE -----
}

static void
end_message(struct buffered *buf, int type, int bytes)
{
#ifdef BABELD_CODE //+++++ REPLACE +++++
    assert(buf->len >= bytes + 2 &&
           buf->buf[buf->len - bytes - 2] == type &&
           buf->buf[buf->len - bytes - 1] == bytes);
    schedule_flush(buf);
#else // CEFBABELD
    assert(buf->len >= bytes + 3);
    assert(buf->buf[buf->len - bytes - 3] == type);
    schedule_flush(buf);
#endif //----- REPLACE -----
}

static void
accumulate_byte(struct buffered *buf, unsigned char value)
{
    buf->buf[buf->len++] = value;
}

static void
accumulate_short(struct buffered *buf, unsigned short value)
{
    DO_HTONS(buf->buf + buf->len, value);
    buf->len += 2;
}

static void
accumulate_int(struct buffered *buf, unsigned int value)
{
    DO_HTONL(buf->buf + buf->len, value);
    buf->len += 4;
}

static void
accumulate_bytes(struct buffered *buf,
                 const unsigned char *value, unsigned len)
{
    memcpy(buf->buf + buf->len, value, len);
    buf->len += len;
}

void
send_ack(struct neighbour *neigh, unsigned short nonce, unsigned short interval)
{
#ifndef BABELD_CODE //+++++ ADD +++++
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |    Type = 3   |            Length             |    Rserved    |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |            Nonce              |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
#endif //----- ADD -----
    debugf("Sending ack (%04x) to %s on %s.\n",
           nonce, format_address(neigh->address), neigh->ifp->name);
#ifdef BABELD_CODE //+++++ REPLACE +++++
    start_message(&neigh->buf, neigh->ifp, MESSAGE_ACK, 2);
    accumulate_short(&neigh->buf, nonce);
    end_message(&neigh->buf, MESSAGE_ACK, 2);
#else // CEFBABELD
    start_message(&neigh->buf, neigh->ifp, MESSAGE_ACK, 3);
    accumulate_byte(&neigh->buf, 0);
    accumulate_short(&neigh->buf, nonce);
    end_message(&neigh->buf, MESSAGE_ACK, 3);
#endif //----- REPLACE -----
    /* Roughly yields a value no larger than 3/2, so this meets the deadline */
    schedule_flush_ms(&neigh->buf, roughly(interval * 6));
}

static void
buffer_hello(struct buffered *buf, struct interface *ifp,
             unsigned short seqno, unsigned interval, int unicast)
{
#ifndef BABELD_CODE //+++++ ADD +++++
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |    Type = 4   |            Length             |    Rserved    |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |            Seqno              |          Interval             |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
    
#endif //----- ADD -----
    int timestamp = !!(ifp->flags & IF_TIMESTAMPS);
#ifdef BABELD_CODE //+++++ REPLACE +++++
    start_message(buf, ifp, MESSAGE_HELLO, timestamp ? 12 : 6);
    buf->hello = buf->len - 2;
    accumulate_short(buf, unicast ? 0x8000 : 0);
#else // CEFBABELD
#ifdef DEB_UPDATE   //+++++ for DEB +++++
    fprintf(stderr, "<<<<< Send HELLO\n");
#endif              //----- for DEB -----
    start_message(buf, ifp, MESSAGE_HELLO, timestamp ? 11 : 5);
    buf->hello = buf->len - 3;
    accumulate_byte(buf, unicast ? 0x80 : 0);
#endif //----- REPLACE -----
    accumulate_short(buf, seqno);
    accumulate_short(buf, interval > 0xFFFF ? 0xFFFF : interval);
    if(timestamp) {
        /* Sub-TLV containing the local time of emission. We use a
           Pad4 sub-TLV, which we'll fill just before sending. */
        accumulate_byte(buf, SUBTLV_PADN);
        accumulate_byte(buf, 4);
        accumulate_int(buf, 0);
    }
#ifdef BABELD_CODE //+++++ REPLACE +++++
    end_message(buf, MESSAGE_HELLO, timestamp ? 12 : 6);
#else // CEFBABELD  
    end_message(buf, MESSAGE_HELLO, timestamp ? 11 : 5);
#endif //----- REPLACE -----
}

void
send_multicast_hello(struct interface *ifp, unsigned interval, int force)
{
    if(!if_up(ifp))
        return;

    if(interval == 0 && (ifp->flags & IF_RFC6126) != 0)
        /* Unscheduled hellos are incompatible with RFC 6126. */
        return;

    /* This avoids sending multiple hellos in a single packet, which breaks
       link quality estimation. */
    if(ifp->buf.hello >= 0) {
        if(force) {
            flushupdates(ifp);
            flushbuf(&ifp->buf, ifp);
        } else {
            return;
        }
    }

    ifp->hello_seqno = seqno_plus(ifp->hello_seqno, 1);
    if(interval > 0)
        set_timeout(&ifp->hello_timeout, ifp->hello_interval);

    debugf("Sending hello %d (%d) to %s.\n",
           ifp->hello_seqno, interval, ifp->name);

    buffer_hello(&ifp->buf, ifp, ifp->hello_seqno, interval, 0);
}

void
send_unicast_hello(struct neighbour *neigh, unsigned interval, int force)
{
    if(!if_up(neigh->ifp))
        return;

    if((neigh->ifp->flags & IF_RFC6126) != 0)
        /* Unicast hellos are incompatible with RFC 6126. */
        return;

    if(neigh->buf.hello >= 0) {
        if(force)
            flushbuf(&neigh->buf, neigh->ifp);
        else
            return;
    }

    neigh->hello_seqno = seqno_plus(neigh->hello_seqno, 1);

    debugf("Sending unicast hello %d (%d) on %s.\n",
           neigh->hello_seqno, interval, neigh->ifp->name);

    buffer_hello(&neigh->buf, neigh->ifp, neigh->hello_seqno, interval, 1);
}

void
send_hello(struct interface *ifp)
{
    send_multicast_hello(ifp, (ifp->hello_interval + 9) / 10, 1);
    /* Send full IHU every 3 hellos, and marginal IHU each time */
    if(ifp->hello_seqno % 3 == 0)
        send_ihu(NULL, ifp);
    else
        send_marginal_ihu(ifp);
}

static void
really_buffer_update(struct buffered *buf, struct interface *ifp,
                     const unsigned char *id,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                     const unsigned char *prefix, unsigned char plen,
                     const unsigned char *src_prefix, unsigned char src_plen,
                     unsigned short seqno, unsigned short metric,
                     unsigned char *channels, int channels_len)
#else // CEFBABELD
                     const unsigned char *prefix, uint16_t plen,
                     const unsigned char *src_prefix, unsigned char src_plen,
                     unsigned short seqno, unsigned short metric, unsigned short port,
                     unsigned char *channels, int channels_len)
#endif //----- REPLACE -----
{
#ifdef BABELD_CODE //+++++ REPLACE +++++
    int add_metric, v4, real_plen, real_src_plen;
    int omit, spb, channels_size, len;
    const unsigned char *real_prefix, *real_src_prefix;
    unsigned short flags = 0;
    int is_ss = !is_default(src_prefix, src_plen);

    if(!if_up(ifp))
        return;

    if(is_ss && (ifp->flags & IF_RFC6126) != 0)
        return;

    add_metric = output_filter(id, prefix, plen, src_prefix,
                               src_plen, ifp->ifindex);
    if(add_metric >= INFINITY)
        return;

    metric = MIN(metric + add_metric, INFINITY);

    /* Worst case */
    ensure_space(buf, ifp, 20 + 12 + 28 + 18);

    v4 = plen >= 96 && v4mapped(prefix);
    
    if(v4) {
        if(!ifp->ipv4)
            return;
        omit = 0;
        if(!buf->have_nh ||
            memcmp(buf->nh, ifp->ipv4, 4) != 0) {
            start_message(buf, ifp, MESSAGE_NH, 6);
            accumulate_byte(buf, 1);
            accumulate_byte(buf, 0);
            accumulate_bytes(buf, ifp->ipv4, 4);
            end_message(buf, MESSAGE_NH, 6);
            memcpy(&buf->nh, ifp->ipv4, 4);
            buf->have_nh = 1;
        }
        real_prefix = prefix + 12;
        real_plen = plen - 96;
        real_src_prefix = src_prefix + 12;
        real_src_plen = src_plen - 96;
    } else {
        omit = 0;
        if(buf->have_prefix) {
            while(omit < plen / 8 &&
                  buf->prefix[omit] == prefix[omit])
                omit++;
        }
        if(!buf->have_prefix || plen >= 48)
            flags |= 0x80;
        real_prefix = prefix;
        real_plen = plen;
        real_src_prefix = src_prefix;
        real_src_plen = src_plen;
    }

    if(!buf->have_id || memcmp(id, buf->id, 8) != 0) {
        if(real_plen == 128 && memcmp(real_prefix + 8, id, 8) == 0) {
            flags |= 0x40;
        } else {
            start_message(buf, ifp, MESSAGE_ROUTER_ID, 10);
            accumulate_short(buf, 0);
            accumulate_bytes(buf, id, 8);
            end_message(buf, MESSAGE_ROUTER_ID, 10);
        }
        memcpy(buf->id, id, 8);
        buf->have_id = 1;
    }

    channels_size = diversity_kind == DIVERSITY_CHANNEL && channels_len >= 0 ?
        channels_len + 2 : 0;
    len = 10 + (real_plen + 7) / 8 - omit + channels_size;
    spb = (real_src_plen + 7) / 8;
    if(is_ss)
        len += 3 + spb;

    start_message(buf, ifp, MESSAGE_UPDATE, len);
    accumulate_byte(buf, v4 ? 1 : 2);
    accumulate_byte(buf, flags);
    accumulate_byte(buf, real_plen);
    accumulate_byte(buf, omit);
    accumulate_short(buf, (ifp->update_interval + 5) / 10);
    accumulate_short(buf, seqno);
    accumulate_short(buf, metric);
    accumulate_bytes(buf, real_prefix + omit, (real_plen + 7) / 8 - omit);
    if(is_ss) {
        accumulate_byte(buf, SUBTLV_SOURCE_PREFIX);
        accumulate_byte(buf, 1 + spb);
        accumulate_byte(buf, real_src_plen);
        accumulate_bytes(buf, real_src_prefix, spb);
    }
    /* Note that an empty channels TLV is different from no such TLV. */
    if(channels_size > 0) {
        accumulate_byte(buf, 2);
        accumulate_byte(buf, channels_len);
        accumulate_bytes(buf, channels, channels_len);
    }
    end_message(buf, MESSAGE_UPDATE, len);
    if(flags & 0x80) {
        memcpy(buf->prefix, prefix, 16);
        buf->have_prefix = 1;
    }
#else // CEFBABELD  
    if(diversity_kind != DIVERSITY_CHANNEL)
        channels_len = -1;
    
    if(!if_up(ifp))
        return;
    
    metric = MIN(metric, INFINITY);
    
    /* Worst case */
    ensure_space(buf, ifp, 20 + 12 + 28 + 18);
    
RESET_NH:   
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |    Type = 7   |            Length             |       AE      |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |              Port             |     Next hop...
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-
    */
    if(ifp->specified_ipv4) {
        if(!buf->have_nh ||
           memcmp(buf->nh, ifp->specified_ipv4, 4) != 0) {
#ifdef DEB_UPDATE   //+++++ for DEB +++++
fprintf(stderr, "<<<<< Send NH (%d.%d.%d.%d)\n", 
    ifp->specified_ipv4[0], ifp->specified_ipv4[1], ifp->specified_ipv4[2], ifp->specified_ipv4[3]);
#endif              //----- for DEB -----
            start_message(buf, ifp, MESSAGE_NH, 7);
            accumulate_byte(buf, 1);
            accumulate_short(buf, port);
            accumulate_bytes(buf, ifp->specified_ipv4, 4);
            end_message(buf, MESSAGE_NH, 7);
            memcpy(&buf->nh, ifp->specified_ipv4, 4);
            buf->have_nh = 1;
       }
    } else 
    if(ifp->specified_ipv6) {
        if(!buf->have_nh ||
           memcmp(buf->nh, ifp->specified_ipv6, 16) != 0) {
#ifdef DEB_UPDATE   //+++++ for DEB +++++
fprintf(stderr, "<<<<< Send NH (forIPv6-%s)\n", format_address(ifp->specified_ipv6));
#endif              //----- for DEB -----
            start_message(buf, ifp, MESSAGE_NH, 1+2+16);
            accumulate_byte(buf, 3);
            accumulate_short(buf, port);
            accumulate_bytes(buf, ifp->specified_ipv6, 16);
            end_message(buf, MESSAGE_NH, 1+2+16);
            memcpy(&buf->nh, ifp->specified_ipv6, 16);
            buf->have_nh = 1;
            
         }
    } else 
    if(ifp->ipv4) {
        if(!buf->have_nh ||
           memcmp(buf->nh, ifp->ipv4, 4) != 0) {
#ifdef DEB_UPDATE   //+++++ for DEB +++++
fprintf(stderr, "<<<<< Send NH (%d.%d.%d.%d)\n", ifp->ipv4[0], ifp->ipv4[1], ifp->ipv4[2], ifp->ipv4[3]);
#endif              //-----for DEB -----
            start_message(buf, ifp, MESSAGE_NH, 7);
            accumulate_byte(buf, 1);
            accumulate_short(buf, port);
            accumulate_bytes(buf, ifp->ipv4, 4);
            end_message(buf, MESSAGE_NH, 7);
            memcpy(&buf->nh, ifp->ipv4, 4);
            buf->have_nh = 1;
       }
    } else {
        if(!buf->have_nh ||
           memcmp(buf->nh, ifp->ll[0], 16) != 0) {
#ifdef DEB_UPDATE   //+++++ for DEB +++++
fprintf(stderr, "<<<<< Send NH (forIPv6-%s)\n", format_address(ifp->ll[0]));
#endif              //----- for DEB -----
            start_message(buf, ifp, MESSAGE_NH, 1+2+16);
            accumulate_byte(buf, 3);
            accumulate_short(buf, port);
            accumulate_bytes(buf, ifp->ll[0], 16);
            end_message(buf, MESSAGE_NH, 1+2+16);
            memcpy(&buf->nh, ifp->ll[0], 16);
            buf->have_nh = 1;
            
         }
    }
            
            /*
                0                   1                   2                   3
                0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |    Type = 6   |            Length             |    Rserved    |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                |                                                               |
                +                           Router-Id                           +
                |                                                               |
                +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            */
    if(!buf->have_id || memcmp(id, buf->id, 8) != 0) {
#ifdef DEB_UPDATE   //+++++ for DEB +++++
fprintf(stderr, "<<<<< Send ROUTER_ID (%s)\n", format_eui64(id));
#endif              //----- for DEB -----
        start_message(buf, ifp, MESSAGE_ROUTER_ID, 9);
        accumulate_byte(buf, 0);
        accumulate_bytes(buf, id, 8);
        end_message(buf, MESSAGE_ROUTER_ID, 9);
        memcpy(buf->id, id, 8);
        buf->have_id = 1;
    }
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |    Type = 8   |            Length             |       MBZ1    |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |     Flags     |              Plen             |       MBZ2    |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |             Interval          |            Seqno              |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |             Metric            |         Name Prefix...        /
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        /
        +-+-+-+-+-+-+-+-+-+-+-+-
    */
        
    if (((11 + plen) + buf->len) > buf->size){
        flushbuf(buf, ifp);
        goto RESET_NH;
    }
    
#ifdef DEB_UPDATE   //+++++ for DEB +++++
    fprintf(stderr, "<<<<< Send UPDATE prefix=%s (plen=%u, seqno=%u, metric=%u)\n"
                  , format_cefore_prefix(prefix, plen), plen, seqno, metric);
#endif              //----- for DEB -----
    debugf("Sent update prefix=%s (plen=%u, seqno=%u, metric=%u) to %s\n"
                  , format_cefore_prefix(prefix, plen), plen, seqno, metric, ifp->name);
    start_message(buf, ifp, MESSAGE_UPDATE, 11 + plen);
    accumulate_byte(buf, 0);
    accumulate_byte(buf, 0);
    accumulate_short(buf, (unsigned short) plen);
    accumulate_byte(buf, 0);
    accumulate_short(buf, (ifp->update_interval + 5) / 10);
    accumulate_short(buf, seqno);
    accumulate_short(buf, metric);
    accumulate_bytes(buf, prefix, plen);
    end_message(buf, MESSAGE_UPDATE, 11 + plen);
#endif //----- REPLACE -----
}

static void
really_send_update(struct interface *ifp, const unsigned char *id,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                   const unsigned char *prefix, unsigned char plen,
                   const unsigned char *src_prefix, unsigned char src_plen,
                   unsigned short seqno, unsigned short metric,
                   unsigned char *channels, int channels_len)
#else // CEFBABELD
                   const unsigned char *prefix, uint16_t plen,
                   const unsigned char *src_prefix, unsigned char src_plen,
                   unsigned short seqno, unsigned short metric, unsigned short port, 
                   unsigned char *channels, int channels_len)
#endif //----- REPLACE -----
{
    if(!if_up(ifp))
        return;

    if((ifp->flags & IF_UNICAST) != 0) {
        struct neighbour *neigh;
        FOR_ALL_NEIGHBOURS(neigh) {
            if(neigh->ifp == ifp) {
#ifdef BABELD_CODE //+++++ REPLACE +++++
                really_buffer_update(&neigh->buf, ifp, id,
                                     prefix, plen, src_prefix, src_plen,
                                     seqno, metric, channels, channels_len);
#else // CEFBABELD
                really_buffer_update(&neigh->buf, ifp, id,
                                     prefix, plen, src_prefix, src_plen,
                                     seqno, metric, port, channels, channels_len);
#endif //----- REPLACE -----
            }
        }
    } else {
#ifdef BABELD_CODE //+++++ REPLACE +++++
        really_buffer_update(&ifp->buf, ifp, id,
                             prefix, plen, src_prefix, src_plen,
                             seqno, metric, channels, channels_len);
#else // CEFBABELD
        really_buffer_update(&ifp->buf, ifp, id,
                             prefix, plen, src_prefix, src_plen,
                             seqno, metric, port, channels, channels_len);
#endif //----- REPLACE -----
    }
}

static int
compare_buffered_updates(const void *av, const void *bv)
{
    const struct buffered_update *a = av, *b = bv;
#ifdef BABELD_CODE //+++++ REPLACE +++++
    int rc, v4a, v4b, ma, mb;
#else // CEFBABELD
    int rc;
#endif //----- REPLACE -----

#ifdef BABELD_CODE //+++++ DEL +++++
    rc = memcmp(a->id, b->id, 8);
    if(rc != 0)
        return rc;

    v4a = (a->plen >= 96 && v4mapped(a->prefix));
    v4b = (b->plen >= 96 && v4mapped(b->prefix));

    if(v4a > v4b)
        return 1;
    else if(v4a < v4b)
        return -1;

    ma = (!v4a && a->plen == 128 && memcmp(a->prefix + 8, a->id, 8) == 0);
    mb = (!v4b && b->plen == 128 && memcmp(b->prefix + 8, b->id, 8) == 0);

    if(ma > mb)
        return -1;
    else if(mb > ma)
        return 1;
#endif //----- DEL -----
    
    if(a->plen < b->plen)
        return 1;
    else if(a->plen > b->plen)
        return -1;

#ifdef BABELD_CODE //+++++ REPLACE +++++
    rc = memcmp(a->prefix, b->prefix, 16);
#else // CEFBABELD
    rc = memcmp(a->prefix, b->prefix, a->plen);
#endif //----- REPLACE -----
    if(rc != 0)
        return rc;

    if(a->src_plen < b->src_plen)
        return -1;
    else if(a->src_plen > b->src_plen)
        return 1;

    return memcmp(a->src_prefix, b->src_prefix, 16);
}

void
flushupdates(struct interface *ifp) 
{
    struct xroute *xroute;
    struct babel_route *route;
    const unsigned char *last_prefix = NULL;
    const unsigned char *last_src_prefix = NULL;
#ifdef BABELD_CODE //+++++ REPLACE +++++
    unsigned char last_plen = 0xFF;
#else // CEFBABELD
    uint16_t last_plen = 0xFFFF;
#endif //----- REPLACE -----
    unsigned char last_src_plen = 0xFF;
    int i;

    if(ifp == NULL) {
        struct interface *ifp_aux;
        FOR_ALL_INTERFACES(ifp_aux)
            flushupdates(ifp_aux);
        return;
    }
#ifndef BABELD_CODE //+++++ ADD +++++
	if(exist_installed_route()==0 && exist_xroute()==0){
	    cefstat_sent_update_num = 0;
	}
#endif //----- ADD -----

    if(ifp->num_buffered_updates > 0) {
        struct buffered_update *b = ifp->buffered_updates;
        int n = ifp->num_buffered_updates;

        ifp->buffered_updates = NULL;
        ifp->update_bufsize = 0;
        ifp->num_buffered_updates = 0;

        if(!if_up(ifp))
            goto done;

        debugf("  (flushing %d buffered updates on %s (%d))\n",
               n, ifp->name, ifp->ifindex);

        /* In order to send fewer update messages, we want to send updates
           with the same router-id together, with IPv6 going out before IPv4. */

        for(i = 0; i < n; i++) {
            route = find_installed_route(b[i].prefix, b[i].plen,
                                         b[i].src_prefix, b[i].src_plen);
            if(route)
                memcpy(b[i].id, route->src->id, 8);
            else                                   
                memcpy(b[i].id, myid, 8);
        }

        qsort(b, n, sizeof(struct buffered_update), compare_buffered_updates);

#ifndef BABELD_CODE //+++++ ADD +++++
        cefstat_sent_update_num = 0;
#endif //----- ADD -----
        for(i = 0; i < n; i++) {
            /* The same update may be scheduled multiple times before it is
               sent out.  Since our buffer is now sorted, it is enough to
               compare with the previous update. */

#ifdef BABELD_CODE //+++++ REPLACE +++++
            if(last_prefix &&
               b[i].plen == last_plen &&
               b[i].src_plen == last_src_plen &&
               memcmp(b[i].prefix, last_prefix, 16) == 0 &&
               memcmp(b[i].src_prefix, last_src_prefix, 16) == 0)
                continue;
#else  // CEFBABELD
            if(last_prefix 
                &&
               b[i].plen == last_plen 
                &&
               b[i].src_plen == last_src_plen 
                &&
               memcmp(b[i].prefix, last_prefix, last_plen) == 0 
                && 
               memcmp(b[i].src_prefix, last_src_prefix, 16) == 0 ) {
                continue;
            }
#endif //----- REPLACE -----
            xroute = find_xroute(b[i].prefix, b[i].plen,
                                 b[i].src_prefix, b[i].src_plen);
            route = find_installed_route(b[i].prefix, b[i].plen,
                                         b[i].src_prefix, b[i].src_plen);

#ifdef BABELD_CODE //+++++ REPLACE +++++
            if(xroute && (!route || xroute->metric <= kernel_metric)) {
                really_send_update(ifp, myid,
                                   xroute->prefix, xroute->plen,
                                   xroute->src_prefix, xroute->src_plen,
                                   myseqno, xroute->metric,
                                   NULL, 0);
#else // CEFBABELD
            if(xroute && !route) {
                really_send_update(ifp, myid,
                                   xroute->prefix, xroute->plen,
                                   xroute->src_prefix, xroute->src_plen,
                                   myseqno, xroute->metric, cefore_portnum, 
                                   NULL, 0);

                cefstat_sent_update_num++;
#endif //----- REPLACE -----
                last_prefix = xroute->prefix;
                last_plen = xroute->plen;
                last_src_prefix = xroute->src_prefix;
                last_src_plen = xroute->src_plen;
            } else if(route) {
                unsigned char channels[MAX_CHANNEL_HOPS];
                int chlen;
                struct interface *route_ifp = route->neigh->ifp;
                unsigned short metric;
                unsigned short seqno;

                seqno = route->seqno;
                metric = 
                    route_interferes(route, ifp) ?
                    route_metric(route) : 
                    route_metric_noninterfering(route);
#ifndef BABELD_CODE //+++++ ADD for MP +++++
                if (route_ctrl_type == ROUTE_CTRL_TYPE_MS) {
                    metric = updateFeasibleDistance_mpss(route->src->prefix, route->src->plen,
                                                 route->src->src_prefix, route->src->src_plen);
                    if(metric == INFINITY){
                        continue;
                    }
                } else if (route_ctrl_type == ROUTE_CTRL_TYPE_MM) {
                    struct best_route* broute;
                    broute = find_bestroute(route->src->prefix, route->src->plen,
                                            route->src->src_prefix, route->src->src_plen,
                                            0);
                    if(!broute){
                        continue;
                    }
                    route = find_route_assoc_broute(broute,
                                               route->src->prefix, route->src->plen,
                                               route->src->src_prefix, route->src->src_plen);
                    if(route ==NULL){
                        continue;
                    }
                    metric = broute->my_FD;
                    seqno = broute->my_seqNo;
                    route_ifp = route->neigh->ifp;
                }
                    
#endif //----- ADD for MP -----

                if(metric < INFINITY)
                    satisfy_request(route->src->prefix, route->src->plen,
                                    route->src->src_prefix,
                                    route->src->src_plen,
                                    seqno, route->src->id, ifp);

                if((ifp->flags & IF_SPLIT_HORIZON) &&
                   route->neigh->ifp == ifp)
                    continue;

                if(route_ifp->channel == IF_CHANNEL_NONINTERFERING) {
                    memcpy(channels, route->channels,
                           MIN(route->channels_len, MAX_CHANNEL_HOPS));
                    chlen = MIN(route->channels_len, MAX_CHANNEL_HOPS);
                } else {
                    if(route_ifp->channel == IF_CHANNEL_UNKNOWN)
                        channels[0] = IF_CHANNEL_INTERFERING;
                    else {
                        assert(route_ifp->channel > 0 &&
                               route_ifp->channel <= 255);
                        channels[0] = route_ifp->channel;
                    }
                    memcpy(channels + 1, route->channels,
                           MIN(route->channels_len, MAX_CHANNEL_HOPS - 1));
                    chlen = 1 + MIN(route->channels_len, MAX_CHANNEL_HOPS - 1);
                }

#ifdef BABELD_CODE //+++++ REPLACE +++++
                really_send_update(ifp, route->src->id,
                                   route->src->prefix, route->src->plen,
                                   route->src->src_prefix,
                                   route->src->src_plen,
                                   seqno, metric,
                                   channels, chlen);
#else  // CEFBABELD
                really_send_update(ifp, route->src->id,
                                   route->src->prefix, route->src->plen,
                                   route->src->src_prefix,
                                   route->src->src_plen,
                                   seqno, metric, cefore_portnum, 
                                   channels, chlen);
                cefstat_sent_update_num++;
#endif //----- REPLACE -----
                if (route_ctrl_type == ROUTE_CTRL_TYPE_MM) {
                    update_source_mpms(route->src->prefix, route->src->plen, 
                                    route->src->src_prefix, route->src->src_plen);
                } else {
                    update_source(route->src, seqno, metric);
                }
                last_prefix = route->src->prefix;
                last_plen = route->src->plen;
                last_src_prefix = route->src->src_prefix;
                last_src_plen = route->src->src_plen;
            } else {
            /* There's no route for this prefix.  This can happen shortly
               after an xroute has been retracted, so send a retraction. */
#ifndef BABELD_CODE //+++++ ADD for MP +++++
                if (route_ctrl_type == ROUTE_CTRL_TYPE_MS) {
                    continue;
                } else 
                if (route_ctrl_type == ROUTE_CTRL_TYPE_MM) {
                    continue;
                }
#endif //----- ADD for MP -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
                really_send_update(ifp, myid,
                                   b[i].prefix, b[i].plen,
                                   b[i].src_prefix, b[i].src_plen,
                                   myseqno, INFINITY, NULL, -1);
#else // CEFBABELD
                really_send_update(ifp, myid,
                                   b[i].prefix, b[i].plen,
                                   b[i].src_prefix, b[i].src_plen,
                                   myseqno, INFINITY, cefore_portnum, NULL, -1);
                cefstat_sent_update_num++;
#endif //----- REPLACE -----
            }
        }

        if((ifp->flags & IF_UNICAST) != 0) {
            struct neighbour *neigh;
            FOR_ALL_NEIGHBOURS(neigh) {
                if(neigh->ifp == ifp) {
                    schedule_flush_now(&neigh->buf);
                }
            }
        } else {
            schedule_flush_now(&ifp->buf);
        }
    done:
        free(b);
    }
    ifp->update_flush_timeout.tv_sec = 0;
    ifp->update_flush_timeout.tv_usec = 0;
}

static void
schedule_update_flush(struct interface *ifp, int urgent)
{
    unsigned msecs;
    msecs = update_jitter(ifp, urgent);
    if(ifp->update_flush_timeout.tv_sec != 0 &&
       timeval_minus_msec(&ifp->update_flush_timeout, &now) < msecs)
        return;
    set_timeout(&ifp->update_flush_timeout, msecs);
}

static void
buffer_update(struct interface *ifp,
#ifdef BABELD_CODE //+++++ REPLACE +++++
              const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
              const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
              const unsigned char *src_prefix, unsigned char src_plen)
{
    if(ifp->num_buffered_updates > 0 &&
       ifp->num_buffered_updates >= ifp->update_bufsize)
        flushupdates(ifp);

    if(ifp->update_bufsize == 0) {
        int n;
        assert(ifp->buffered_updates == NULL);
        /* Allocate enough space to hold a full update.  Since the
           number of installed routes will grow over time, make sure we
           have enough space to send a full-ish frame. */
        n = installed_routes_estimate() + xroutes_estimate() + 4;
        n = MAX(n, ifp->buf.size / 16);
    again:
        ifp->buffered_updates = malloc(n * sizeof(struct buffered_update));
        if(ifp->buffered_updates == NULL) {
            perror("malloc(buffered_updates)");
            if(n > 4) {
                /* Try again with a tiny buffer. */
                n = 4;
                goto again;
            }
            return;
        }
        ifp->update_bufsize = n;
        ifp->num_buffered_updates = 0;
    }

#ifdef BABELD_CODE //+++++ REPLACE +++++
    memcpy(ifp->buffered_updates[ifp->num_buffered_updates].prefix,
           prefix, 16);
#else // CEFBABELD
    memcpy(ifp->buffered_updates[ifp->num_buffered_updates].prefix,
           prefix, plen);
#endif //----- REPLACE -----
    ifp->buffered_updates[ifp->num_buffered_updates].plen = plen;
    memcpy(ifp->buffered_updates[ifp->num_buffered_updates].src_prefix,
           src_prefix, 16);
    ifp->buffered_updates[ifp->num_buffered_updates].src_plen = src_plen;
    ifp->num_buffered_updates++;
}

/* Full wildcard update with prefix == src_prefix == NULL,
   Standard wildcard update with prefix == NULL && src_prefix != NULL,
   Specific wildcard update with prefix != NULL && src_prefix == NULL. */
void
send_update(struct interface *ifp, int urgent,
#ifdef BABELD_CODE //+++++ REPLACE +++++
            const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
            const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
            const unsigned char *src_prefix, unsigned char src_plen)
{
    if(ifp == NULL) {
        struct interface *ifp_aux;
        struct babel_route *route;
        FOR_ALL_INTERFACES(ifp_aux)
            send_update(ifp_aux, urgent, prefix, plen, src_prefix, src_plen);
        if(prefix) {
            /* Since flushupdates only deals with non-wildcard interfaces, we
               need to do this now. */
            route = find_installed_route(prefix, plen, src_prefix, src_plen);
            if(route && route_metric(route) < INFINITY)
                satisfy_request(prefix, plen, src_prefix, src_plen,
                                route->src->seqno, route->src->id, NULL);
        }
        return;
    }

    if(!if_up(ifp))
        return;

    if(prefix && src_prefix) {
        debugf("Sending update to %s for %s from %s.\n",
#ifdef BABELD_CODE //+++++ REPLACE +++++
               ifp->name, format_prefix(prefix, plen),
#else // CEFBABELD
               ifp->name, format_cefore_prefix(prefix, plen),
#endif //----- REPLACE -----
               format_prefix(src_prefix, src_plen));
        buffer_update(ifp, prefix, plen, src_prefix, src_plen);
    } else if(prefix || src_prefix) {
        struct route_stream *routes;
        send_self_update(ifp);
#ifdef BABELD_CODE //+++++ REPLACE +++++
        debugf("Sending update to %s for any.\n", ifp->name);
#else // CEFBABELD
        if(prefix){
            debugf("Sending update to %s for %s.\n",
                   ifp->name, format_cefore_prefix(prefix, plen));
        } else {
            debugf("Sending update to %s for any.\n", ifp->name);
        }
#endif //----- REPLACE -----
        routes = route_stream(ROUTE_INSTALLED);
        if(routes) {
            while(1) {
                int is_ss;
                struct babel_route *route = route_stream_next(routes);
                if(route == NULL)
                    break;
                is_ss = !is_default(route->src->src_prefix,
                                    route->src->src_plen);
                if((src_prefix && is_ss) || (prefix && !is_ss))
                    continue;
                buffer_update(ifp, route->src->prefix, route->src->plen,
                              route->src->src_prefix, route->src->src_plen);
            }
            route_stream_done(routes);
        } else {
            fprintf(stderr, "Couldn't allocate route stream.\n");
        }
        set_timeout(&ifp->update_timeout, ifp->update_interval);
        ifp->last_update_time = now.tv_sec;
    } else {
        send_update(ifp, urgent, NULL, 0, zeroes, 0);
        send_update(ifp, urgent, zeroes, 0, NULL, 0);
    }
    schedule_update_flush(ifp, urgent);
}

void
send_update_resend(struct interface *ifp,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                   const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                   const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                   const unsigned char *src_prefix, unsigned char src_plen)
{
    assert(prefix != NULL);

    send_update(ifp, 1, prefix, plen, src_prefix, src_plen);
    record_resend(RESEND_UPDATE, prefix, plen, src_prefix, src_plen,
                  0, NULL, NULL, resend_delay);
}

void
buffer_wildcard_retraction(struct buffered *buf, struct interface *ifp)
{
#ifndef BABELD_CODE //+++++ ADD +++++
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |    Type = 8   |            Length             |      MBZ1     |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |     Flags     |              Plen             |      MBZ2     |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |             Interval          |            Seqno              |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |             Metric            |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */
#endif //----- ADD -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
    start_message(buf, ifp, MESSAGE_UPDATE, 10);
    accumulate_byte(buf, 0);
    accumulate_byte(buf, 0);
    accumulate_byte(buf, 0);
    accumulate_byte(buf, 0);
    accumulate_short(buf, 0xFFFF);
    accumulate_short(buf, myseqno);
    accumulate_short(buf, 0xFFFF);
    end_message(buf, MESSAGE_UPDATE, 10);
#else // CEFBABELD
#ifdef DEB_UPDATE   //+++++ for DEB +++++
    fprintf(stderr, "\n <<<<< Send WILDCARD-UPDATE\n\n");
#endif              //----- for DEB -----
    start_message(buf, ifp, MESSAGE_UPDATE, 11);
    accumulate_byte(buf, 0);
    accumulate_byte(buf, 0);
    accumulate_short(buf, 0);
    accumulate_byte(buf, 0);
    accumulate_short(buf, 0xFFFF);
    accumulate_short(buf, myseqno);
    accumulate_short(buf, 0xFFFF);
    end_message(buf, MESSAGE_UPDATE, 11);
#endif //----- REPLACE -----

    buf->have_id = 0;
}


void
send_wildcard_retraction(struct interface *ifp)
{
    if(ifp == NULL) {
        struct interface *ifp_aux;
        FOR_ALL_INTERFACES(ifp_aux)
            send_wildcard_retraction(ifp_aux);
        return;
    }

    if(!if_up(ifp))
        return;

    if((ifp->flags & IF_UNICAST) != 0) {
        struct neighbour *neigh;
        FOR_ALL_NEIGHBOURS(neigh) {
            if(neigh->ifp == ifp) {
                buffer_wildcard_retraction(&neigh->buf, neigh->ifp);
            }
        }
    } else {
        buffer_wildcard_retraction(&ifp->buf, ifp);
    }
}

void
update_myseqno()
{
    myseqno = seqno_plus(myseqno, 1);
    seqno_time = now;
}

void
send_self_update(struct interface *ifp)
{
    struct xroute_stream *xroutes;
    if(ifp == NULL) {
        struct interface *ifp_aux;
        FOR_ALL_INTERFACES(ifp_aux) {
            if(!if_up(ifp_aux))
                continue;
            send_self_update(ifp_aux);
        }
        return;
    }

    debugf("Sending self update to %s.\n", ifp->name);
    xroutes = xroute_stream();
    if(xroutes) {
        while(1) {
            struct xroute *xroute = xroute_stream_next(xroutes);
            if(xroute == NULL) break;
            send_update(ifp, 0, xroute->prefix, xroute->plen,
                        xroute->src_prefix, xroute->src_plen);
        }
        xroute_stream_done(xroutes);
    } else {
        fprintf(stderr, "Couldn't allocate xroute stream.\n");
    }
}

void
buffer_ihu(struct buffered *buf, struct interface *ifp, unsigned short rxcost,
           unsigned short interval, const unsigned char *address,
           int rtt_data, unsigned int t1, unsigned int t2)
{
    int msglen, ll;

#ifndef BABELD_CODE //+++++ ADD +++++
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |    Type = 5   |            Length             |       AE      |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |            Rxcost             |          Interval             |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |       Address...
        +-+-+-+-+-+-+-+-+-+-+-+-
    */
#endif //----- ADD -----
    
    ll = linklocal(address);
#ifdef BABELD_CODE //+++++ REPLACE +++++
    msglen = (ll ? 14 : 22) + (rtt_data ? 10 : 0);
#else // CEFBABELD
    msglen = (ll ? 13 : 21) + (rtt_data ? 10 : 0);
#endif //----- REPLACE -----

    start_message(buf, ifp, MESSAGE_IHU, msglen);
    accumulate_byte(buf, ll ? 3 : 2);
#ifdef BABELD_CODE //+++++ REPLACE +++++
    accumulate_byte(buf, 0);
#else // CEFBABELD
    ;
#endif //----- REPLACE -----
    accumulate_short(buf, rxcost);
    accumulate_short(buf, interval);
    if(ll)
        accumulate_bytes(buf, address + 8, 8);
    else
        accumulate_bytes(buf, address, 16);
    if(rtt_data) {
        accumulate_byte(buf, SUBTLV_TIMESTAMP);
        accumulate_byte(buf, 8);
        accumulate_int(buf, t1);
        accumulate_int(buf, t2);
    }
    end_message(buf, MESSAGE_IHU, msglen);
}


void
send_ihu(struct neighbour *neigh, struct interface *ifp)
{
    int rxcost, interval;
    int send_rtt_data;
    int unicast;
    
    if(neigh == NULL && ifp == NULL) {
        struct interface *ifp_aux;
        FOR_ALL_INTERFACES(ifp_aux) {
            if(if_up(ifp_aux))
                send_ihu(NULL, ifp_aux);
        }
        return;
    }

    if(neigh == NULL) {
        struct neighbour *ngh;
        FOR_ALL_NEIGHBOURS(ngh) {
            if(ngh->ifp == ifp)
                send_ihu(ngh, ifp);
        }
        return;
    }


    if(ifp && neigh->ifp != ifp)
        return;

    ifp = neigh->ifp;
    if(!if_up(ifp))
        return;

    rxcost = neighbour_rxcost(neigh);
    interval = (ifp->hello_interval * 3 + 9) / 10;

    debugf("Sending ihu %d on %s to %s.\n",
           rxcost,
           neigh->ifp->name,
           format_address(neigh->address));

    /* If we already have unicast data buffered for this peer, piggyback
       the IHU.  Only do that if RFC 6126 compatibility is disabled, since
       doing that might require sending an unscheduled unicast Hello. */
    unicast = !!(ifp->flags & IF_UNICAST) ||
        (neigh->buf.len > 0 && !(ifp->flags & IF_RFC6126));


    if(!!(ifp->flags & IF_TIMESTAMPS) != 0 && neigh->hello_send_us &&
       /* Checks whether the RTT data is not too old to be sent. */
       timeval_minus_msec(&now, &neigh->hello_rtt_receive_time) < 1000000) {
        send_rtt_data = 1;
    } else {
        neigh->hello_send_us = 0;
        send_rtt_data = 0;
    }

    if(send_rtt_data) {
        /* Ensure that there is a Hello in the same packet. */
        ensure_space(unicast ? &neigh->buf : &ifp->buf, ifp, 14 + 16);
        if(unicast)
            send_unicast_hello(neigh, 0, 0);
        else
            send_multicast_hello(ifp, 0, 0);
    }

    buffer_ihu(unicast ? &neigh->buf : &ifp->buf,
               ifp, rxcost, interval, neigh->address,
               send_rtt_data, neigh->hello_send_us,
               time_us(neigh->hello_rtt_receive_time));

}

/* Send IHUs to all marginal neighbours */
void
send_marginal_ihu(struct interface *ifp)
{
    struct neighbour *neigh;
    FOR_ALL_NEIGHBOURS(neigh) {
        if(ifp && neigh->ifp != ifp)
            continue;
        if(neigh->txcost >= 384 || (neigh->hello.reach & 0xF000) != 0xF000)
            send_ihu(neigh, ifp);
    }
}

/* Standard wildcard request with prefix == NULL && src_prefix == zeroes,
   Specific wildcard request with prefix == zeroes && src_prefix == NULL. */
static void
send_request(struct buffered *buf, struct interface *ifp,
#ifdef BABELD_CODE //+++++ REPLACE +++++
             const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
             const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
             const unsigned char *src_prefix, unsigned char src_plen)
{
#ifdef BABELD_CODE //+++++ DEL +++++
    int v4, pb, spb, len;
#endif //----- DEL -----
    int is_ss = !is_default(src_prefix, src_plen);

#ifndef BABELD_CODE //+++++ ADD +++++
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |    Type = 9   |            Length             |     PE        |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |              Plen             |        Name Prefix...         /
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        
        CAE     0   wildcart
                1   specified
    */
#endif //----- ADD -----

    if(is_ss && (ifp->flags & IF_RFC6126) != 0)
        return;

    if(!prefix) {
        assert(!src_prefix);
        debugf("sending request for any.\n");
#ifdef BABELD_CODE //+++++ REPLACE +++++
        start_message(buf, ifp, MESSAGE_REQUEST, 2);
        accumulate_byte(buf, 0);
        accumulate_byte(buf, 0);
        end_message(buf, MESSAGE_REQUEST, 2);
#else // CEFBABELD
#ifdef DEB_REQ   //+++++ for DEB +++++
    fprintf(stderr, "<<<<< Sending request for any.\n");
#endif              //----- for DEB -----
        start_message(buf, ifp, MESSAGE_REQUEST, 3);
        accumulate_byte(buf, 0);
        accumulate_short(buf, 0);
        end_message(buf, MESSAGE_REQUEST, 3);
#endif //----- REPLACE -----
        return;
    }

#ifdef BABELD_CODE //+++++ REPLACE +++++
    debugf("sending request for %s from %s.\n",
           format_prefix(prefix, plen),
           format_prefix(src_prefix, src_plen));
#else // CEFBABELD
    debugf("sending request for %s from %s.\n",
           format_cefore_prefix(prefix, plen),
           format_prefix(src_prefix, src_plen));
#endif //----- REPLACE -----

#ifdef BABELD_CODE //+++++ DEL +++++
    v4 = plen >= 96 && v4mapped(prefix);
    pb = v4 ? ((plen - 96) + 7) / 8 : (plen + 7) / 8;
    spb = v4 ? ((src_plen - 96) + 7) / 8 : (src_plen + 7) / 8;
    len = 2 + pb + (is_ss ? 3 + spb : 0);
#endif //----- DEL -----

#ifdef BABELD_CODE //+++++ REPLACE +++++
    start_message(buf, ifp, MESSAGE_REQUEST, len);
    accumulate_byte(buf, v4 ? 1 : 2);
    accumulate_byte(buf, v4 ? plen - 96 : plen);
    if(v4)
        accumulate_bytes(buf, prefix + 12, pb);
    else
        accumulate_bytes(buf, prefix, pb);
    if(is_ss) {
        accumulate_byte(buf, SUBTLV_SOURCE_PREFIX);
        accumulate_byte(buf, 1 + spb);
        accumulate_byte(buf, v4 ? src_plen - 96 : src_plen);
        if(v4)
            accumulate_bytes(buf, src_prefix + 12, spb);
        else
            accumulate_bytes(buf, src_prefix, spb);
    }
    end_message(buf, MESSAGE_REQUEST, len);
#else // CEFBABELD
#ifdef DEB_REQ   //+++++ for DEB +++++
    fprintf(stderr, "<<<<< Sending request for %s from %s.\n",
           format_cefore_prefix(prefix, plen),
           format_prefix(src_prefix, src_plen));
#endif              //----- for DEB -----
    start_message(buf, ifp, MESSAGE_REQUEST, 3 + plen);
    accumulate_byte(buf, 1);
    accumulate_short(buf, plen);
    accumulate_bytes(buf, prefix, plen);
    end_message(buf, MESSAGE_REQUEST, 3 + plen);
#endif //----- REPLACE -----

}

void
send_multicast_request(struct interface *ifp,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                       const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                       const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                       const unsigned char *src_prefix, unsigned char src_plen)
{
    if(ifp == NULL) {
        struct interface *ifp_auxn;
        FOR_ALL_INTERFACES(ifp_auxn) {
            if(!if_up(ifp_auxn))
                continue;
            send_multicast_request(ifp_auxn, prefix, plen, src_prefix, src_plen);
        }
        return;
    }

    if(!if_up(ifp))
        return;

    /* make sure any buffered updates go out before this request. */
    flushupdates(ifp);

    if((ifp->flags & IF_UNICAST) != 0) {
        struct neighbour *neigh;
        FOR_ALL_NEIGHBOURS(neigh) {
            if(neigh->ifp == ifp) {
                send_request(&neigh->buf, ifp, prefix, plen,
                             src_prefix, src_plen);
            }
        }
    } else {
        send_request(&ifp->buf, ifp, prefix, plen, src_prefix, src_plen);
    }
}

void
send_unicast_request(struct neighbour *neigh,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                     const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                     const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                     const unsigned char *src_prefix, unsigned char src_plen)
{
    if(!if_up(neigh->ifp))
        return;

    flushupdates(neigh->ifp);

    send_request(&neigh->buf, neigh->ifp, prefix, plen, src_prefix, src_plen);
}

static void
send_multihop_request(struct buffered *buf, struct interface *ifp,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                      const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                      const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                      const unsigned char *src_prefix, unsigned char src_plen,
                      unsigned short seqno, const unsigned char *id,
                      unsigned short hop_count)
{
#ifdef BABELD_CODE //+++++ DEL +++++
    int v4, pb, spb, len;
#endif //----- DEL -----
    int is_ss = !is_default(src_prefix, src_plen);

#ifndef BABELD_CODE //+++++ ADD +++++
    /*
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |    Type = 10  |            Length             |      MBZ      |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |              Plen             |            Seqno              |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |  Hop Count    |                 Reserved                      |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |                                                               |
        +                           Router-Id                           +
        |                                                               |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |   Name Prefix...
        +-+-+-+-+-+-+-+-+-+-+
    */
#endif //----- ADD -----
    if(is_ss && (ifp->flags & IF_RFC6126) != 0)
        return;

#ifdef BABELD_CODE //+++++ REPLACE +++++
    debugf("Sending request (%d) for %s.\n",
           hop_count, format_prefix(prefix, plen));
#else // CEFBABELD
    debugf("Sending request (%d) for %s.\n",
           hop_count, format_cefore_prefix(prefix, plen));
#endif //----- REPLACE -----

#ifdef BABELD_CODE //+++++ REPLACE +++++
    v4 = plen >= 96 && v4mapped(prefix);
    pb = v4 ? ((plen - 96) + 7) / 8 : (plen + 7) / 8;
    spb = v4 ? ((src_plen - 96) + 7) / 8 : (src_plen + 7) / 8;
    len = 6 + 8 + pb + (is_ss ? 3 + spb : 0);

    start_message(buf, ifp, MESSAGE_MH_REQUEST, len);
    accumulate_byte(buf, v4 ? 1 : 2);
    accumulate_byte(buf, v4 ? plen - 96 : plen);
    accumulate_short(buf, seqno);
    accumulate_byte(buf, hop_count);
    accumulate_byte(buf, v4 ? src_plen - 96 : src_plen);
    accumulate_bytes(buf, id, 8);
    if(prefix) {
        if(v4)
            accumulate_bytes(buf, prefix + 12, pb);
        else
            accumulate_bytes(buf, prefix, pb);
    }
    if(is_ss) {
        accumulate_byte(buf, SUBTLV_SOURCE_PREFIX);
        accumulate_byte(buf, 1 + spb);
        accumulate_byte(buf, v4 ? src_plen - 96 : src_plen);
        if(v4)
            accumulate_bytes(buf, src_prefix + 12, spb);
        else
            accumulate_bytes(buf, src_prefix, spb);
    }
    end_message(buf, MESSAGE_MH_REQUEST, len);
#else // CEFBABELD
#ifdef DEB_REQ   //+++++ for DEB +++++
    fprintf(stderr, "<<<<< Send MH_REQUEST prefix=%s (plen=%u, seqno=%u, hop_count=%u id=%s)\n"
                  , format_cefore_prefix(prefix, plen), plen, seqno, hop_count
                  , format_eui64(id));
#endif              //----- for DEB -----
    start_message(buf, ifp, MESSAGE_MH_REQUEST, 17+plen);

    accumulate_byte(buf, 0);
    accumulate_short(buf, plen);
    accumulate_short(buf, seqno);
    accumulate_byte(buf, hop_count);
    accumulate_byte(buf, 0);
    accumulate_byte(buf, 0);
    accumulate_byte(buf, 0);
    accumulate_bytes(buf, id, 8);
    accumulate_bytes(buf, prefix, plen);
    
    end_message(buf, MESSAGE_MH_REQUEST, 17+plen);
#endif //----- REPLACE -----
}

void
send_multicast_multihop_request(struct interface *ifp,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                      const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                      const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                      const unsigned char *src_prefix, unsigned char src_plen,
                      unsigned short seqno, const unsigned char *id,
                      unsigned short hop_count)
{
    if(ifp == NULL) {
        struct interface *ifp_aux;
        FOR_ALL_INTERFACES(ifp_aux) {
            if(!if_up(ifp_aux))
                continue;
            send_multicast_multihop_request(ifp_aux,
                                            prefix, plen, src_prefix, src_plen,
                                            seqno, id, hop_count);
        }
        return;
    }

    flushupdates(ifp);

    if(!if_up(ifp))
        return;

    if((ifp->flags & IF_UNICAST) != 0) {
            struct neighbour *neigh;
            FOR_ALL_NEIGHBOURS(neigh) {
                if(neigh->ifp == ifp) {
                    send_multihop_request(&neigh->buf, neigh->ifp,
                                          prefix, plen,
                                          src_prefix, src_plen,
                                          seqno, id, hop_count);
                }
            }
    } else {
        send_multihop_request(&ifp->buf, ifp,
                              prefix, plen,
                              src_prefix, src_plen,
                              seqno, id, hop_count);
    }
}

void
send_unicast_multihop_request(struct neighbour *neigh,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                              const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                              const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                              const unsigned char *src_prefix,
                              unsigned char src_plen,
                              unsigned short seqno, const unsigned char *id,
                              unsigned short hop_count)
{
    flushupdates(neigh->ifp);
    send_multihop_request(&neigh->buf, neigh->ifp,
                          prefix, plen, src_prefix, src_plen,
                          seqno, id, hop_count);
}

/* Send a request to a well-chosen neighbour and resend.  If there is no
   good neighbour, send over multicast but only once. */
void
#ifdef BABELD_CODE //+++++ REPLACE +++++
send_request_resend(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
send_request_resend(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                    const unsigned char *src_prefix, unsigned char src_plen,
                    unsigned short seqno, unsigned char *id)
{
    struct babel_route *route;

    route = find_best_route(prefix, plen, src_prefix, src_plen, 0, NULL);

    if(route) {
        struct neighbour *neigh = route->neigh;
        send_unicast_multihop_request(neigh, prefix, plen, src_prefix, src_plen,
                                      seqno, id, 127);
        record_resend(RESEND_REQUEST, prefix, plen, src_prefix, src_plen, seqno,
                      id, neigh->ifp, resend_delay);
    } else {
        struct interface *ifp;
        FOR_ALL_INTERFACES(ifp) {
        if(!if_up(ifp)) continue;
            send_multihop_request(&ifp->buf, ifp,
                                  prefix, plen, src_prefix, src_plen,
                                  seqno, id, 127);
    }
    }
}

void
handle_request(struct neighbour *neigh, const unsigned char *prefix,
#ifdef BABELD_CODE //+++++ REPLACE +++++
               unsigned char plen,
#else // CEFBABELD
               uint16_t plen,
#endif //----- REPLACE -----
               const unsigned char *src_prefix, unsigned char src_plen,
               unsigned char hop_count,
               unsigned short seqno, const unsigned char *id)
{
    struct xroute *xroute;
    struct babel_route *route;
    struct neighbour *successor = NULL;

    xroute = find_xroute(prefix, plen, src_prefix, src_plen);
    route = find_installed_route(prefix, plen, src_prefix, src_plen);

#ifdef BABELD_CODE //+++++ REPLACE +++++
    if(xroute && (!route || xroute->metric <= kernel_metric)) {
#else // CEFBABELD
    if(xroute && !route) {
#endif //----- REPLACE -----
        if(hop_count > 0 && memcmp(id, myid, 8) == 0) {
            if(seqno_compare(seqno, myseqno) > 0) {
                if(seqno_minus(seqno, myseqno) > 100) {
                    /* Hopelessly out-of-date request */
                    return;
                }
                update_myseqno();
            }
        }
        send_update(neigh->ifp, 1, prefix, plen, src_prefix, src_plen);
        return;
    }

#ifdef BABELD_CODE //+++++ REPLACE +++++
    if(route &&
       (memcmp(id, route->src->id, 8) != 0 ||
        seqno_compare(seqno, route->seqno) <= 0)) {
        send_update(neigh->ifp, 1, prefix, plen, src_prefix, src_plen);
        return;
    }
#else // CEFBABELD
    if (route_ctrl_type != ROUTE_CTRL_TYPE_MM) {
        if(route &&
           (memcmp(id, route->src->id, 8) != 0 ||
            seqno_compare(seqno, route->seqno) <= 0)) {
            send_update(neigh->ifp, 1, prefix, plen, src_prefix, src_plen);
            return;
        }
    } else {
        if(route &&
            memcmp(id, route->src->id, 8) == 0 &&
            seqno_compare(seqno, route->seqno) <= 0) {
            send_update(neigh->ifp, 1, prefix, plen, src_prefix, src_plen);
            return;
        }
    }
#endif //----- REPLACE -----

    if(hop_count <= 1)
        return;

    if(route && memcmp(id, route->src->id, 8) == 0 &&
       seqno_minus(seqno, route->seqno) > 100) {
        /* Hopelessly out-of-date */
        return;
    }

    if(request_redundant(neigh->ifp, prefix, plen, src_prefix, src_plen,
                         seqno, id))
        return;

    /* Let's try to forward this request. */
    if(route && route_metric(route) < INFINITY)
        successor = route->neigh;

    if(!successor || successor == neigh) {
        /* We were about to forward a request to its requestor.  Try to
           find a different neighbour to forward the request to. */
        struct babel_route *other_route;

        other_route = find_best_route(prefix, plen, src_prefix, src_plen,
                                      0, neigh);
        if(other_route && route_metric(other_route) < INFINITY)
            successor = other_route->neigh;
    }

    if(!successor || successor == neigh)
        /* Give up */
        return;

    send_unicast_multihop_request(successor, prefix, plen, src_prefix, src_plen,
                                  seqno, id, hop_count - 1);
    record_resend(RESEND_REQUEST, prefix, plen, src_prefix, src_plen, seqno, id,
                  neigh->ifp, 0);
}

#ifndef BABELD_CODE //+++++ ADD for MP +++++
void
really_send_update_mp(struct interface *ifp, const unsigned char *id,
                   const unsigned char *prefix, uint16_t plen,
                   const unsigned char *src_prefix, unsigned char src_plen,
                   unsigned short seqno, unsigned short metric, unsigned short port)
{
    if(ifp == NULL) {
        struct interface *ifp_aux;
        FOR_ALL_INTERFACES(ifp_aux) {
            really_send_update_mp(ifp_aux, id, prefix, plen, src_prefix, src_plen, seqno, metric, port);
        }
        return;
    }

    if(!if_up(ifp))
        return;

    really_send_update(ifp, id, prefix, plen, src_prefix, src_plen, seqno, metric, port, NULL, -1);
}
#endif //----- ADD for MP -----


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

#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "babeld.h"
#include "util.h"
#include "interface.h"
#include "neighbour.h"
#include "resend.h"
#include "message.h"
#include "configuration.h"

struct timeval resend_time = {0, 0};
struct resend *to_resend = NULL;

static int
resend_match(struct resend *resend,
#ifdef BABELD_CODE //+++++ REPLACE +++++
             int kind, const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
             int kind, const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
             const unsigned char *src_prefix, unsigned char src_plen)
{
    return (resend->kind == kind &&
#ifdef BABELD_CODE //+++++ REPLACE +++++
            resend->plen == plen && memcmp(resend->prefix, prefix, 16) == 0 &&
#else // CEFBABELD
            resend->plen == plen && memcmp(resend->prefix, prefix, plen) == 0 &&
#endif //----- REPLACE -----
            resend->src_plen == src_plen &&
            memcmp(resend->src_prefix, src_prefix, 16) == 0);
}

/* This is called by neigh.c when a neighbour is flushed */

void
flush_resends(struct neighbour *neigh)
{
    /* Nothing for now */
}

static struct resend *
#ifdef BABELD_CODE //+++++ REPLACE +++++
find_resend(int kind, const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
find_resend(int kind, const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
            const unsigned char *src_prefix, unsigned char src_plen,
            struct resend **previous_return)
{
    struct resend *current, *previous;

    previous = NULL;
    current = to_resend;
    while(current) {
        if(resend_match(current, kind, prefix, plen, src_prefix, src_plen)) {
            if(previous_return)
                *previous_return = previous;
            return current;
        }
        previous = current;
        current = current->next;
    }

    return NULL;
}

struct resend *
#ifdef BABELD_CODE //+++++ REPLACE +++++
find_request(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
find_request(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
             const unsigned char *src_prefix, unsigned char src_plen,
             struct resend **previous_return)
{
    return find_resend(RESEND_REQUEST, prefix, plen, src_prefix, src_plen,
                       previous_return);
}

int
#ifdef BABELD_CODE //+++++ REPLACE +++++
record_resend(int kind, const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
record_resend(int kind, const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
              const unsigned char *src_prefix, unsigned char src_plen,
              unsigned short seqno, const unsigned char *id,
              struct interface *ifp, int delay)
{
    struct resend *resend;
#ifdef BABELD_CODE //+++++ DEL +++++
    unsigned int ifindex = ifp ? ifp->ifindex : 0;

    if((kind == RESEND_REQUEST &&
        input_filter(NULL, prefix, plen, src_prefix, src_plen, NULL,
                     ifindex) >=
        INFINITY) ||
       (kind == RESEND_UPDATE &&
        output_filter(NULL, prefix, plen, src_prefix, src_plen, ifindex) >=
        INFINITY))
        return 0;
#endif //----- DEL -----

    if(delay >= 0xFFFF)
        delay = 0xFFFF;

    resend = find_resend(kind, prefix, plen, src_prefix, src_plen, NULL);
    if(resend) {
        if(resend->delay && delay)
            resend->delay = MIN(resend->delay, delay);
        else if(delay)
            resend->delay = delay;
        resend->time = now;
        resend->max = RESEND_MAX;
        if(id && memcmp(resend->id, id, 8) == 0 &&
           seqno_compare(resend->seqno, seqno) > 0) {
            return 0;
        }
        if(id)
            memcpy(resend->id, id, 8);
        else
            memset(resend->id, 0, 8);
        resend->seqno = seqno;
        if(resend->ifp != ifp)
            resend->ifp = NULL;
    } else {
        resend = calloc(1, sizeof(struct resend));
        if(resend == NULL)
            return -1;
        resend->kind = kind;
        resend->max = RESEND_MAX;
        resend->delay = delay;
#ifdef BABELD_CODE //+++++ REPLACE +++++
        memcpy(resend->prefix, prefix, 16);
#else // CEFBABELD
        memcpy(resend->prefix, prefix, plen);
#endif //----- REPLACE -----
        resend->plen = plen;
        memcpy(resend->src_prefix, src_prefix, 16);
        resend->src_plen = src_plen;
        resend->seqno = seqno;
        if(id)
            memcpy(resend->id, id, 8);
        resend->ifp = ifp;
        resend->time = now;
        resend->next = to_resend;
        to_resend = resend;
    }

    if(resend->delay) {
        struct timeval timeout;
        timeval_add_msec(&timeout, &resend->time, resend->delay);
        timeval_min(&resend_time, &timeout);
    }
    return 1;
}

static int
resend_expired(struct resend *resend)
{
    switch(resend->kind) {
    case RESEND_REQUEST:
        return timeval_minus_msec(&now, &resend->time) >= REQUEST_TIMEOUT;
    default:
        return resend->max <= 0;
    }
}

int
#ifdef BABELD_CODE //+++++ REPLACE +++++
unsatisfied_request(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
unsatisfied_request(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                    const unsigned char *src_prefix, unsigned char src_plen,
                    unsigned short seqno, const unsigned char *id)
{
    struct resend *request;

    request = find_request(prefix, plen, src_prefix, src_plen, NULL);
    if(request == NULL || resend_expired(request))
        return 0;

    if(memcmp(request->id, id, 8) != 0 ||
       seqno_compare(request->seqno, seqno) <= 0)
        return 1;

    return 0;
}

/* Determine whether a given request should be forwarded. */
int
request_redundant(struct interface *ifp,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                  const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                  const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                  const unsigned char *src_prefix, unsigned char src_plen,
                  unsigned short seqno, const unsigned char *id)
{
    struct resend *request;

    request = find_request(prefix, plen, src_prefix, src_plen, NULL);
    if(request == NULL || resend_expired(request))
        return 0;

    if(memcmp(request->id, id, 8) == 0 &&
       seqno_compare(request->seqno, seqno) > 0)
        return 0;

    if(request->ifp != NULL && request->ifp != ifp)
        return 0;

    if(request->max > 0)
        /* Will be resent. */
        return 1;

    if(timeval_minus_msec(&now, &request->time) <
       (ifp ? MIN(ifp->hello_interval, 1000) : 1000))
        /* Fairly recent. */
        return 1;

    return 0;
}

int
#ifdef BABELD_CODE //+++++ REPLACE +++++
satisfy_request(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
satisfy_request(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                const unsigned char *src_prefix, unsigned char src_plen,
                unsigned short seqno, const unsigned char *id,
                struct interface *ifp)
{
    struct resend *request, *previous;

    request = find_request(prefix, plen, src_prefix, src_plen, &previous);
    if(request == NULL)
        return 0;

    if(ifp != NULL && request->ifp != ifp)
        return 0;

    if(memcmp(request->id, id, 8) != 0 ||
       seqno_compare(request->seqno, seqno) <= 0) {
        /* We cannot remove the request, as we may be walking the list right
           now.  Mark it as expired, so that expire_resend will remove it. */
        request->max = 0;
        request->time.tv_sec = 0;
        recompute_resend_time();
        return 1;
    }

    return 0;
}

void
expire_resend()
{
    struct resend *current, *previous;
    int recompute = 0;

    previous = NULL;
    current = to_resend;
    while(current) {
        if(resend_expired(current)) {
            if(previous == NULL) {
                to_resend = current->next;
                free(current);
                current = to_resend;
            } else {
                previous->next = current->next;
                free(current);
                current = previous->next;
            }
            recompute = 1;
        } else {
            previous = current;
            current = current->next;
        }
    }
    if(recompute)
        recompute_resend_time();
}

void
recompute_resend_time()
{
    struct resend *request;
    struct timeval resend = {0, 0};

    request = to_resend;
    while(request) {
        if(!resend_expired(request) && request->delay > 0 && request->max > 0) {
            struct timeval timeout;
            timeval_add_msec(&timeout, &request->time, request->delay);
            timeval_min(&resend, &timeout);
        }
        request = request->next;
    }

    resend_time = resend;
}

void
do_resend()
{
    struct resend *resend;

    resend = to_resend;
    while(resend) {
        if(!resend_expired(resend) && resend->delay > 0 && resend->max > 0) {
            struct timeval timeout;
            timeval_add_msec(&timeout, &resend->time, resend->delay);
            if(timeval_compare(&now, &timeout) >= 0) {
                switch(resend->kind) {
                case RESEND_REQUEST:
                    send_multicast_multihop_request(resend->ifp,
                                                    resend->prefix, resend->plen,
                                                    resend->src_prefix,
                                                    resend->src_plen,
                                                    resend->seqno, resend->id,
                                                    127);
                    break;
                case RESEND_UPDATE:
                    send_update(resend->ifp, 1,
                                resend->prefix, resend->plen,
                                resend->src_prefix, resend->src_plen);
                    break;
                default: abort();
                }
                resend->delay = MIN(0xFFFF, resend->delay * 2);
                resend->max--;
            }
        }
        resend = resend->next;
    }
    recompute_resend_time();
}

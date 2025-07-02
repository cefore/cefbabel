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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "babeld.h"
#include "kernel.h"
#include "interface.h"
#include "neighbour.h"
#include "message.h"
#include "source.h"
#include "route.h"
#include "xroute.h"
#include "util.h"
#include "configuration.h"
#include "local.h"

static struct xroute *xroutes;
static int numxroutes = 0, maxxroutes = 0;

static int
#ifdef BABELD_CODE //+++++ REPLACE +++++
xroute_compare(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
xroute_compare(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
               const unsigned char *src_prefix, unsigned char src_plen,
               const struct xroute *xroute)
{
    int rc;

    if(plen < xroute->plen)
        return -1;
    if(plen > xroute->plen)
        return 1;

#ifdef BABELD_CODE //+++++ REPLACE +++++
    rc = memcmp(prefix, xroute->prefix, 16);
#else // CEFBABELD
    rc = memcmp(prefix, xroute->prefix, xroute->plen);
#endif //----- REPLACE -----
    if(rc != 0)
        return rc;

#ifndef BABELD_CODE //+++++ ADD +++++
    if(src_plen == 0 && xroute->src_plen == 0){
        return 0;
    }
#endif //----- ADD -----
    if(src_plen < xroute->src_plen)
        return -1;
    if(src_plen > xroute->src_plen)
        return 1;

    rc = memcmp(src_prefix, xroute->src_prefix, 16);
    if(rc != 0)
        return rc;

    return 0;
}

static int
#ifdef BABELD_CODE //+++++ REPLACE +++++
find_xroute_slot(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
find_xroute_slot(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                 const unsigned char *src_prefix, unsigned char src_plen,
                 int *new_return)
{
    int p, m, g, c;

    if(numxroutes < 1) {
        if(new_return)
            *new_return = 0;
        return -1;
    }

    p = 0; g = numxroutes - 1;

    do {
        m = (p + g) / 2;
        c = xroute_compare(prefix, plen, src_prefix, src_plen, &xroutes[m]);
        if(c == 0)
            return m;
        else if(c < 0)
            g = m - 1;
        else
            p = m + 1;
    } while(p <= g);
    
    if(new_return)
        *new_return = p;

    return -1;
}


struct xroute *
#ifdef BABELD_CODE //+++++ REPLACE +++++
find_xroute(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
find_xroute(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
            const unsigned char *src_prefix, unsigned char src_plen)
{
    int i = find_xroute_slot(prefix, plen, src_prefix, src_plen, NULL);
    if(i >= 0)
        return &xroutes[i];

    return NULL;
}

int
#ifdef BABELD_CODE //+++++ REPLACE +++++
add_xroute(unsigned char prefix[16], unsigned char plen,
#else // CEFBABELD
add_xroute(unsigned char prefix[NAME_PREFIX_LEN], uint16_t plen,
#endif //----- REPLACE -----
           unsigned char src_prefix[16], unsigned char src_plen,
           unsigned short metric, unsigned int ifindex, int proto)
{
    int n = -1;
    int i = find_xroute_slot(prefix, plen, src_prefix, src_plen, &n);

    if(i >= 0)
        return -1;

    if(numxroutes >= maxxroutes) {
        struct xroute *new_xroutes;
        int num = maxxroutes < 1 ? 8 : 2 * maxxroutes;
        new_xroutes = realloc(xroutes, num * sizeof(struct xroute));
        if(new_xroutes == NULL)
            return -1;
        maxxroutes = num;
        xroutes = new_xroutes;
    }

    if(n < numxroutes)
        memmove(xroutes + n + 1, xroutes + n,
                (numxroutes - n) * sizeof(struct xroute));
    numxroutes++;

#ifdef BABELD_CODE //+++++ REPLACE +++++
    memcpy(xroutes[n].prefix, prefix, 16);
#else // CEFBABELD
    memcpy(xroutes[n].prefix, prefix, plen);
#endif //----- REPLACE -----
    xroutes[n].plen = plen;
    memcpy(xroutes[n].src_prefix, src_prefix, 16);
    xroutes[n].src_plen = src_plen;
    xroutes[n].metric = metric;
    xroutes[n].ifindex = ifindex;
    xroutes[n].proto = proto;
#ifdef BABELD_CODE //+++++ REPLACE +++++
    local_notify_xroute(&xroutes[n], LOCAL_ADD);
#else // CEFBABELD
//    local_notify_xroute(&xroutes[n], LOCAL_ADD);
#endif //----- REPLACE -----
    return 1;
}

void
flush_xroute(struct xroute *xroute)
{
    int i;

    i = xroute - xroutes;
    assert(i >= 0 && i < numxroutes);

#ifdef BABELD_CODE //+++++ REPLACE +++++
    local_notify_xroute(xroute, LOCAL_FLUSH);
#else // CEFBABELD
//    local_notify_xroute(xroute, LOCAL_FLUSH);
#endif //----- REPLACE -----

    if(i != numxroutes - 1)
        memmove(xroutes + i, xroutes + i + 1,
                (numxroutes - i - 1) * sizeof(struct xroute));
    numxroutes--;
    VALGRIND_MAKE_MEM_UNDEFINED(xroutes + numxroutes, sizeof(struct xroute));

    if(numxroutes == 0) {
        free(xroutes);
        xroutes = NULL;
        maxxroutes = 0;
    } else if(maxxroutes > 8 && numxroutes < maxxroutes / 4) {
        struct xroute *new_xroutes;
        int n = maxxroutes / 2;
        new_xroutes = realloc(xroutes, n * sizeof(struct xroute));
        if(new_xroutes == NULL)
            return;
        xroutes = new_xroutes;
        maxxroutes = n;
    }
}

/* Returns an overestimate of the number of xroutes. */
int
xroutes_estimate()
{
    return numxroutes;
}

struct xroute_stream {
    int index;
};

struct
xroute_stream *
xroute_stream()
{
    struct xroute_stream *stream = calloc(1, sizeof(struct xroute_stream));
    if(stream == NULL)
        return NULL;

    return stream;
}


struct xroute *
xroute_stream_next(struct xroute_stream *stream)
{
    if(stream->index < numxroutes)
        return &xroutes[stream->index++];
    else
        return NULL;
}

void
xroute_stream_done(struct xroute_stream *stream)
{
    free(stream);
}

static int
filter_route(struct kernel_route *route, void *data) {
    void **args = (void**)data;
    int maxroutes = *(int*)args[0];
    struct kernel_route *routes = (struct kernel_route *)args[1];
    int *found = (int*)args[2];

    if(*found >= maxroutes)
        return -1;

    if(martian_prefix(route->prefix, route->plen) ||
       martian_prefix(route->src_prefix, route->src_plen))
        return 0;

    routes[*found] = *route;
    ++ *found;

    return 0;
}

static int
kernel_routes(struct kernel_route *routes, int maxroutes)
{
    int found = 0;
    void *data[3] = { &maxroutes, routes, &found };
    struct kernel_filter filter = {0};
    filter.route = filter_route;
    filter.route_closure = data;

    kernel_dump(CHANGE_ROUTE, &filter);

    return found;
}

static int
filter_address(struct kernel_addr *addr, void *data) {
    void **args = (void **)data;
    int maxroutes = *(int *)args[0];
    struct kernel_route *routes = (struct kernel_route*)args[1];
    int *found = (int *)args[2];
    int ifindex = *(int*)args[3];
    int ll = args[4] ? !!*(int*)args[4] : 0;
    struct kernel_route *route = NULL;

    if(*found >= maxroutes)
        return 0;

    if(ll == !IN6_IS_ADDR_LINKLOCAL(&addr->addr))
        return 0;

    /* ifindex may be 0 -- see kernel_addresses */
    if(ifindex && addr->ifindex != ifindex)
        return 0;

    route = &routes[*found];
    memcpy(route->prefix, addr->addr.s6_addr, 16);
    route->plen = 128;
    route->metric = 0;
    route->ifindex = addr->ifindex;
    route->proto = RTPROT_BABEL_LOCAL;
    memset(route->gw, 0, 16);
    ++ *found;

    return 1;
}

/* ifindex is 0 for all interfaces.  ll indicates whether we are
   interested in link-local or global addresses. */
int
kernel_addresses(int ifindex, int ll, struct kernel_route *routes,
                 int maxroutes)
{
    int found = 0;
    void *data[5] = { &maxroutes, routes, &found, &ifindex, &ll };
    struct kernel_filter filter = {0};
    filter.addr = filter_address;
    filter.addr_closure = data;

    kernel_dump(CHANGE_ADDR, &filter);

    return found;
}

/* This must coincide with the ordering defined by xroute_compare above. */
static int
kernel_route_compare(const void *v1, const void *v2)
{
    const struct kernel_route *route1 = (struct kernel_route*)v1;
    const struct kernel_route *route2 = (struct kernel_route*)v2;
    int rc;

    if(route1->plen < route2->plen)
        return -1;
    if(route1->plen > route2->plen)
        return 1;

#ifdef BABELD_CODE //+++++ REPLACE +++++
    rc = memcmp(route1->prefix, route2->prefix, 16);
#else // CEFBABELD
    rc = memcmp(route1->prefix, route2->prefix, route1->plen);
#endif //----- REPLACE -----
    if(rc != 0)
        return rc;

    if(route1->src_plen < route2->src_plen)
        return -1;
    if(route1->src_plen > route2->src_plen)
        return 1;

    rc = memcmp(route1->src_prefix, route2->src_prefix, 16);
    if(rc != 0)
        return rc;

    return 0;
}

int
check_xroutes(int send_updates) 
{
    int i, j, change = 0, rc;
    struct kernel_route *routes;
    struct filter_result filter_result;
    int numroutes;
    static int maxroutes = 8;
    const int maxmaxroutes = 256 * 1024;

    debugf("\nChecking kernel routes.\n");

 again:
    routes = calloc(maxroutes, sizeof(struct kernel_route));
    if(routes == NULL)
        return -1;

    rc = kernel_addresses(0, 0, routes, maxroutes);
    if(rc < 0) {
        perror("kernel_addresses");
        numroutes = 0;
    } else {
        numroutes = rc;
    }

    if(numroutes >= maxroutes)
        goto resize;

    rc = kernel_routes(routes + numroutes, maxroutes - numroutes);
    if(rc < 0)
        fprintf(stderr, "Couldn't get kernel routes.\n");
    else
        numroutes += rc;

    if(numroutes >= maxroutes)
        goto resize;

    for(i = 0; i < numroutes; i++) {
        routes[i].metric = redistribute_filter(routes[i].prefix, routes[i].plen,
                                               routes[i].src_prefix,
                                               routes[i].src_plen,
                                               routes[i].ifindex,
                                               routes[i].proto,
                                               &filter_result);
        if(filter_result.src_prefix != NULL) {
            memcpy(routes[i].src_prefix, filter_result.src_prefix, 16);
            routes[i].src_plen = filter_result.src_plen;
        }
    }

    qsort(routes, numroutes, sizeof(struct kernel_route), kernel_route_compare);
    i = 0;
    j = 0;
    while(i < numroutes || j < numxroutes) {
        /* Ignore routes filtered out. */
        if(i < numroutes && routes[i].metric >= INFINITY) {
            i++;
            continue;
        }

        if(i >= numroutes)
            rc = +1;
        else if(j >= numxroutes)
            rc = -1;
        else
            rc = xroute_compare(routes[i].prefix, routes[i].plen,
                                routes[i].src_prefix, routes[i].src_plen,
                                &xroutes[j]);
        if(rc < 0) {
            /* Add route i. */
            if(!martian_prefix(routes[i].prefix, routes[i].plen) &&
               routes[i].metric < INFINITY) {
                rc = add_xroute(routes[i].prefix, routes[i].plen,
                                routes[i].src_prefix, routes[i].src_plen,
                                routes[i].metric, routes[i].ifindex,
                                routes[i].proto);
                if(rc > 0) {
                    struct babel_route *route;
                    route = find_installed_route(routes[i].prefix,
                                                 routes[i].plen,
                                                 routes[i].src_prefix,
                                                 routes[i].src_plen);
                    if(route) {
                        if(allow_duplicates < 0 ||
                           routes[i].metric < allow_duplicates)
                            uninstall_route(route);
                    }
                    if(send_updates)
                        send_update(NULL, 0, routes[i].prefix, routes[i].plen,
                                    routes[i].src_prefix, routes[i].src_plen);
                    j++;
                }
            }
            i++;
        } else if(rc > 0) {
            /* Flush xroute j. */
#ifdef BABELD_CODE //+++++ REPLACE +++++
            unsigned char prefix[16], plen;
#else // CEFBABELD
            unsigned char prefix[NAME_PREFIX_LEN], plen;
#endif //----- REPLACE -----
            unsigned char src_prefix[16], src_plen;
            struct babel_route *route;
#ifdef BABELD_CODE //+++++ REPLACE +++++
            memcpy(prefix, xroutes[i].prefix, 16);
#else // CEFBABELD
            memcpy(prefix, xroutes[i].prefix, xroutes[i].plen);
#endif //----- REPLACE -----
            
            plen = xroutes[i].plen;
            memcpy(src_prefix, xroutes[i].src_prefix, 16);
            src_plen = xroutes[i].src_plen;
            flush_xroute(&xroutes[j]);
            route = find_best_route(prefix, plen, src_prefix, src_plen,
                                    1, NULL);
            if(route != NULL) {
                install_route(route);
                send_update(NULL, 0, prefix, plen, src_prefix, src_plen);
            } else {
                send_update_resend(NULL, prefix, plen, src_prefix, src_plen);
            }
        } else {
            if(routes[i].metric != xroutes[j].metric ||
               routes[i].proto != xroutes[j].proto) {
                xroutes[j].metric = routes[i].metric;
                xroutes[j].proto = routes[i].proto;
                local_notify_xroute(&xroutes[j], LOCAL_CHANGE);
                if(send_updates)
                    send_update(NULL, 0, xroutes[j].prefix, xroutes[j].plen,
                                xroutes[j].src_prefix, xroutes[j].src_plen);
            }
            i++;
            j++;
        }
    }

    free(routes);
    /* Set up maxroutes for the next call. */
    maxroutes = MIN(numroutes + 8, maxmaxroutes);
    return change;

 resize:
    free(routes);
    if(maxroutes >= maxmaxroutes)
        return -1;
    maxroutes = MIN(maxmaxroutes, 2 * maxroutes);
    goto again;
}
#ifndef BABELD_CODE //+++++ ADD for STAT +++++
/**************************************************************************************************/    
/***** STAT Common Functions                                                                  *****/
/**************************************************************************************************/    
int
exist_xroute()
{
    if (numxroutes > 0){
        return 1;
    } else {
        return 0;
    }
}
#endif //----- ADD for STAT -----
	
#ifndef BABELD_CODE //+++++ ADD for MP +++++
/**************************************************************************************************/    
/***** Multi path Common Functions                                                            *****/
/**************************************************************************************************/    
static int
xroute_compare_mp(const unsigned char *prefix, uint16_t plen,
               const struct xroute *xroute)
{
    int rc;

    if(plen < xroute->plen)
        return -1;
    if(plen > xroute->plen)
        return 1;

    rc = memcmp(prefix, xroute->prefix, xroute->plen);
    if(rc != 0)
        return rc;

    return 0;
}
static int
find_xroute_slot_mp(const unsigned char *prefix, uint16_t plen,
                 int *new_return)
{
    int p, m, g, c;

    if(numxroutes < 1) {
        if(new_return)
            *new_return = 0;
        return -1;
    }

    p = 0; g = numxroutes - 1;

    do {
        m = (p + g) / 2;
        c = xroute_compare_mp(prefix, plen, &xroutes[m]);
        if(c == 0)
            return m;
        else if(c < 0)
            g = m - 1;
        else
            p = m + 1;
    } while(p <= g);
    
    if(new_return)
        *new_return = p;

    return -1;
}
struct xroute *
find_xroute_mp(const unsigned char *prefix, uint16_t plen)
{
    int i = find_xroute_slot_mp(prefix, plen, NULL);
    if(i >= 0)
        return &xroutes[i];

    return NULL;
}
#endif  //----- ADD for MP -----

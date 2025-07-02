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
Copyright (c) 2007-2011 by Juliusz Chroboczek

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
#include <errno.h>
#include <assert.h>
#include <sys/time.h>

#include "babeld.h"
#include "util.h"
#include "kernel.h"
#include "interface.h"
#include "source.h"
#include "neighbour.h"
#include "route.h"
#include "xroute.h"
#include "message.h"
#include "resend.h"
#include "configuration.h"
#include "local.h"
#include "disambiguation.h"
#ifndef BABELD_CODE //+++++ ADD +++++
#include "cefore.h"
#endif //----- ADD -----

struct babel_route **routes = NULL;
static int route_slots = 0, max_route_slots = 0;
int kernel_metric = 0, reflect_kernel_metric = 0;
int allow_duplicates = -1;
int diversity_kind = DIVERSITY_NONE;
int diversity_factor = 256;     /* in units of 1/256 */

static int smoothing_half_life = 0;
static int two_to_the_one_over_hl = 0; /* 2^(1/hl) * 0x10000 */

#ifndef BABELD_CODE //+++++ ADD for MPMS +++++
static struct best_route **bestroutes = NULL;
static int bestroute_slots = 0, max_bestroute_slots = 0;
#endif //----- ADD for MPMS -----

#ifndef BABELD_CODE //+++++ ADD for MP +++++
static struct babel_route * create_route_entry_mp(
             struct source *src, 
             unsigned short seqno, unsigned short refmetric,
             unsigned short interval,
             struct neighbour *neigh, const unsigned char *nexthop, unsigned short port, 
             const unsigned char *channels, int channels_len); 
static void clear_route_entry_mp(struct babel_route *route);
static void removeNbFromRoute_mp(
            const unsigned char *prefix, uint16_t plen,
            const unsigned char *src_prefix, unsigned char src_plen,
            struct neighbour *neigh, const unsigned char *nexthop);
static int find_any_route_mp(const unsigned char *prefix, uint16_t plen,
           const unsigned char *src_prefix, unsigned char src_plen);
#endif //----- ADD for MP -----
#ifndef BABELD_CODE //+++++ ADD for MPSS +++++
static void clear_all_routes_mpss (const unsigned char *prefix, uint16_t plen,
                           const unsigned char *src_prefix, unsigned char src_plen);
#endif //----- ADD for MPSS -----
#ifndef BABELD_CODE //+++++ ADD for MPMS +++++
static void removeInfeasble_mpms(
            const unsigned char *prefix, uint16_t plen,
            const unsigned char *src_prefix, unsigned char src_plen,
            unsigned short my_fd);
static int find_bestroute_slot(
            const unsigned char *prefix, uint16_t plen,
            const unsigned char *src_prefix, unsigned char src_plen,
            int *new_return);
static int resize_bestroute_table(int new_slots);
static int isFassible_mpms(
            unsigned short refmetric, struct neighbour *neigh,
            const unsigned char *prefix, uint16_t plen,
            const unsigned char *src_prefix, unsigned char src_plen);
#endif //----- ADD for MPMS -----

static int
check_specific_first(void)
{
    /* All source-specific routes are in front of the list */
    int specific = 1;
    int i;
    for(i = 0; i < route_slots; i++) {
        if(is_default(routes[i]->src->src_prefix, routes[i]->src->src_plen)) {
            specific = 0;
        } else if(!specific) {
            return 0;
        }
    }
    return 1;
}

/* We maintain a list of "slots", ordered by prefix.  Every slot
   contains a linked list of the routes to this prefix, with the
   installed route, if any, at the head of the list. */

static int
#ifdef BABELD_CODE //+++++ REPLACE +++++
route_compare(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
route_compare(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
              const unsigned char *src_prefix, unsigned char src_plen,
              struct babel_route *route)
{
    int i;
    int is_ss = !is_default(src_prefix, src_plen);
    int is_ss_rt = !is_default(route->src->src_prefix, route->src->src_plen);

    /* Put all source-specific routes in the front of the list. */
    if(!is_ss && is_ss_rt) {
        return 1;
    } else if(is_ss && !is_ss_rt) {
        return -1;
    }
#ifdef BABELD_CODE //+++++ REPLACE +++++
    i = memcmp(prefix, route->src->prefix, 16);
#else // CEFBABELD
    i = memcmp(prefix, route->src->prefix, plen);
#endif //----- REPLACE -----
    if(i != 0)
        return i;

    if(plen < route->src->plen)
        return -1;
    if(plen > route->src->plen)
        return 1;

    if(is_ss) {
        i = memcmp(src_prefix, route->src->src_prefix, 16);
        if(i != 0)
            return i;
        if(src_plen < route->src->src_plen)
            return -1;
        if(src_plen > route->src->src_plen)
            return 1;
    }

    return 0;
}

/* Performs binary search, returns -1 in case of failure.  In the latter
   case, new_return is the place where to insert the new element. */

static int
#ifdef BABELD_CODE //+++++ REPLACE +++++
find_route_slot(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
find_route_slot(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                const unsigned char *src_prefix, unsigned char src_plen,
                int *new_return)
{
    int p, m, g, c;

    if(route_slots < 1) {
        if(new_return)
            *new_return = 0;
        return -1;
    }

    p = 0; g = route_slots - 1;

    do {
        m = (p + g) / 2;
        c = route_compare(prefix, plen, src_prefix, src_plen, routes[m]);
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

struct babel_route *
#ifdef BABELD_CODE //+++++ REPLACE +++++
find_route(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
find_route(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
           const unsigned char *src_prefix, unsigned char src_plen,
           struct neighbour *neigh, const unsigned char *nexthop)
{
    struct babel_route *route;
#ifdef BABELD //+++++ REPLACE +++++
    int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);

    if(i < 0)
        return NULL;

    route = routes[i];

    while(route) {
        if(route->neigh == neigh && memcmp(route->nexthop, nexthop, 16) == 0)
            return route;
        route = route->next;
    }
#else // CEFBABELD
    if (route_ctrl_type == ROUTE_CTRL_TYPE_S) {
        int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);

        if(i < 0)
            return NULL;

        route = routes[i];

        while(route) {
            if(route->neigh == neigh && memcmp(route->nexthop, nexthop, 16) == 0)
                return route;
            route = route->next;
        }
    } else { // for MPSS, MPMS 
        int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);

        if(i < 0)
            return NULL;

        route = routes[i];

        while(route) {
            if(route->neigh == neigh && memcmp(route->nexthop, nexthop, 16) == 0){
                return route;
        }
            route = route->next;
        }
    }
#endif //----- REPLACE -----

    return NULL;
}

struct babel_route *
#ifdef BABELD_CODE //+++++ REPLACE +++++
find_installed_route(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
find_installed_route(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                     const unsigned char *src_prefix, unsigned char src_plen)
{
    int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);

#ifdef BABELD //+++++ REPLACE +++++
    if(i >= 0 && routes[i]->installed)
        return routes[i];
#else // CEFBABELD   
    if (route_ctrl_type == ROUTE_CTRL_TYPE_S) {
        if(i >= 0 && routes[i]->installed){
            return routes[i];
        }
    } else { // for MPSS, MPMS
        if(i >= 0) {
            struct babel_route * route; 
            route = routes[i];
            while(route){
                if(route->installed){
                    return route;
                }
                route = route->next;
            }
        }
    }
#endif //----- REPLACE -----

    return NULL;
}

/* Returns an overestimate of the number of installed routes. */
int
installed_routes_estimate(void)
{
    return route_slots;
}

static int
resize_route_table(int new_slots)
{
    struct babel_route **new_routes;
    assert(new_slots >= route_slots);

    if(new_slots == 0) {
        new_routes = NULL;
        free(routes);
    } else {
        new_routes = realloc(routes, new_slots * sizeof(struct babel_route*));
        if(new_routes == NULL)
            return -1;
    }

    max_route_slots = new_slots;
    routes = new_routes;
    return 1;
}

/* Insert a route into the table.  If successful, retains the route.
   On failure, caller must free the route. */
static struct babel_route *
insert_route(struct babel_route *route)
{
    int i, n;

    assert(!route->installed);

    i = find_route_slot(route->src->prefix, route->src->plen,
                        route->src->src_prefix, route->src->src_plen, &n);

    if(i < 0) {
        if(route_slots >= max_route_slots)
            resize_route_table(max_route_slots < 1 ? 8 : 2 * max_route_slots);
        if(route_slots >= max_route_slots)
            return NULL;
        route->next = NULL;
        if(n < route_slots)
            memmove(routes + n + 1, routes + n,
                    (route_slots - n) * sizeof(struct babel_route*));
        route_slots++;
        routes[n] = route;
    } else {
        struct babel_route *r;
        r = routes[i];
        while(r->next)
            r = r->next;
        r->next = route;
        route->next = NULL;
    }

    return route;
}

static void
destroy_route(struct babel_route *route)
{
    free(route->channels);
    free(route);
}

void
flush_route(struct babel_route *route)
{
    int i;
    struct source *src;
    unsigned oldmetric;
    int lost = 0;
#ifndef BABELD_CODE //+++++ ADD for MP +++++
    char ifname[IF_NAMESIZE];
    int port;
    strcpy(ifname, route->neigh->ifp->name);
    port = route->port;
#endif //----- ADD for MP -----

    oldmetric = route_metric(route);
    src = route->src;

    if(route->installed) {
        uninstall_route(route);
        lost = 1;
    }
    
    i = find_route_slot(route->src->prefix, route->src->plen,
                        route->src->src_prefix, route->src->src_plen, NULL);
    assert(i >= 0 && i < route_slots);
#ifdef BABELD_CODE //+++++ REPLACE +++++
    local_notify_route(route, LOCAL_FLUSH);
#else // CEFBABELD
//    local_notify_route(route, LOCAL_FLUSH);
#endif //----- REPLACE -----
    if(route == routes[i]) {
        routes[i] = route->next;
        route->next = NULL;
        destroy_route(route);

        if(routes[i] == NULL) {
            if(i < route_slots - 1)
                memmove(routes + i, routes + i + 1,
                        (route_slots - i - 1) * sizeof(struct babel_route*));
            routes[route_slots - 1] = NULL;
            route_slots--;
            VALGRIND_MAKE_MEM_UNDEFINED(routes + route_slots, sizeof(struct route *));
        }

        if(route_slots == 0)
            resize_route_table(0);
        else if(max_route_slots > 8 && route_slots < max_route_slots / 4)
            resize_route_table(max_route_slots / 2);
    } else {
        struct babel_route *r = routes[i];
        while(r->next != route)
            r = r->next;
        r->next = route->next;
        route->next = NULL;
        destroy_route(route);
    }

#ifdef BABELD_CODE //+++++ REPLACE for MP +++++
    if(lost)
        route_lost(src, oldmetric);
#else // CEFBABELD
    if (route_ctrl_type == ROUTE_CTRL_TYPE_MS) {
        /// (MPSS-linkDisconnected)
        debugf("linkDisconnected (%s) process for %s\n", ifname, format_cefore_prefix(src->prefix, src->plen));
        if(lost){
            int rc;
            rc = find_any_route_mp(src->prefix, src->plen, src->src_prefix, src->src_plen);
            if(rc == 0){
                /// Send retract update if I have no feasible routes
                debugf("Send retract update if I have no feasible routes (%s, INFINITY)\n", 
                       format_cefore_prefix(src->prefix, src->plen));
                really_send_update_mp(NULL, src->id, src->prefix, src->plen, src->src_prefix, src->src_plen, 
                                      src->seqno, INFINITY, port);
            }
        }
        release_source(src);
    } else if (route_ctrl_type == ROUTE_CTRL_TYPE_MM) {
        /// (MPMS-linkDisconnected)
        debugf("linkDisconnected (%s) process for %s\n", ifname, format_cefore_prefix(src->prefix, src->plen));
        if(lost){
            struct best_route *broute;
            struct best_route last_bestroute;
            broute = find_bestroute(src->prefix, src->plen, src->src_prefix, src->src_plen, 0);
            if(broute){
                memcpy(&last_bestroute, broute, sizeof(struct best_route));
                release_source(src); /*** updateFeasibleDistance refers to route_ount,  ***/
                                     /*** so this function should be CALL at this time. ***/
                broute = updateFeasibleDistance_mpms(src->prefix, src->plen, src->src_prefix, src->src_plen);
                if (broute) {
                    if(memcmp(&last_bestroute, broute, sizeof(struct best_route)) != 0){
                        debugf("Send UPDATE(BestRoute) frefix=%s my_FD=%u \n",
                               format_cefore_prefix(src->prefix, src->plen), broute->my_FD);
                        send_update(NULL, 1, src->prefix, src->plen, src->src_prefix, src->src_plen);
                    }
                	
                }
            	else {
                    really_send_update_mp(NULL, last_bestroute.my_sourceId,
                                          last_bestroute.prefix, last_bestroute.plen,
                                          last_bestroute.src_prefix, last_bestroute.src_plen,
                                          last_bestroute.my_seqNo, 
		                                  INFINITY, port);
                }
            } else {
               release_source(src);
            }
        }
        else {
            release_source(src);
        }
    } else {
        if(lost){
            route_lost(src, oldmetric);
        }
        release_source(src);
    }
#endif //----- REPLACE for MP -----
        
}

void
flush_all_routes()
{
    int i;

    /* Start from the end, to avoid shifting the table. */
    i = route_slots - 1;
    while(i >= 0) {
        while(i < route_slots) {
            /* Uninstall first, to avoid calling route_lost. */
            if(routes[i]->installed)
                uninstall_route(routes[i]);
            flush_route(routes[i]);
        }
        i--;
    }

    check_sources_released();
}

void
flush_neighbour_routes(struct neighbour *neigh)
{
    int i;

    i = 0;
    while(i < route_slots) {
        struct babel_route *r;
        r = routes[i];
        while(r) {
            if(r->neigh == neigh) {
                flush_route(r);
                goto again;
            }
            r = r->next;
        }
        i++;
    again:
        ;
    }
}

void
flush_interface_routes(struct interface *ifp, int v4only)
{
    int i;

    i = 0;
    while(i < route_slots) {
        struct babel_route *r;
        r = routes[i];
        while(r) {
            if(r->neigh->ifp == ifp &&
               (!v4only || v4mapped(r->nexthop))) {
                flush_route(r);
                goto again;
            }
            r = r->next;
        }
        i++;
    again:
        ;
    }
}

struct route_stream {
    int installed;
    int index;
    struct babel_route *next;
};


struct route_stream *
route_stream(int which)
{
    struct route_stream *stream;

    if(!check_specific_first())
        fprintf(stderr, "Invariant failed: specific routes first in RIB.\n");

    stream = calloc(1, sizeof(struct route_stream));
    if(stream == NULL)
        return NULL;

    stream->installed = which;
    stream->index = which == ROUTE_ALL ? -1 : 0;
    stream->next = NULL;

    return stream;
}

struct babel_route *
route_stream_next(struct route_stream *stream)
{
    if(stream->installed) {
        while(stream->index < route_slots)
            if(stream->installed == ROUTE_SS_INSTALLED &&
               is_default(routes[stream->index]->src->src_prefix,
                          routes[stream->index]->src->src_plen))
                return NULL;
            else if(routes[stream->index]->installed)
                break;
            else
                stream->index++;

        if(stream->index < route_slots)
            return routes[stream->index++];
        else
            return NULL;
    } else {
        struct babel_route *next;
        if(!stream->next) {
            stream->index++;
            if(stream->index >= route_slots)
                return NULL;
            stream->next = routes[stream->index];
        }
        next = stream->next;
        stream->next = next->next;
        return next;
    }
}

void
route_stream_done(struct route_stream *stream)
{
    free(stream);
}

int
metric_to_kernel(int metric)
{
        if(metric >= INFINITY) {
                return KERNEL_INFINITY;
        } else if(reflect_kernel_metric) {
                int r = kernel_metric + metric;
                return r >= KERNEL_INFINITY ? KERNEL_INFINITY : r;
        } else {
                return kernel_metric;
        }
}

/* This is used to maintain the invariant that the installed route is at
   the head of the list. */
static void
move_installed_route(struct babel_route *route, int i)
{
    assert(i >= 0 && i < route_slots);
    assert(route->installed);

    if(route != routes[i]) {
        struct babel_route *r = routes[i];
        while(r->next != route)
            r = r->next;
        r->next = route->next;
        route->next = routes[i];
        routes[i] = route;
    }
}

void
install_route(struct babel_route *route)
{
    int i, rc;

    if(route->installed)
        return;

    if(!route_feasible(route))
        fprintf(stderr, "WARNING: installing unfeasible route "
                "(this shouldn't happen).");

    i = find_route_slot(route->src->prefix, route->src->plen,
                        route->src->src_prefix, route->src->src_plen, NULL);
    assert(i >= 0 && i < route_slots);

    if(routes[i] != route && routes[i]->installed) {
        fprintf(stderr, "WARNING: attempting to install duplicate route "
                "(this shouldn't happen).");
        return;
    }
#ifdef BABELD_CODE //+++++ REPLACE +++++
    rc = kinstall_route(route);
#else // CEFBABELD
#ifdef CEFNETD_IF   //+++++ DEB for ROUTING +++++
fprintf(stderr, "IF-[%s(%d)] ===== CALL cefore_fib_add_req_send(%s, nh:%s %d) =====\n"
    , __FUNCTION__, __LINE__, format_cefore_prefix(route->src->prefix, route->src->plen)
    , format_address(route->nexthop), route->port
);
#endif              //----- DEB for ROUTING -----
    rc = cefore_fib_add_req_send (
                route->src->prefix, route->src->plen, route->nexthop, route->port, 
                route->neigh->ifp->name);
#endif //----- REPLACE -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
    if(rc < 0 && errno != EEXIST)
        return;
#else // CEFBABELD
    if (rc < 0) {
        return;
    }
#endif //----- REPLACE -----

    route->installed = 1;
    move_installed_route(route, i);

#ifdef BABELD_CODE //+++++ REPLACE +++++
    local_notify_route(route, LOCAL_CHANGE);
#else // CEFBABELD
//    local_notify_route(route, LOCAL_CHANGE);
#endif //----- REPLACE -----
}

void
uninstall_route(struct babel_route *route)
{
    if(!route->installed)
        return;

    route->installed = 0;
#ifdef BABELD_CODE //+++++ REPLACE +++++
    kuninstall_route(route);

    local_notify_route(route, LOCAL_CHANGE);
#else // CEFBABELD
#ifdef CEFNETD_IF   //+++++ DEB for ROUTING +++++
fprintf(stderr, "IF-[%s(%d)] ===== CALL cefore_fib_del_req_send(%s, nh:%s) =====\n"
    , __FUNCTION__, __LINE__, format_cefore_prefix(route->src->prefix, route->src->plen)
    , format_address(route->nexthop)
);
#endif              //----- DEB for ROUTING -----
    cefore_fib_del_req_send (
        route->src->prefix, route->src->plen, route->nexthop, route->port,
        route->neigh->ifp->name);
#endif //----- REPLACE -----
}

/* This is equivalent to uninstall_route followed with install_route,
   but without the race condition.  The destination of both routes
   must be the same. */

static void
switch_routes(struct babel_route *old, struct babel_route *new)
{
    int rc;

    if(!old) {
        install_route(new);
        return;
    }

    if(!old->installed)
        return;

    if(!route_feasible(new))
        fprintf(stderr, "WARNING: switching to unfeasible route "
                "(this shouldn't happen).");
#ifdef BABELD_CODE //+++++ REPLACE +++++
    rc = kswitch_routes(old, new);
#else // CEFBABELD
#ifdef CEFNETD_IF   //+++++ DEB for ROUTING +++++
fprintf(stderr, "IF-[%s(%d)] ===== CALL cefore_fib_del_req_send(%s, nh:%s) =====\n"
    , __FUNCTION__, __LINE__, format_cefore_prefix(old->src->prefix, old->src->plen)
    , format_address(old->nexthop)
);
    
#endif              //----- DEB for ROUTING -----
    cefore_fib_del_req_send (
        old->src->prefix, old->src->plen, old->nexthop, old->port,
        old->neigh->ifp->name);
#ifdef CEFNETD_IF   //+++++ DEB for ROUTING +++++
fprintf(stderr, "IF-[%s(%d)] ===== CALL cefore_fib_add_req_send(%s, nh:%s) =====\n"
    , __FUNCTION__, __LINE__, format_cefore_prefix(new->src->prefix, new->src->plen)
    , format_address(new->nexthop)
);
#endif              //----- DEB for ROUTING -----
    rc = cefore_fib_add_req_send (
            new->src->prefix, new->src->plen, new->nexthop, new->port,
            new->neigh->ifp->name);
#endif //----- REPLACE -----
    if(rc < 0)
        return;

    old->installed = 0;
    new->installed = 1;
    move_installed_route(new, find_route_slot(new->src->prefix, new->src->plen,
                                              new->src->src_prefix,
                                              new->src->src_plen,
                                              NULL));
#ifdef BABELD_CODE //+++++ REPLACE +++++
    local_notify_route(old, LOCAL_CHANGE);
    local_notify_route(new, LOCAL_CHANGE);
#else // CEFBABELD
//    local_notify_route(old, LOCAL_CHANGE);
//    local_notify_route(new, LOCAL_CHANGE);
#endif //----- REPLACE -----
}

static void
change_route_metric(struct babel_route *route,
                    unsigned refmetric, unsigned cost, unsigned add)
{
#ifndef BABELD_CODE //+++++ ADD for DEB +++++
    debugf("[FIN] %s (<args>)\n", __FUNCTION__);
#endif //----- ADD for DEB -----

#ifdef BABELD_CODE //+++++ DEL +++++
    int old, new;
    int newmetric = MIN(refmetric + cost + add, INFINITY);

    old = metric_to_kernel(route_metric(route));
    new = metric_to_kernel(newmetric);

    if(route->installed && old != new) {
        int rc;
        rc = kchange_route_metric(route, refmetric, cost, add);
        if(rc < 0)
            return;
    }
#endif //----- DEL -----

    /* Update route->smoothed_metric using the old metric. */
    route_smoothed_metric(route);

    route->refmetric = refmetric;
    route->cost = cost;
    route->add_metric = add;

    if(smoothing_half_life == 0) {
        route->smoothed_metric = route_metric(route);
        route->smoothed_metric_time = now.tv_sec;
    }
#ifdef BABELD_CODE //+++++ REPLACE +++++
    local_notify_route(route, LOCAL_CHANGE);
#else // CEFBABELD
//    local_notify_route(route, LOCAL_CHANGE);
#endif //----- REPLACE -----
}

static void
retract_route(struct babel_route *route)
{
    /* We cannot simply remove the route from the kernel, as that might
       cause a routing loop -- see RFC 6126 Sections 2.8 and 3.5.5. */
    change_route_metric(route, INFINITY, INFINITY, 0);
}

int
route_feasible(struct babel_route *route)
{
    return update_feasible(route->src, route->seqno, route->refmetric);
}

int
route_old(struct babel_route *route)
{
    return route->time < now.tv_sec - route->hold_time * 7 / 8;
}

int
route_expired(struct babel_route *route)
{
    return route->time < now.tv_sec - route->hold_time;
}

static int
channels_interfere(int ch1, int ch2)
{
    if(ch1 == IF_CHANNEL_NONINTERFERING || ch2 == IF_CHANNEL_NONINTERFERING)
        return 0;
    if(ch1 == IF_CHANNEL_INTERFERING || ch2 == IF_CHANNEL_INTERFERING)
        return 1;
    return ch1 == ch2;
}

int
route_interferes(struct babel_route *route, struct interface *ifp)
{
    switch(diversity_kind) {
    case DIVERSITY_NONE:
        return 1;
    case DIVERSITY_INTERFACE_1:
        return route->neigh->ifp == ifp;
    case DIVERSITY_CHANNEL_1:
    case DIVERSITY_CHANNEL:
        if(route->neigh->ifp == ifp)
            return 1;
        if(channels_interfere(ifp->channel, route->neigh->ifp->channel))
            return 1;
        if(diversity_kind == DIVERSITY_CHANNEL) {
            int i;
            for(i = 0; i < route->channels_len; i++) {
                if(route->channels[i] != 0 &&
                   channels_interfere(ifp->channel, route->channels[i]))
                    return 1;
            }
        }
        return 0;
    default:
        fprintf(stderr, "Unknown kind of diversity.\n");
        return 1;
    }
}

int
update_feasible(struct source *src,
                unsigned short seqno, unsigned short refmetric)
{
    if(src == NULL)
        return 1;

    if(src->time < now.tv_sec - SOURCE_GC_TIME)
        /* Never mind what is probably stale data */
        return 1;

    if(refmetric >= INFINITY)
        /* Retractions are always feasible */
        return 1;

    return (seqno_compare(seqno, src->seqno) > 0 ||
            (src->seqno == seqno && refmetric < src->metric));
}

void
change_smoothing_half_life(int half_life)
{
    if(half_life <= 0) {
        smoothing_half_life = 0;
        two_to_the_one_over_hl = 0;
        return;
    }

    smoothing_half_life = half_life;
    switch(smoothing_half_life) {
    case 1: two_to_the_one_over_hl = 131072; break;
    case 2: two_to_the_one_over_hl = 92682; break;
    case 3: two_to_the_one_over_hl = 82570; break;
    case 4: two_to_the_one_over_hl = 77935; break;
    default:
        /* 2^(1/x) is 1 + log(2)/x + O(1/x^2) at infinity. */
        two_to_the_one_over_hl = 0x10000 + 45426 / half_life;
    }
}

/* Update the smoothed metric, return the new value. */
int
route_smoothed_metric(struct babel_route *route)
{
    int metric = route_metric(route);

    if(smoothing_half_life <= 0 ||                 /* no smoothing */
       metric >= INFINITY ||                       /* route retracted */
       route->smoothed_metric_time > now.tv_sec || /* clock stepped */
       route->smoothed_metric == metric) {         /* already converged */
        route->smoothed_metric = metric;
        route->smoothed_metric_time = now.tv_sec;
    } else {
        int diff;
        /* We randomise the computation, to minimise global synchronisation
           and hence oscillations. */
        while(route->smoothed_metric_time <= now.tv_sec - smoothing_half_life) {
            diff = metric - route->smoothed_metric;
            route->smoothed_metric += roughly(diff) / 2;
            route->smoothed_metric_time += smoothing_half_life;
        }
        while(route->smoothed_metric_time < now.tv_sec) {
            diff = metric - route->smoothed_metric;
            route->smoothed_metric +=
                roughly(diff) * (two_to_the_one_over_hl - 0x10000) / 0x10000;
            route->smoothed_metric_time++;
        }

        diff = metric - route->smoothed_metric;
        if(diff > -4 && diff < 4)
            route->smoothed_metric = metric;
    }

    /* change_route_metric relies on this */
    assert(route->smoothed_metric_time == now.tv_sec);
    return route->smoothed_metric;
}

static int
route_acceptable(struct babel_route *route, int feasible,
                 struct neighbour *exclude)
{
    if(route_expired(route))
        return 0;
    if(feasible && !route_feasible(route))
        return 0;
    if(exclude && route->neigh == exclude)
        return 0;
    return 1;
}

/* Find the best route according to the weak ordering.  Any
   linearisation of the strong ordering (see consider_route) will do,
   we use sm <= sm'.  We could probably use a lexical ordering, but
   that's probably overkill. */

struct babel_route *
#ifdef BABELD_CODE //+++++ REPLACE +++++
find_best_route(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
find_best_route(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                const unsigned char *src_prefix, unsigned char src_plen,
                int feasible, struct neighbour *exclude)
{
    struct babel_route *route, *r;
    int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);

    if(i < 0)
        return NULL;

    route = routes[i];
    while(route && !route_acceptable(route, feasible, exclude))
        route = route->next;

    if(!route)
        return NULL;

    r = route->next;
    while(r) {
        if(route_acceptable(r, feasible, exclude) &&
           (route_smoothed_metric(r) < route_smoothed_metric(route)))
            route = r;
        r = r->next;
    }

    return route;
}

void
update_route_metric(struct babel_route *route)
{
    int oldmetric = route_metric(route);
    int old_smoothed_metric = route_smoothed_metric(route);

    if(route_expired(route)) {
        if(route->refmetric < INFINITY) {
            route->seqno = seqno_plus(route->src->seqno, 1);
            retract_route(route);
            if(oldmetric < INFINITY)
                route_changed(route, route->src, oldmetric);
        }
    } else {
#ifdef BABELD_CODE //+++++ DEL +++++
        struct neighbour *neigh = route->neigh;
        int add_metric = input_filter(route->src->id,
                                      route->src->prefix, route->src->plen,
                                      route->src->src_prefix,
                                      route->src->src_plen,
                                      neigh->address,
                                      neigh->ifp->ifindex);
#endif //----- DEL -----
#ifdef BABELD_CODE //+++++ REPLACE +++++
        change_route_metric(route, route->refmetric,
                            neighbour_cost(route->neigh), add_metric);
#else // CEFBABELD
        change_route_metric(route, route->refmetric,
                            neighbour_cost(route->neigh), 0);
#endif //----- REPLACE -----
        if(route_metric(route) != oldmetric ||
           route_smoothed_metric(route) != old_smoothed_metric)
            route_changed(route, route->src, oldmetric);
    }
}

/* Called whenever a neighbour's cost changes, to update the metric of
   all routes through that neighbour.  Calls local_notify_neighbour. */
void
update_neighbour_metric(struct neighbour *neigh, int changed)
{

    if(changed) {
        int i;

        for(i = 0; i < route_slots; i++) {
            struct babel_route *r = routes[i];
            while(r) {
                if(r->neigh == neigh)
                    update_route_metric(r);
                r = r->next;
            }
        }
    }
#ifdef BABELD_CODE //+++++ REPLACE +++++
    local_notify_neighbour(neigh, LOCAL_CHANGE);
#else // CEFBABELD
//    local_notify_neighbour(neigh, LOCAL_CHANGE);
#endif //----- REPLACE -----
}

void
update_interface_metric(struct interface *ifp)
{
    int i;

    for(i = 0; i < route_slots; i++) {
        struct babel_route *r = routes[i];
        while(r) {
            if(r->neigh->ifp == ifp)
                update_route_metric(r);
            r = r->next;
        }
    }
}

/* This is called whenever we receive an update. */
struct babel_route *
update_route(const unsigned char *id,
#ifdef BABELD_CODE //+++++ REPLACE +++++
             const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
             const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
             const unsigned char *src_prefix, unsigned char src_plen,
             unsigned short seqno, unsigned short refmetric,
             unsigned short interval,
#ifdef BABELD_CODE //+++++ REPLACE +++++
             struct neighbour *neigh, const unsigned char *nexthop,
#else // CEFBABELD
             struct neighbour *neigh, const unsigned char *nexthop, unsigned short port, 
#endif //----- REPLACE -----
             const unsigned char *channels, int channels_len)
{
    struct babel_route *route;
    struct source *src;
    int metric, feasible;
#ifdef BABELD_CODE //+++++ DEL +++++
    int add_metric;
#endif //----- DEL -----
    int hold_time = MAX((4 * interval) / 100 + interval / 50, 15);
#ifdef BABELD_CODE //+++++ REPLACE +++++
    int is_v4;
    if(memcmp(id, myid, 8) == 0)
        return NULL;
#else // CEFBABELD
    if(memcmp(id, myid, 8) == 0) {
        debugf("Received Router-id is my ID.\n");
        return NULL;
    }
#endif //----- REPLACE -----
#ifdef BABELD_CODE //+++++ DEL +++++
    if(martian_prefix(prefix, plen) || martian_prefix(src_prefix, src_plen)) {
        fprintf(stderr, "Rejecting martian route to %s from %s through %s.\n",
                format_prefix(prefix, plen),
                format_prefix(src_prefix, src_plen), format_address(nexthop));
        return NULL;
    }

    is_v4 = v4mapped(prefix);
    if(is_v4 != v4mapped(src_prefix))
        return NULL;

    add_metric = input_filter(id, prefix, plen, src_prefix, src_plen,
                              neigh->address, neigh->ifp->ifindex);
    if(add_metric >= INFINITY)
        return NULL;
#endif //----- DEL -----

    route = find_route(prefix, plen, src_prefix, src_plen, neigh, nexthop);

    if(route && memcmp(route->src->id, id, 8) == 0)
        /* Avoid scanning the source table. */
        src = route->src;
    else
        src = find_source(id, prefix, plen, src_prefix, src_plen, 1, seqno);

    if(src == NULL)
        return NULL;

    feasible = update_feasible(src, seqno, refmetric);
#ifdef BABELD_CODE //+++++ REPLACE +++++
    metric = MIN((int)refmetric + neighbour_cost(neigh) + add_metric, INFINITY);
#else // CEFBABELD
    metric = MIN((int)refmetric + neighbour_cost(neigh), INFINITY);
#endif //----- REPLACE -----
    if(route) {
        struct source *oldsrc;
        unsigned short oldmetric, oldinstalled;
        int lost = 0;

        oldinstalled = route->installed;
        oldsrc = route->src;
        oldmetric = route_metric(route);

        /* If a successor switches sources, we must accept his update even
           if it makes a route unfeasible in order to break any routing loops
           in a timely manner.  If the source remains the same, we ignore
           the update. */
        if(!feasible && route->installed) {
            debugf("Unfeasible update for installed route to %s "
                   "(%s %d %d -> %s %d %d).\n",
#ifdef BABELD_CODE //+++++ REPLACE +++++
                   format_prefix(src->prefix, src->plen),
#else // CEFBABELD
                   format_cefore_prefix(src->prefix, src->plen),
#endif //----- REPLACE -----
                   format_eui64(route->src->id),
                   route->seqno, route->refmetric,
                   format_eui64(src->id), seqno, refmetric);
            if(src != route->src) {
                uninstall_route(route);
                 lost = 1;
            }
        }

        route->src = retain_source(src);
        if(refmetric < INFINITY)
            route->time = now.tv_sec;
        route->seqno = seqno;

        if(channels_len == 0) {
            free(route->channels);
            route->channels = NULL;
            route->channels_len = 0;
        } else {
            if(channels_len != route->channels_len) {
                unsigned char *new_channels =
                    realloc(route->channels, channels_len);
                if(new_channels == NULL) {
                    perror("malloc(channels)");
                    /* Truncate the data. */
                    channels_len = MIN(channels_len, route->channels_len);
                } else {
                    route->channels = new_channels;
                }
            }
            memcpy(route->channels, channels, channels_len);
            route->channels_len = channels_len;
        }

#ifdef BABELD_CODE //+++++ REPLACE +++++
        change_route_metric(route,
                            refmetric, neighbour_cost(neigh), add_metric);
#else // CEFBABELD
        change_route_metric(route,
                            refmetric, neighbour_cost(neigh), 0);
#endif //----- REPLACE -----
        route->hold_time = hold_time;

        route_changed(route, oldsrc, oldmetric);
        if(!lost) {
            lost = oldinstalled &&
                find_installed_route(prefix, plen, src_prefix, src_plen) == NULL;
        }
        if(lost)
            route_lost(oldsrc, oldmetric);
        else if(!feasible)
            send_unfeasible_request(neigh, route_old(route), seqno, metric, src);
        release_source(oldsrc);
    } else {
        struct babel_route *new_route;

        if(refmetric >= INFINITY)
            /* Somebody's retracting a route we never saw. */
            return NULL;
        if(!feasible) {
            send_unfeasible_request(neigh, 0, seqno, metric, src);
        }

        route = calloc(1, sizeof(struct babel_route));
        if(route == NULL) {
            perror("malloc(route)");
            return NULL;
        }

        route->src = retain_source(src);
        route->refmetric = refmetric;
        route->cost = neighbour_cost(neigh);
#ifdef BABELD_CODE //+++++ REPLACE +++++
        route->add_metric = add_metric;
#else // CEFBABELD
        route->add_metric = 0;
#endif //----- REPLACE -----
        route->seqno = seqno;
        route->neigh = neigh;
        memcpy(route->nexthop, nexthop, 16);
#ifndef BABELD_CODE //+++++ ADD +++++
        route->port = port;
#endif //----- ADD -----
        route->time = now.tv_sec;
        route->hold_time = hold_time;
        route->smoothed_metric = MAX(route_metric(route), INFINITY / 2);
        route->smoothed_metric_time = now.tv_sec;
        if(channels_len > 0) {
            route->channels = malloc(channels_len);
            if(route->channels == NULL) {
                perror("malloc(channels)");
            } else {
                memcpy(route->channels, channels, channels_len);
            }
        }
        route->next = NULL;
        new_route = insert_route(route);
        if(new_route == NULL) {
            fprintf(stderr, "Couldn't insert route.\n");
            destroy_route(route);
            return NULL;
        }
#ifdef BABELD_CODE //+++++ REPLACE +++++
        local_notify_route(route, LOCAL_ADD);
#else // CEFBABELD
//        local_notify_route(route, LOCAL_ADD);

#endif //----- REPLACE -----
        consider_route(route);
    }
    return route;
}

/* We just received an unfeasible update.  If it's any good, send
   a request for a new seqno. */
void
send_unfeasible_request(struct neighbour *neigh, int force,
                        unsigned short seqno, unsigned short metric,
                        struct source *src)
{
    struct babel_route *route = find_installed_route(src->prefix, src->plen,
                                                     src->src_prefix,
                                                     src->src_plen);

    if(seqno_minus(src->seqno, seqno) > 100) {
        /* Probably a source that lost its seqno.  Let it time-out. */
        return;
    }

    if(force || !route || route_metric(route) >= metric + 512) {
        send_unicast_multihop_request(neigh, src->prefix, src->plen,
                                      src->src_prefix, src->src_plen,
                                      src->metric >= INFINITY ?
                                      src->seqno :
                                      seqno_plus(src->seqno, 1),
                                      src->id, 127);
    }
}

/* This takes a feasible route and decides whether to install it.
   This uses the strong ordering, which is defined by sm <= sm' AND
   m <= m'.  This ordering is not total, which is what causes
   hysteresis. */

void
consider_route(struct babel_route *route)
{
    struct babel_route *installed;
    struct xroute *xroute;

    if(route->installed)
        return;

    if(!route_feasible(route))
        return;

    xroute = find_xroute(route->src->prefix, route->src->plen,
                         route->src->src_prefix, route->src->src_plen);
    if(xroute && (allow_duplicates < 0 || xroute->metric >= allow_duplicates))
        return;

    installed = find_installed_route(route->src->prefix, route->src->plen,
                                     route->src->src_prefix,
                                     route->src->src_plen);

    if(installed == NULL)
        goto install;

    if(route_metric(route) >= INFINITY)
        return;

    if(route_metric(installed) >= INFINITY)
        goto install;

    if(route_metric(installed) >= route_metric(route) &&
       route_smoothed_metric(installed) > route_smoothed_metric(route))
        goto install;

    return;

 install:
    switch_routes(installed, route);
    if(installed && route->installed)
        send_triggered_update(route, installed->src, route_metric(installed));
    else
        send_update(NULL, 1, route->src->prefix, route->src->plen,
                    route->src->src_prefix, route->src->src_plen);
    return;
}

void
retract_neighbour_routes(struct neighbour *neigh)
{
    int i;

    for(i = 0; i < route_slots; i++) {
        struct babel_route *r = routes[i];
        while(r) {
            if(r->neigh == neigh) {
                if(r->refmetric != INFINITY) {
                    unsigned short oldmetric = route_metric(r);
                    retract_route(r);
                    if(oldmetric != INFINITY)
                        route_changed(r, r->src, oldmetric);
                }
            }
            r = r->next;
        }
    }
}

void
send_triggered_update(struct babel_route *route, struct source *oldsrc,
                      unsigned oldmetric)
{
    unsigned newmetric, diff;
    /* 1 means send speedily, 2 means resend */
    int urgent;

    if(!route->installed)
        return;

    newmetric = route_metric(route);
    diff =
        newmetric >= oldmetric ? newmetric - oldmetric : oldmetric - newmetric;

    if(route->src != oldsrc || (oldmetric < INFINITY && newmetric >= INFINITY))
        /* Switching sources can cause transient routing loops.
           Retractions can cause blackholes. */
        urgent = 2;
    else if(newmetric > oldmetric && oldmetric < 6 * 256 && diff >= 512)
        /* Route getting significantly worse */
        urgent = 1;
    else if(unsatisfied_request(route->src->prefix, route->src->plen,
                                route->src->src_prefix, route->src->src_plen,
                                route->seqno, route->src->id))
        /* Make sure that requests are satisfied speedily */
        urgent = 1;
    else if(oldmetric >= INFINITY && newmetric < INFINITY)
        /* New route */
        urgent = 0;
    else if(newmetric < oldmetric && diff < 1024)
        /* Route getting better.  This may be a transient fluctuation, so
           don't advertise it to avoid making routes unfeasible later on. */
        return;
    else if(diff < 384)
        /* Don't fret about trivialities */
        return;
    else
        urgent = 0;

    if(urgent >= 2)
        send_update_resend(NULL, route->src->prefix, route->src->plen,
                           route->src->src_prefix, route->src->src_plen);
    else
        send_update(NULL, urgent, route->src->prefix, route->src->plen,
                    route->src->src_prefix, route->src->src_plen);

    if(oldmetric < INFINITY) {
        if(newmetric >= oldmetric + 288) {
            send_multicast_request(NULL, route->src->prefix, route->src->plen,
                                   route->src->src_prefix, route->src->src_plen);
        }
    }
}

/* A route has just changed.  Decide whether to switch to a different route or
   send an update. */
void
route_changed(struct babel_route *route,
              struct source *oldsrc, unsigned short oldmetric)
{
    if(route->installed) {
        struct babel_route *better_route;
        /* Do this unconditionally -- microoptimisation is not worth it. */
        better_route =
            find_best_route(route->src->prefix, route->src->plen,
                            route->src->src_prefix, route->src->src_plen,
                            1, NULL);
        if(better_route && route_metric(better_route) < route_metric(route))
            consider_route(better_route);
    }

    if(route->installed) {
        /* We didn't change routes after all. */
        send_triggered_update(route, oldsrc, oldmetric);
    } else {
        /* Reconsider routes even when their metric didn't decrease,
           they may not have been feasible before. */
        consider_route(route);
    }
}

/* We just lost the installed route to a given destination. */
void
route_lost(struct source *src, unsigned oldmetric)
{
    struct babel_route *new_route;
    new_route = find_best_route(src->prefix, src->plen,
                                src->src_prefix, src->src_plen, 1, NULL);
    if(new_route) {
        consider_route(new_route);
    } else if(oldmetric < INFINITY) {
        /* Avoid creating a blackhole. */
        send_update_resend(NULL, src->prefix, src->plen,
                           src->src_prefix, src->src_plen);
        /* If the route was usable enough, try to get an alternate one.
           If it was not, we could be dealing with oscillations around
           the value of INFINITY. */
        if(oldmetric <= INFINITY / 2)
            send_request_resend(src->prefix, src->plen,
                                src->src_prefix, src->src_plen,
                                src->metric >= INFINITY ?
                                src->seqno : seqno_plus(src->seqno, 1),
                                src->id);
    }
}

/* This is called periodically to flush old routes.  It will also send
   requests for routes that are about to expire. */
void
expire_routes(void)
{
    struct babel_route *r;
    int i;

    debugf("Expiring old routes.\n");

    i = 0;
    while(i < route_slots) {
        r = routes[i];
        while(r) {
            /* Protect against clock being stepped. */
            if(r->time > now.tv_sec || route_old(r)) {
                flush_route(r);
                goto again;
            }

            update_route_metric(r);

            if(r->installed && r->refmetric < INFINITY) {
                if(route_old(r))
                    /* Route about to expire, send a request. */
                    send_unicast_request(r->neigh,
                                         r->src->prefix, r->src->plen,
                                         r->src->src_prefix, r->src->src_plen);
            }
            r = r->next;
        }
        i++;
    again:
        ;
    }
}
#ifndef BABELD_CODE //+++++  for DEB +++++
void DEB_RPTIN_ROUTE_TABLE()
{
    struct route_stream *routes;
    fprintf(stderr, ">>>>>>>>>>>>>>>>>>>> %s >>>>>>>>>>>>>>>>>>>\n", __FUNCTION__);
    routes = route_stream(ROUTE_ALL); 
    if(routes) {
        while(1) {
            struct babel_route *route = route_stream_next(routes);
            if(route == NULL) break;
            {
                const unsigned char *nexthop =
                    memcmp(route->nexthop, route->neigh->address, 16) == 0 ?
                    NULL : route->nexthop;
                fprintf(stderr, "%s%s%s metric %d (%d) refmetric %d id %s "
                        "seqno %d age %d via %s neigh %s%s%s%s\n",
                        format_cefore_prefix(route->src->prefix, route->src->plen),
                        route->src->src_plen > 0 ? " from " : "",
                        route->src->src_plen > 0 ?
                        format_prefix(route->src->src_prefix, route->src->src_plen) : "",
                        route_metric(route), route_smoothed_metric(route), route->refmetric,
                        format_eui64(route->src->id),
                        (int)route->seqno,
                        (int)(now.tv_sec - route->time),
                        route->neigh->ifp->name,
                        format_address(route->neigh->address),
                        nexthop ? " nexthop " : "",
                        nexthop ? format_address(nexthop) : "",
                        route->installed ? " (installed)" :
                        route_feasible(route) ? " (feasible)" : "");
            }
        }
        route_stream_done(routes);
    }
    if (route_ctrl_type == ROUTE_CTRL_TYPE_MS) {
        dump_source(stderr);
    } else if (route_ctrl_type == ROUTE_CTRL_TYPE_MM) {
        dump_best_route(stderr);
        dump_source(stderr);
    }
    fprintf(stderr, "<<<<<<<<<<<<<<<<<<<< %s <<<<<<<<<<<<<<<<<<<\n", __FUNCTION__);
}
#endif //----- ADD for DEB -----

#ifndef BABELD_CODE //+++++ ADD for STAT +++++
/**************************************************************************************************/    
/***** STAT Common Functions                                                                  *****/
/**************************************************************************************************/    
int
exist_installed_route()
{
    int i; 
    struct babel_route *route;
    for(i=0; i<route_slots; i++){
        route = routes[i];
        while(route) {
        	if(route->installed){
        		return 1;
            }
            route = route->next;
        }
    }
    return 0;
}
#endif //----- ADD for STAT -----

#ifndef BABELD_CODE //+++++ ADD for MP +++++
/**************************************************************************************************/    
/***** Multi Path Common Functions                                                            *****/
/**************************************************************************************************/    
static void
clear_route_entry_mp(struct babel_route *route)
{
    int i;

    i = find_route_slot(route->src->prefix, route->src->plen,
                        route->src->src_prefix, route->src->src_plen, NULL);
    assert(i >= 0 && i < route_slots);
    if(route == routes[i]) {
        routes[i] = route->next;
        route->next = NULL;
        release_source(route->src);
        destroy_route(route);

        if(routes[i] == NULL) {
            if(i < route_slots - 1)
                memmove(routes + i, routes + i + 1,
                        (route_slots - i - 1) * sizeof(struct babel_route*));
            routes[route_slots - 1] = NULL;
            route_slots--;
            VALGRIND_MAKE_MEM_UNDEFINED(routes + route_slots, sizeof(struct route *));
        }

        if(route_slots == 0)
            resize_route_table(0);
        else if(max_route_slots > 8 && route_slots < max_route_slots / 4)
            resize_route_table(max_route_slots / 2);
    } else {
        struct babel_route *r = routes[i];
        while(r->next != route)
            r = r->next;
        r->next = route->next;
        route->next = NULL;
        release_source(route->src);
        destroy_route(route);
    }
}

static struct babel_route *
create_route_entry_mp(
             struct source *src, 
             unsigned short seqno, unsigned short refmetric,
             unsigned short interval,
             struct neighbour *neigh, const unsigned char *nexthop, unsigned short port, 
             const unsigned char *channels, int channels_len) 
{
    struct babel_route *route;
    struct babel_route *new_route;
    int hold_time = MAX((4 * interval) / 100 + interval / 50, 15);

        route = calloc(1, sizeof(struct babel_route));
        if(route == NULL) {
            perror("malloc(route)");
            return NULL;
        }

        route->src = retain_source(src);
        route->refmetric = refmetric;
        route->cost = neighbour_cost(neigh);
        route->add_metric = 0;
        route->seqno = seqno;
        route->neigh = neigh;
        memcpy(route->nexthop, nexthop, 16);
        route->port = port;
        route->time = now.tv_sec;
        route->hold_time = hold_time;
        route->smoothed_metric = MAX(route_metric(route), INFINITY / 2);
        route->smoothed_metric_time = now.tv_sec;
        if(channels_len > 0) {
            route->channels = malloc(channels_len);
            if(route->channels == NULL) {
                perror("malloc(channels)");
            } else {
                memcpy(route->channels, channels, channels_len);
            }
        }
        route->next = NULL;
        new_route = insert_route(route);
        if(new_route == NULL) {
            fprintf(stderr, "Couldn't insert route.\n");
            destroy_route(route);
            return NULL;
        }

        new_route->installed = 1;
        move_installed_route(new_route,
                             find_route_slot(new_route->src->prefix, new_route->src->plen,
                                             new_route->src->src_prefix,
                                             new_route->src->src_plen,
                                             NULL));

    return route;
}
static void 
removeNbFromRoute_mp(
             const unsigned char *prefix, uint16_t plen,
             const unsigned char *src_prefix, unsigned char src_plen,
             struct neighbour *neigh, const unsigned char *nexthop)
{
    int i;
    struct babel_route *route;

    i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);
    assert(i >= 0 && i < route_slots);
    route = routes[i];
    while(route) {
        if(route->neigh == neigh && memcmp(route->nexthop, nexthop, 16) == 0) {
            if(route == routes[i]) {
#ifdef CEFNETD_IF   //+++++ DEB for ROUTING +++++
fprintf(stderr, "IF-[%s(%d)] ===== CALL cefore_fib_del_req_send(%s, nh:%s) =====\n"
    , __FUNCTION__, __LINE__, format_cefore_prefix(route->src->prefix, route->src->plen)
    , format_address(route->nexthop)
);
#endif              //----- DEB for ROUTING -----
                /* Delete FIB */
                cefore_fib_del_req_send (route->src->prefix, route->src->plen, route->nexthop, route->port,
                                          route->neigh->ifp->name);
                routes[i] = route->next;
                route->next = NULL;
                release_source(route->src);
                destroy_route(route);

                if(routes[i] == NULL) {
                    if(i < route_slots - 1)
                        memmove(routes + i, routes + i + 1,
                                (route_slots - i - 1) * sizeof(struct babel_route*));
                    routes[route_slots - 1] = NULL;
                    route_slots--;
                    VALGRIND_MAKE_MEM_UNDEFINED(routes + route_slots, sizeof(struct route *));
                }

                if(route_slots == 0)
                    resize_route_table(0);
                else if(max_route_slots > 8 && route_slots < max_route_slots / 4)
                    resize_route_table(max_route_slots / 2);
            } else {
                struct babel_route *r = routes[i];
                while(r->next != route){
                    r = r->next;
                }
#ifdef CEFNETD_IF   //+++++ DEB for ROUTING +++++
fprintf(stderr, "IF-[%s(%d)] ===== CALL cefore_fib_del_req_send(%s, nh:%s) =====\n"
    , __FUNCTION__, __LINE__, format_cefore_prefix(route->src->prefix, route->src->plen)
    , format_address(route->nexthop)
);
#endif              //----- DEB for ROUTING -----
                /* Delete FIB */
                cefore_fib_del_req_send (route->src->prefix, route->src->plen, route->nexthop, route->port,
                                          route->neigh->ifp->name);
                r->next = route->next;
                route->next = NULL;
                release_source(route->src);
                destroy_route(route);
            }
        }
        route = route->next;
    }
}
    
static int 
find_any_route_mp(const unsigned char *prefix, uint16_t plen,
           const unsigned char *src_prefix, unsigned char src_plen)
{
    int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);

    if(i < 0){
        return 0;
    }
    
    return 1;
    
}
void
send_unfeasible_request_mp(struct neighbour *neigh,
                        unsigned short seqno, unsigned short metric,
                        struct source *src)
{
    int force;
    int i;

    struct babel_route *route = find_installed_route(src->prefix, src->plen,
                                                     src->src_prefix,
                                                     src->src_plen);
    if(!route) {
        send_unicast_multihop_request(neigh, src->prefix, src->plen,
                                      src->src_prefix, src->src_plen,
                                      seqno_plus(src->seqno, 1),
                                      src->id, 127);
        return;
    }

    i = find_route_slot(src->prefix, src->plen, src->src_prefix, src->src_plen, NULL);
    if(i < 0) {
         return;
    }
    route = routes[i];
    while(route) {
        force = route_old(route);
        if(force==0 && (route_metric(route) < metric + 512)){
            return;
        }
        route = route->next;
    }
    i = find_route_slot(src->prefix, src->plen, src->src_prefix, src->src_plen, NULL);
    if(i < 0) {
         return;
    }
    route = routes[i];
    while(route) {
        force = route_old(route);
        if(force==1 || (route_metric(route) >= metric + 512)){
            send_unicast_multihop_request(neigh, src->prefix, src->plen,
                                          src->src_prefix, src->src_plen,
                                          seqno_plus(src->seqno, 1),
                                          src->id, 127);
        }
        route = route->next;
    }
}
#endif  //----- ADD for MP -----

#ifndef BAELD_CODE //+++++ ADD for MPSS +++++
/**************************************************************************************************/    
/***** Multi Path Single Source Functions                                               *****/
/**************************************************************************************************/    
/* This is called whenever we receive an update. */
void
update_route_mpss(const unsigned char *id,
             const unsigned char *prefix, uint16_t plen,
             const unsigned char *src_prefix, unsigned char src_plen,
             unsigned short seqno, unsigned short refmetric,
             unsigned short interval,
             struct neighbour *neigh, const unsigned char *nexthop, unsigned short port, 
             const unsigned char *channels, int channels_len)
{
    struct babel_route *route;
    struct babel_route *new_route = NULL;
    struct source *src;
    unsigned char *sid;
    int rc;
    unsigned short my_FD;
    struct xroute *xroute;
    
    if(memcmp(id, myid, 8) == 0) {
        debugf("Received Router-id is my ID.\n");
        return;
    }

#ifdef DEB_MPSS //+++++ DEB for MPSS +++++
    fprintf(stderr, "MPSS-[%s]: ===== RCVed UPDATE(id=%s, prefix=%s, seqno=%u, refmetric=%u, nexthop=%s, port=%u) =====\n",
                      __FUNCTION__, 
                      format_eui64(id), format_cefore_prefix(prefix, plen), seqno, refmetric, 
                      format_address(nexthop), port);
#endif //----- DEB for MPSS -----

    /* Check multi source */
    if (refmetric != INFINITY) {
        sid = find_other_source_mpss (id, prefix, plen, src_prefix, src_plen);
        if (sid != NULL) {
            fprintf(stderr, 
                 "Warning: Multiple sources are detected, while cefbabeld was launched with MPSS option."
                 "(Prefix=%s RouterID=%s Stored RouterID=%s)\n",
                 format_cefore_prefix(prefix, plen), format_eui64(id), format_eui64(sid));
            return;
        }
    }
    /* Check for the existence of static prefix name*/
    xroute = find_xroute_mp(prefix, plen);
    if(xroute){
        fprintf(stderr, 
            "Warning: Multiple sources are detected, while cefbabeld was launched with MPSS option."
            "(Prefix=%s RouterID=%s Stored RouterID=MyID)\n",
            format_cefore_prefix(prefix, plen), format_eui64(id));
        return;
    }
    
BEGIN:; 

    src = find_source(id, prefix, plen, src_prefix, src_plen, 0, seqno);
    if(!src){  /// This is the first route update received
        if (refmetric < INFINITY) {
            /* Create Source Entry */
#ifdef DEB_MPSS
fprintf(stderr, "MPSS-[%s]: ----- Create Source entry -----\n", __FUNCTION__);
#endif
            src = find_source(id, prefix, plen, src_prefix, src_plen, 1, seqno);
            if(src == NULL) {
                return;
            }
            /* Set my_FD */
            my_FD = MIN((int)refmetric + neighbour_cost(neigh), INFINITY);
            src->metric = my_FD; 
            /* Create Route Entry */
            new_route = create_route_entry_mp(src, seqno, refmetric,
                                           interval, neigh, nexthop, port, 
                                           channels, channels_len);
            /* Add FIB */
            rc = cefore_fib_add_req_send (
                        new_route->src->prefix, new_route->src->plen, new_route->nexthop, new_route->port,
                        new_route->neigh->ifp->name);
            if(rc < 0) {
                return;
            }
            send_update(NULL, 1, prefix, plen, src_prefix, src_plen);
        }
        return; 
    }
    
    src = find_source(id, prefix, plen, src_prefix, src_plen, 0, seqno);
    if(!src){
        return; 
    }

    if(seqno_compare(seqno, src->seqno/*my_seqNo*/) > 0) {
        clear_all_routes_mpss (prefix, plen, src_prefix, src_plen);
        if(refmetric == INFINITY) {
            return;
        }
        delete_source_mp(src);
        goto BEGIN;
    }

    /// Remove retracted route (same as Babel)
    route = find_route(prefix, plen, src_prefix, src_plen, neigh, nexthop);
    if (refmetric == INFINITY) {
        debugf("[%s]: RCVed INFINITY UPDATE(id=%s, prefix=%s, seqno=%u, refmetric=%u, nexthop=%s, port=%u)\n",
               __FUNCTION__, 
               format_eui64(id), format_cefore_prefix(prefix, plen), seqno, refmetric, 
               format_address(nexthop), port);
        if(route) {
            removeNbFromRoute_mp(prefix, plen, src_prefix, src_plen, neigh, nexthop);
            rc = find_any_route_mp(prefix, plen, src_prefix, src_plen);
            if(rc == 0){
                /// Send retract update if I have no feasible routes
                really_send_update_mp(NULL, id, prefix, plen, src_prefix, src_plen, seqno, INFINITY, port);
            }
        }
        return; /// Ignore retract update from neighbor that is not in my nexthops
    }

#ifdef DEB_MPSS
fprintf(stderr, "MPSS--[%s]: ----- my_FD=%u, rcvd_metric+lc=%u -----\n"
    , __FUNCTION__, src->metric/*my_FD*/, MIN((int)refmetric + neighbour_cost(neigh), INFINITY));
#endif

    if(seqno_compare(seqno, src->seqno/*my_seqNo*/) < 0) {
        return; // Ignore this update
    }

    if (refmetric != INFINITY 
           && 
        src->metric/*my_FD*/ >= MIN((int)refmetric + neighbour_cost(neigh), INFINITY)) {
        if(route) {
            change_route_metric(route, refmetric, neighbour_cost(neigh), 0);
            route->time = now.tv_sec;
        } else {
            /* Create Route Entry */
            new_route = create_route_entry_mp(src, seqno, refmetric,
                                           interval, neigh, nexthop, port, 
                                           channels, channels_len);
            /* Add FIB */
            rc = cefore_fib_add_req_send (
                        new_route->src->prefix, new_route->src->plen, new_route->nexthop, new_route->port,
                        new_route->neigh->ifp->name);
            if(rc < 0) {
                return;
            } 
        }
    } 
    else {
        int metric;
        metric = MIN((int)refmetric + neighbour_cost(neigh), INFINITY);
        send_unfeasible_request_mp(neigh, seqno, metric, src);
   }
    
    return;

}

unsigned short
updateFeasibleDistance_mpss(const unsigned char *prefix, uint16_t plen,
           const unsigned char *src_prefix, unsigned char src_plen)
{
    int my_FD = -1;
    int metric;
    struct babel_route *route;
    struct source *src;

    int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);

    if(i < 0) {
        return INFINITY;
    }

    route = routes[i];
    src = route->src;

    
    while(route) {
        if (route->installed) {
            metric = route_metric(route);
            if (metric != INFINITY && my_FD < metric) {
                my_FD = metric;
            }
        }
        route = route->next;
    }
    if(my_FD == -1) {
        return INFINITY;
    }
    
    src->metric/*my_FD*/ = (unsigned short)my_FD; 
    
    return src->metric;
}
static void
clear_all_routes_mpss (const unsigned char *prefix, uint16_t plen,
                   const unsigned char *src_prefix, unsigned char src_plen)
{
    struct babel_route *route;
    while(1) {
        int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);
        if(i < 0)
            break;
        route = routes[i];
        /// Send retract update if I have no feasible route
        really_send_update_mp(NULL, route->src->id, 
                        route->src->prefix, route->src->plen, route->src->src_prefix, route->src->src_plen,
                        route->src->seqno, INFINITY, cefore_portnum);
        while(route) {
#ifdef CEFNETD_IF   //+++++ DEB for ROUTING +++++
fprintf(stderr, "IF-[%s(%d)] ===== CALL cefore_fib_del_req_send(%s, nh:%s) =====\n"
    , __FUNCTION__, __LINE__, format_cefore_prefix(route->src->prefix, route->src->plen)
    , format_address(route->nexthop)
);
#endif              //----- DEB for ROUTING -----
            /* Delete FIB */
             cefore_fib_del_req_send (
                        route->src->prefix, route->src->plen, route->nexthop, route->port,
                        route->neigh->ifp->name);
            clear_route_entry_mp(route);
        	break;
        }
    }
}   
    
#endif //----- ADD for MPSS -----

#ifndef BAELD_CODE //+++++ ADD for MPMS +++++
/**************************************************************************************************/    
/***** Multi Path Multi Source Functions                                               *****/
/**************************************************************************************************/    
/* This is called whenever we receive an update. */
void
update_route_mpms(const unsigned char *id,
             const unsigned char *prefix, uint16_t plen,
             const unsigned char *src_prefix, unsigned char src_plen,
             unsigned short seqno, unsigned short refmetric,
             unsigned short interval,
             struct neighbour *neigh, const unsigned char *nexthop, unsigned short port, 
             const unsigned char *channels, int channels_len)
{
    struct babel_route *route;
    struct babel_route *new_route = NULL;
    struct source *src;
    struct best_route *broute;
    int rc;
    struct xroute *xroute;
    if(memcmp(id, myid, 8) == 0) {
        debugf("Received Router-id is my ID.\n");
        return;
    }

#ifdef DEB_MPMS //+++++ DEB for MPMS +++++
    fprintf(stderr, "MPMS-[%s]: ===== RCVed UPDATE(id=%s, prefix=%s, seqno=%u, refmetric=%u, nexthop=%s, port=%u) =====\n",
                      __FUNCTION__, 
                      format_eui64(id), format_cefore_prefix(prefix, plen), seqno, refmetric, 
                      format_address(nexthop), port);
    fprintf(stderr, "MPMS-[%s]: ===== Nb_addr=%s, My_addr=%s =====\n",
                      __FUNCTION__, 
                      format_address(neigh->address),
                      format_address(neigh->ifp->ll[0]));
#endif //----- DEB for MPMS -----

    /* Check for the existence of static prefix name*/
    xroute = find_xroute_mp(prefix, plen);
    if(xroute){
        debugf("Received same prefix name.\n");
        src = find_source(id, prefix, plen, src_prefix, src_plen, 0, seqno);
        if(src!=NULL) {
            removeOldRoutes_mpms(prefix, plen, src_prefix, src_plen, id);
            delete_bestroute_entry(prefix, plen, src_prefix, src_plen);
            delete_source_mp(src);
        }
        return;
    }

BEGIN:;
    
    broute = find_bestroute(prefix, plen, src_prefix, src_plen, 0);
    if(!broute) { /// This is the first route update
        if (refmetric < INFINITY) {
            /* Check pendig source record */
            src = find_source(id, prefix, plen, src_prefix, src_plen, 0, seqno);
            if(!src) {
                /* Create Source Entry (If entry does not exist, entry is created) */
                src = find_source(id, prefix, plen, src_prefix, src_plen, 1, seqno);
                if(!src) {
                    return;
                }
            }
            /* Create Route Entry */
            new_route = create_route_entry_mp(src, seqno, refmetric,
                                           interval, neigh, nexthop, port, 
                                           channels, channels_len);
            /* Add FIB */
            rc = cefore_fib_add_req_send (
                        new_route->src->prefix, new_route->src->plen, new_route->nexthop, new_route->port,
                        new_route->neigh->ifp->name);
            if(rc < 0) {
                return;
            } 
#ifdef DEB_MPMS
fprintf(stderr, "MPMS-[%s]: ----- Create Best Route entry -----\n", __FUNCTION__);
#endif
            broute = find_bestroute(prefix, plen, src_prefix, src_plen, 1/*create*/);
            if(!broute) {
                return;
            } 
            broute->my_FD = MIN((int)refmetric + neighbour_cost(neigh), INFINITY);
            memcpy(broute->my_sourceId, id, 8);
            broute->my_seqNo = seqno;
            send_update(NULL, 1, prefix, plen, src_prefix, src_plen);
            return;
        }
    }
    
    if (refmetric == INFINITY) {  /// Remove retracted route (same as Babel)
        struct best_route last_bestroute;
        route = find_route(prefix, plen, src_prefix, src_plen, neigh, nexthop);
        if(route) {
            broute = find_bestroute(prefix, plen, src_prefix, src_plen, 0);
            removeNbFromRoute_mp(prefix, plen, src_prefix, src_plen, neigh, nexthop);
            if(broute){
                memcpy(&last_bestroute, broute, sizeof(struct best_route));
            } else {
                return;
            }
            broute = updateFeasibleDistance_mpms(prefix, plen, src_prefix, src_plen);
            if (broute){
                if(memcmp(&last_bestroute, broute, sizeof(struct best_route)) != 0){
                    send_update(NULL, 1, prefix, plen, src_prefix, src_plen);
                }
            }
        	else {
                really_send_update_mp(NULL, last_bestroute.my_sourceId,
                                      last_bestroute.prefix, last_bestroute.plen,
                                      last_bestroute.src_prefix, last_bestroute.src_plen,
                                      last_bestroute.my_seqNo, 
		                              INFINITY, port);
            }
        }
        return; /// Ignore retract update from neighbor that is not in my nexthops
    }
    
    src = find_source(id, prefix, plen, src_prefix, src_plen, 0, seqno);
    if(src!=NULL && (seqno_compare(seqno, src->seqno) < 0)) {  /// Ignore old sequence number
        return;
    }
    
    if(!src) {
        /* Create Source Entry (If entry does not exist, entry is created) */
        src = find_source(id, prefix, plen, src_prefix, src_plen, 1, seqno);
    } else if (seqno_compare(seqno, src->seqno) > 0) {
        removeOldRoutes_mpms(prefix, plen, src_prefix, src_plen, id);
        delete_bestroute_entry(prefix, plen, src_prefix, src_plen);
        delete_source_mp(src);
        goto BEGIN;
    }

    if(!isFassible_mpms(refmetric, neigh, prefix, plen, src_prefix, src_plen)){ 
        int metric;
        metric = MIN((int)refmetric + neighbour_cost(neigh), INFINITY);
        send_unfeasible_request_mp(neigh, seqno, metric, src);
        /// Ignore infeasible route
        return;
    }
    
    route = find_route(prefix, plen, src_prefix, src_plen, neigh, nexthop);
    if(route && route->src == src) {
        change_route_metric(route, refmetric, neighbour_cost(neigh), 0);
        route->time = now.tv_sec;
    } else {
        if(route){
            removeNbFromRoute_mp(prefix, plen, src_prefix, src_plen, neigh, nexthop);
        }
        /* Create Route Entry */
        new_route = create_route_entry_mp(src, seqno, refmetric,
                                       interval, neigh, nexthop, port, 
                                       channels, channels_len);
        /* Add FIB */
        rc = cefore_fib_add_req_send (
                        new_route->src->prefix, new_route->src->plen, 
                        new_route->nexthop, new_route->port,
                        new_route->neigh->ifp->name);
        if(rc < 0) {
            return;
        } 
    }
    
    {
        struct best_route last_bestroute;
        broute = find_bestroute(prefix, plen, src_prefix, src_plen, 0);
        if(!broute) {
            return;
        }
        memcpy(&last_bestroute, broute, sizeof(struct best_route));
        broute = updateFeasibleDistance_mpms(prefix, plen, src_prefix, src_plen);
        if(broute){
            removeInfeasble_mpms(prefix, plen, src_prefix, src_plen, broute->my_FD);
            if (memcmp(&last_bestroute, broute, sizeof(struct best_route)) != 0) {
                send_update(NULL, 1, prefix, plen, src_prefix, src_plen);
            }
        }
       	else {
            really_send_update_mp(NULL, last_bestroute.my_sourceId,
                                  last_bestroute.prefix, last_bestroute.plen,
                                  last_bestroute.src_prefix, last_bestroute.src_plen,
                                  last_bestroute.my_seqNo, 
	                              INFINITY, port);
        }
    	
    }
    return;
}
struct babel_route *
find_route_assoc_broute(struct best_route* broute,
                        const unsigned char *prefix, uint16_t plen,
                        const unsigned char *src_prefix, unsigned char src_plen)
{
    int i; 
    struct babel_route *route;
    i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);

    if(i < 0){
        return NULL;
    }

    route = routes[i];

    while(route) {
        if(broute->my_seqNo == route->seqno 
            && (memcmp(broute->my_sourceId, route->src->id, 8) == 0)){
            return route;
        }
        route = route->next;
    }
    return NULL;
}
struct best_route*
find_bestroute(const unsigned char *prefix, uint16_t plen,
               const unsigned char *src_prefix, unsigned char src_plen,
               int create)
{
    int n = -1;
    int i = find_bestroute_slot(prefix, plen, src_prefix, src_plen, &n);
    struct best_route *broute;

    if(i >= 0)
        return bestroutes[i];

    if(!create)
        return NULL;

    broute = calloc(1, sizeof(struct best_route));
    if(broute == NULL) {
        perror("malloc(bestroute)");
        return NULL;
    }

    memcpy(broute->prefix, prefix, plen);
    broute->plen = plen;
    memcpy(broute->src_prefix, src_prefix, 16);
    broute->src_plen = src_plen;
    if(bestroute_slots >= max_bestroute_slots)
        resize_bestroute_table(max_bestroute_slots < 1 ? 8 : 2 * max_bestroute_slots);
    if(bestroute_slots >= max_bestroute_slots) {
        free(broute);
        return NULL;
    }
    if(n < bestroute_slots)
        memmove(bestroutes + n + 1, bestroutes + n,
                (bestroute_slots - n) * sizeof(struct best_route*));
    bestroute_slots++;
    bestroutes[n] = broute;

    return broute;
}
struct best_route *
updateFeasibleDistance_mpms(
             const unsigned char *prefix, uint16_t plen,
             const unsigned char *src_prefix, unsigned char src_plen)
{
    unsigned short my_FD = INFINITY;
    unsigned char my_sourceId[8];
    unsigned short my_seqNo=0;
    int i;
    unsigned short fd;
    struct babel_route *route;
    struct best_route *broute=NULL;
    int rc;
	memset(my_sourceId, 0xFF, 8);

    i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);

    if(i < 0){
        rc = exist_source_mp(prefix, plen, src_prefix, src_plen);
        if(!rc){
            delete_bestroute_entry(prefix, plen, src_prefix, src_plen);
        }
        return NULL;
    } 

    route = routes[i];

    while(route) {
        fd = MIN((int)route->refmetric + route->cost, INFINITY);
        if ((fd < my_FD) /// my_FD = minimum (metric+linkcost) & smallest sourceId
              ||
            ((fd == my_FD) && (memcmp(my_sourceId, route->src->id, 8) > 0))){
            my_FD = fd;
            memcpy(my_sourceId, route->src->id, 8);
            my_seqNo = route->src->seqno;
        }
        route = route->next;
    }
    if(my_FD == INFINITY) {
        rc = exist_source_mp(prefix, plen, src_prefix, src_plen);
        if(!rc){
            delete_bestroute_entry(prefix, plen, src_prefix, src_plen);
        }
        return NULL;
    }
    if(my_FD < INFINITY){
        broute = find_bestroute(prefix, plen, src_prefix, src_plen, 0);
        if(broute){
            broute->my_FD = my_FD;
            memcpy(broute->my_sourceId, my_sourceId, 8);
            broute->my_seqNo = my_seqNo;
        }
    }
    return broute;
}
void dump_best_route(FILE *out)
{
    int i;
    struct best_route *broute;

    fprintf(out, "----- bestroutes -----\n");
    
    for(i=0; i < bestroute_slots; i++) {
        broute = bestroutes[i];
        fprintf(out, "%s%s%s my_sourceId=%s my_seqNo=%u my_FD=%u\n",
                format_cefore_prefix(broute->prefix, broute->plen),
                broute->src_plen > 0 ? " from " : "",
                broute->src_plen > 0 ?
                format_prefix(broute->src_prefix, broute->src_plen) : "",
                format_eui64(broute->my_sourceId),
                broute->my_seqNo, broute->my_FD);
    }
}

static void
removeInfeasble_mpms(
            const unsigned char *prefix, uint16_t plen,
            const unsigned char *src_prefix, unsigned char src_plen,
            unsigned short my_fd)
{
                
    struct babel_route *route;
	int rcdnum;
    while(1) {
        int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);
        if(i < 0){
            break;
        }
        route = routes[i];
	    rcdnum = 0;
        do {
    	    rcdnum++;
    	    route = route->next;
	    } while (route != NULL);
        route = routes[i];
        while(route) {
    	    rcdnum--;
            if(!isFassible_mpms(route->refmetric, route->neigh, prefix, plen, src_prefix, src_plen)){
#ifdef CEFNETD_IF   //+++++ DEB for ROUTING +++++
fprintf(stderr, "IF-[%s(%d)] ===== CALL cefore_fib_del_req_send(%s, nh:%s) =====\n"
    , __FUNCTION__, __LINE__, format_cefore_prefix(route->src->prefix, route->src->plen)
    , format_address(route->nexthop)
);
#endif              //----- DEB for ROUTING -----
                /* Delete FIB */
                cefore_fib_del_req_send (
                            route->src->prefix, route->src->plen, route->nexthop, route->port,
                            route->neigh->ifp->name);
                clear_route_entry_mp(route);
                break;
            } else {
                route = route->next;
            }
        }
	    if (rcdnum==0){
		    break;
	    }
    }
}
static int
bestroute_compare(
               const unsigned char *prefix, uint16_t plen,
               const unsigned char *src_prefix, unsigned char src_plen,
               const struct best_route *broute)
{
    
    int rc;

    if(plen < broute->plen)
        return -1;
    if(plen > broute->plen)
        return 1;

    rc = memcmp(prefix, broute->prefix, plen);
    if(rc != 0)
        return rc;

    if(src_plen < broute->src_plen)
        return -1;
    if(src_plen > broute->src_plen)
        return 1;

    rc = memcmp(src_prefix, broute->src_prefix, 16);
    if(rc != 0)
        return rc;

    return 0;
}

static int
find_bestroute_slot(
                 const unsigned char *prefix, uint16_t plen,
                 const unsigned char *src_prefix, unsigned char src_plen,
                 int *new_return)
{
    int p, m, g, c;

    if(bestroute_slots < 1) {
        if(new_return)
            *new_return = 0;
        return -1;
    }

    p = 0; g = bestroute_slots - 1;

    do {
        m = (p + g) / 2;
        c = bestroute_compare(prefix, plen, src_prefix, src_plen, bestroutes[m]);
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

static int
resize_bestroute_table(int new_slots)
{
    struct best_route **new_bestroutes;
    assert(new_slots >= bestroute_slots);

    if(new_slots == 0) {
        new_bestroutes = NULL;
        free(bestroutes);
    } else {
        new_bestroutes = realloc(bestroutes, new_slots * sizeof(struct best_route*));
        if(new_bestroutes == NULL)
            return -1;
    }

    max_bestroute_slots = new_slots;
    bestroutes = new_bestroutes;
    return 1;
}

void
delete_bestroute_entry(
             const unsigned char *prefix, uint16_t plen,
             const unsigned char *src_prefix, unsigned char src_plen)
{
    int i = 0, j = 0;
    int c;
    
    while(i < bestroute_slots) {
        struct best_route *broute = bestroutes[i];
        c = bestroute_compare(prefix, plen, src_prefix, src_plen, broute);
        if(c == 0) {
            free(broute);
            bestroutes[i] = NULL;
            i++;
        } else {
            if(j < i) {
                bestroutes[j] = bestroutes[i];
                bestroutes[i] = NULL;
            }
            i++;
            j++;
        }
    }
    bestroute_slots = j;
}


static int
isFassible_mpms(
             unsigned short refmetric, struct neighbour *neigh,
             const unsigned char *prefix, uint16_t plen,
             const unsigned char *src_prefix, unsigned char src_plen)
{
    struct best_route *broute;

    broute = find_bestroute(prefix, plen, src_prefix, src_plen, 0);
    if(broute == NULL) {
       return 1;
     }
    
    if (broute->my_FD > refmetric) {
        return 1;
    }
    if ((broute->my_FD == refmetric) && (memcmp(neigh->ifp->ll[0], neigh->address, 16) > 0)) {
        return 1;
    }
    
    return 0;
}
void
removeOldRoutes_mpms(
                const unsigned char *prefix, uint16_t plen,
                const unsigned char *src_prefix, unsigned char src_plen,
                const unsigned char *id)
{
    struct babel_route *route;
	int rcdnum;
    while(1) {
        int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);
        if(i < 0)
            break;
        route = routes[i];
	    rcdnum = 0;
        do {
    	    rcdnum++;
    	    route = route->next;
	    } while (route != NULL);
        route = routes[i];
        while(route) {
        	rcdnum--;
            if(memcmp(route->src->id, id, 8) == 0){
#ifdef CEFNETD_IF   //+++++ DEB for ROUTING +++++
fprintf(stderr, "IF-[%s(%d)] ===== CALL cefore_fib_del_req_send(%s, nh:%s) =====\n"
    , __FUNCTION__, __LINE__, format_cefore_prefix(route->src->prefix, route->src->plen)
    , format_address(route->nexthop)
);
#endif              //----- DEB for ROUTING -----
                /* Delete FIB */
                cefore_fib_del_req_send (
                        route->src->prefix, route->src->plen, route->nexthop, route->port,
                        route->neigh->ifp->name);
                clear_route_entry_mp(route);
            	break;
            } else {
                route = route->next;
            }
        }
    	if (rcdnum==0){
	    	break;
	    }
    }
}
void
clear_all_routes_mpms (const unsigned char *prefix, uint16_t plen,
                   const unsigned char *src_prefix, unsigned char src_plen)
{
    struct babel_route *route;
    while(1) {
        int i = find_route_slot(prefix, plen, src_prefix, src_plen, NULL);
        if(i < 0)
            break;
        route = routes[i];
        while(route) {
#ifdef CEFNETD_IF   //+++++ DEB for ROUTING +++++
fprintf(stderr, "IF-[%s(%d)] ===== CALL cefore_fib_del_req_send(%s, nh:%s) =====\n"
    , __FUNCTION__, __LINE__, format_cefore_prefix(route->src->prefix, route->src->plen)
    , format_address(route->nexthop)
);
#endif              //----- DEB for ROUTING -----
            /* Delete FIB */
             cefore_fib_del_req_send (
                        route->src->prefix, route->src->plen, route->nexthop, route->port,
                        route->neigh->ifp->name);
            clear_route_entry_mp(route);
        	break;
        }
    }
}   
    
#endif //----- ADD for MPMS -----

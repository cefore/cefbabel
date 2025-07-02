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

#define DIVERSITY_NONE 0
#define DIVERSITY_INTERFACE_1 1
#define DIVERSITY_CHANNEL_1 2
#define DIVERSITY_CHANNEL 3

struct babel_route {
    struct source *src;
    unsigned short refmetric;
    unsigned short cost;
    unsigned short add_metric;
    unsigned short seqno;
    struct neighbour *neigh;
    unsigned char nexthop[16];
#ifndef BABELD_CODE //+++++ ADD +++++
    unsigned short port;
#endif //----- ADD -----
    time_t time;
    unsigned short hold_time;    /* in seconds */
    unsigned short smoothed_metric; /* for route selection */
    time_t smoothed_metric_time;
    short installed;
    short channels_len;
    unsigned char *channels;
    struct babel_route *next;
};

#ifndef BABELD_CODE //+++++ ADD for MPMS +++++
struct best_route {
    unsigned char prefix[NAME_PREFIX_LEN];
    uint16_t plen;
    unsigned char src_prefix[16];
    unsigned char src_plen;
    unsigned char my_sourceId[8];
    unsigned short my_seqNo;
    unsigned short my_FD;
};
#endif //----- ADD for MPMS -----

#define ROUTE_ALL 0
#define ROUTE_INSTALLED 1
#define ROUTE_SS_INSTALLED 2
struct route_stream;

extern struct babel_route **routes;
extern int kernel_metric, allow_duplicates, reflect_kernel_metric;
extern int diversity_kind, diversity_factor;

static inline int
route_metric(const struct babel_route *route)
{
    int m = (int)route->refmetric + route->cost + route->add_metric;
    return MIN(m, INFINITY);
}

static inline int
route_metric_noninterfering(const struct babel_route *route)
{
    int m =
        (int)route->refmetric +
        (diversity_factor * route->cost + 128) / 256 +
        route->add_metric;
    m = MAX(m, route->refmetric + 1);
    return MIN(m, INFINITY);
}

#ifdef BABELD_CODE //+++++ REPLACE +++++
struct babel_route *find_route(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
struct babel_route *find_route(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                        const unsigned char *src_prefix, unsigned char src_plen,
                        struct neighbour *neigh, const unsigned char *nexthop);
struct babel_route *find_installed_route(const unsigned char *prefix,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                        unsigned char plen, const unsigned char *src_prefix,
#else // CEFBABELD
                        uint16_t plen, const unsigned char *src_prefix,
#endif //----- REPLACE -----
                        unsigned char src_plen);


int installed_routes_estimate(void);
void flush_route(struct babel_route *route);
void flush_all_routes(void);
void flush_neighbour_routes(struct neighbour *neigh);
void flush_interface_routes(struct interface *ifp, int v4only);
struct route_stream *route_stream(int which);
struct babel_route *route_stream_next(struct route_stream *stream);
void route_stream_done(struct route_stream *stream);
int metric_to_kernel(int metric);
void install_route(struct babel_route *route);
void uninstall_route(struct babel_route *route);
int route_feasible(struct babel_route *route);
int route_old(struct babel_route *route);
int route_expired(struct babel_route *route);
int route_interferes(struct babel_route *route, struct interface *ifp);
int update_feasible(struct source *src,
                    unsigned short seqno, unsigned short refmetric);
void change_smoothing_half_life(int half_life);
int route_smoothed_metric(struct babel_route *route);
struct babel_route *find_best_route(const unsigned char *prefix,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                                    unsigned char plen,
#else // CEFBABELD
                                    uint16_t plen,
#endif //----- REPLACE -----
                                    const unsigned char *src_prefix,
                                    unsigned char src_plen,
                                    int feasible, struct neighbour *exclude);
#ifdef BABELD_CODE //+++++ REPLACE +++++
struct babel_route *install_best_route(const unsigned char prefix[16],
                                 unsigned char plen);
#else // CEFBABELD
struct babel_route *install_best_route(const unsigned char prefix[NAME_PREFIX_LEN],
                                 uint16_t plen);
#endif //----- REPLACE -----
void update_neighbour_metric(struct neighbour *neigh, int changed);
void update_interface_metric(struct interface *ifp);
void update_route_metric(struct babel_route *route);
struct babel_route *update_route(const unsigned char *id,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                           const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                           const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                           const unsigned char *src_prefix,
                           unsigned char src_plen,
                           unsigned short seqno, unsigned short refmetric,
                           unsigned short interval, struct neighbour *neigh,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                           const unsigned char *nexthop,
#else // CEFBABELD
                           const unsigned char *nexthop, unsigned short port, 
#endif //----- REPLACE -----
                           const unsigned char *channels, int channels_len);
void retract_neighbour_routes(struct neighbour *neigh);
void send_unfeasible_request(struct neighbour *neigh, int force,
                             unsigned short seqno, unsigned short metric,
                             struct source *src);
void consider_route(struct babel_route *route);
void send_triggered_update(struct babel_route *route,
                           struct source *oldsrc, unsigned oldmetric);
void route_changed(struct babel_route *route,
                   struct source *oldsrc, unsigned short oldmetric);
void route_lost(struct source *src, unsigned oldmetric);
void expire_routes(void);

#ifndef BABELD_CODE //+++++ ADD for STAT +++++
int exist_installed_route();
#endif //----- ADD for STAT -----
#ifndef BABELD_CODE //+++++ ADD +++++
void DEB_RPTIN_ROUTE_TABLE();
#endif //----- ADD -----
#ifndef BABELD_CODE //+++++ ADD for MP +++++
void send_unfeasible_request_mp(struct neighbour *neigh,
                        unsigned short seqno, unsigned short metric,
                        struct source *src);
#endif //----- ADD for MP -----
#ifndef BABELD_CODE //+++++ ADD for MPSS +++++
void update_route_mpss(const unsigned char *id,
                            const unsigned char *prefix, uint16_t plen,
                            const unsigned char *src_prefix,
                            unsigned char src_plen,
                            unsigned short seqno, unsigned short refmetric,
                            unsigned short interval, struct neighbour *neigh,
                            const unsigned char *nexthop, unsigned short port, 
                            const unsigned char *channels, int channels_len);
unsigned short updateFeasibleDistance_mpss(const unsigned char *prefix, uint16_t plen,
                            const unsigned char *src_prefix, unsigned char src_plen);
void removeOldRoutes_mpms(
            const unsigned char *prefix, uint16_t plen,
            const unsigned char *src_prefix, unsigned char src_plen,
            const unsigned char *id);
void clear_all_routes_mpms (const unsigned char *prefix, uint16_t plen,
                   const unsigned char *src_prefix, unsigned char src_plen);
void delete_bestroute_entry(
             const unsigned char *prefix, uint16_t plen,
             const unsigned char *src_prefix, unsigned char src_plen);
#endif //----- ADD for MPSS -----

#ifndef BABELD_CODE //+++++ ADD for MPMS +++++
void update_route_mpms(const unsigned char *id,
                            const unsigned char *prefix, uint16_t plen,
                            const unsigned char *src_prefix,
                            unsigned char src_plen,
                            unsigned short seqno, unsigned short refmetric,
                            unsigned short interval, struct neighbour *neigh,
                            const unsigned char *nexthop, unsigned short port, 
                            const unsigned char *channels, int channels_len);
struct babel_route* find_route_assoc_broute(struct best_route* broute,
                        const unsigned char *prefix, uint16_t plen,
                        const unsigned char *src_prefix, unsigned char src_plen);
struct best_route* find_bestroute(
            const unsigned char *prefix, uint16_t plen,
            const unsigned char *src_prefix, unsigned char src_plen,
            int create);
struct best_route * updateFeasibleDistance_mpms(
                            const unsigned char *prefix, uint16_t plen,
                            const unsigned char *src_prefix, unsigned char src_plen);
void dump_best_route(FILE *out);
#endif //----- ADD for MPMS -----

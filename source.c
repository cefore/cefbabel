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
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>

#include "babeld.h"
#include "util.h"
#include "source.h"
#include "interface.h"
#include "route.h"

static struct source **sources = NULL;
static int source_slots = 0, max_source_slots = 0;

static int
source_compare(const unsigned char *id,
#ifdef BABELD_CODE //+++++ REPLACE +++++
               const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
               const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
               const unsigned char *src_prefix, unsigned char src_plen,
               const struct source *src)
{
    int rc;

    rc = memcmp(id, src->id, 8);
    if(rc != 0)
        return rc;

    if(plen < src->plen)
        return -1;
    if(plen > src->plen)
        return 1;

#ifdef BABELD_CODE //+++++ REPLACE +++++
    rc = memcmp(prefix, src->prefix, 16);
#else // CEFBABELD
    rc = memcmp(prefix, src->prefix, plen);
#endif //----- REPLACE -----
    if(rc != 0)
        return rc;

    if(src_plen < src->src_plen)
        return -1;
    if(src_plen > src->src_plen)
        return 1;

    rc = memcmp(src_prefix, src->src_prefix, 16);
    if(rc != 0)
        return rc;

    return 0;
}

static int
find_source_slot(const unsigned char *id,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                 const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                 const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                 const unsigned char *src_prefix, unsigned char src_plen,
                 int *new_return)
{
    int p, m, g, c;

    if(source_slots < 1) {
        if(new_return)
            *new_return = 0;
        return -1;
    }

    p = 0; g = source_slots - 1;

    do {
        m = (p + g) / 2;
        c = source_compare(id, prefix, plen, src_prefix, src_plen, sources[m]);
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
resize_source_table(int new_slots)
{
    struct source **new_sources;
    assert(new_slots >= source_slots);

    if(new_slots == 0) {
        new_sources = NULL;
        free(sources);
    } else {
        new_sources = realloc(sources, new_slots * sizeof(struct source*));
        if(new_sources == NULL)
            return -1;
    }

    max_source_slots = new_slots;
    sources = new_sources;
    return 1;
}

struct source*
find_source(const unsigned char *id,
#ifdef BABELD_CODE //+++++ REPLACE +++++
            const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
            const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
            const unsigned char *src_prefix, unsigned char src_plen,
            int create, unsigned short seqno)
{
    int n = -1;
    int i = find_source_slot(id, prefix, plen, src_prefix, src_plen, &n);
    struct source *src;

    if(i >= 0)
        return sources[i];

    if(!create)
        return NULL;

    src = calloc(1, sizeof(struct source));
    if(src == NULL) {
        perror("malloc(source)");
        return NULL;
    }

    memcpy(src->id, id, 8);
#ifdef BABELD_CODE //+++++ REPLACE +++++
    memcpy(src->prefix, prefix, 16);
#else // CEFBABELD
    memcpy(src->prefix, prefix, plen);
#endif //----- REPLACE -----
    src->plen = plen;
    memcpy(src->src_prefix, src_prefix, 16);
    src->src_plen = src_plen;
    src->seqno = seqno;
    src->metric = INFINITY;
    src->time = now.tv_sec;

    if(source_slots >= max_source_slots)
        resize_source_table(max_source_slots < 1 ? 8 : 2 * max_source_slots);
    if(source_slots >= max_source_slots) {
        free(src);
        return NULL;
    }
    if(n < source_slots)
        memmove(sources + n + 1, sources + n,
                (source_slots - n) * sizeof(struct source*));
    source_slots++;
    sources[n] = src;

    return src;
}

struct source *
retain_source(struct source *src)
{
    assert(src->route_count < 0xffff);
    src->route_count++;
    return src;
}

void
release_source(struct source *src)
{
    assert(src->route_count > 0);
    src->route_count--;
}

void
update_source(struct source *src,
              unsigned short seqno, unsigned short metric)
{
    if(metric >= INFINITY)
        return;

    /* If a source is expired, pretend that it doesn't exist and update
       it unconditionally.  This makes ensures that old data will
       eventually be overridden, and prevents us from getting stuck if
       a router loses its sequence number. */
    if(src->time < now.tv_sec - SOURCE_GC_TIME ||
       seqno_compare(src->seqno, seqno) < 0 ||
       (src->seqno == seqno && src->metric > metric)) {
        src->seqno = seqno;
        src->metric = metric;
    }
    src->time = now.tv_sec;
}

void
expire_sources()
{
    int i = 0, j = 0;
#ifdef BABELD_CODE //+++++ REPLACE for MP +++++
    while(i < source_slots) {
        struct source *src = sources[i];
        if(src->time > now.tv_sec)
            /* clock stepped */
            src->time = now.tv_sec;

        if(src->route_count == 0 && src->time < now.tv_sec - SOURCE_GC_TIME) {
            free(src);
            sources[i] = NULL;
            i++;
        } else {
            if(j < i) {
                sources[j] = sources[i];
                sources[i] = NULL;
            }
            i++;
            j++;
        }
    }
    source_slots = j;
#else // CEFBABELD
    if (route_ctrl_type == ROUTE_CTRL_TYPE_MM) {
        unsigned char prefix[NAME_PREFIX_LEN];
        uint16_t plen;
        unsigned char src_prefix[16];
        unsigned char src_plen;
        while(i < source_slots) {
            struct source *src = sources[i];
            if(src->time > now.tv_sec)
                /* clock stepped */
                src->time = now.tv_sec;

            if(src->route_count == 0 && src->time < now.tv_sec - SOURCE_GC_TIME) {
                plen = src->plen;
                memcpy(prefix, src->prefix, plen);
                src_plen = src->src_plen;
                memset(src_prefix, 0, 16);
                memcpy(src_prefix, src->src_prefix, src_plen);
                free(src);
                sources[i] = NULL;
            	memmove(sources + i, sources + i + 1,
                        (source_slots - i) * sizeof(struct source*));
		        source_slots--;
                updateFeasibleDistance_mpms(prefix, plen, src_prefix, src_plen);
            } else {
                i++;
            }
        }
    } else { // ROUTE_CTRL_TYPE_S/MS
        while(i < source_slots) {
            struct source *src = sources[i];
            if(src->time > now.tv_sec)
                /* clock stepped */
                src->time = now.tv_sec;

            if(src->route_count == 0 && src->time < now.tv_sec - SOURCE_GC_TIME) {
                free(src);
                sources[i] = NULL;
                i++;
            } else {
                if(j < i) {
                    sources[j] = sources[i];
                    sources[i] = NULL;
                }
                i++;
                j++;
            }
        }
        source_slots = j;
    }
#endif //----- REPLACE for MP
}

void
check_sources_released(void)
{
    int i;

    for(i = 0; i < source_slots; i++) {
        struct source *src = sources[i];

        if(src->route_count != 0)
            fprintf(stderr, "Warning: source %s %s has refcount %d.\n",
                    format_eui64(src->id),
                    format_prefix(src->prefix, src->plen),
                    (int)src->route_count);
    }
}
#ifndef BABELD_CODE //+++++ ADD for MP +++++
static int
prefix_compare_mp(const unsigned char *prefix, uint16_t plen,
               const unsigned char *src_prefix, unsigned char src_plen,
               const struct source *src)
{
    int rc;
    if(plen < src->plen) {
        return -1;
    }
    if(plen > src->plen) {
        return 1;
    }

    rc = memcmp(prefix, src->prefix, plen);
    if(rc != 0) {
        return rc;
    }

    if(src_plen < src->src_plen) { 
        return -1;
    }
    if(src_plen > src->src_plen) {
        return 1;
    }

    rc = memcmp(src_prefix, src->src_prefix, 16);
    if(rc != 0) {
        return rc;
    }

    return 0;
}
int 
exist_source_mp(
                 const unsigned char *prefix, uint16_t plen,
                 const unsigned char *src_prefix, unsigned char src_plen)
{
    int exist=0;

    if(source_slots < 1) {
        return exist;
    }

    int p, m, g, c;
    p = 0; g = source_slots - 1;

    do {
        m = (p + g) / 2;
        c = prefix_compare_mp(prefix, plen, src_prefix, src_plen, sources[m]);
    	if(c == 0) {
            exist = 1;
            break;
    	} else if(c < 0) {
            g = m - 1;
    	} else {
            p = m + 1;
    	}
    } while(p <= g);

    return exist;
}
void
delete_source_mp(struct source * delsrc)
{
    int i = 0, j = 0;
    
    
    while(i < source_slots) {
        struct source *src = sources[i];
        
        if(delsrc == src) {
            assert(src->route_count == 0);
            free(src);
            sources[i] = NULL;
            i++;
        } else {
            if(j < i) {
                sources[j] = sources[i];
                sources[i] = NULL;
            }
            i++;
            j++;
        }
    }
    source_slots = j;
}
struct source*
get_src_rcd_mp(const unsigned char *prefix, uint16_t plen,
              const unsigned char *src_prefix, unsigned char src_plen)
{
    struct source* exist=NULL;

    if(source_slots < 1) {
        return exist;
    }

    int p, m, g, c;
    p = 0; g = source_slots - 1;

    do {
        m = (p + g) / 2;
        c = prefix_compare_mp(prefix, plen, src_prefix, src_plen, sources[m]);
    	
    	if(c == 0) {
            exist = sources[m];
            break;
    	} else if(c < 0) {
            g = m - 1;
    	} else {
            p = m + 1;
    	}
    } while(p <= g);

    return exist;
}

#endif //----- ADD for MP -----

#ifndef BABELD_CODE //+++++ ADD for MPSS +++++
unsigned char *
find_other_source_mpss (const unsigned char *id,
                 const unsigned char *prefix, uint16_t plen,
                 const unsigned char *src_prefix, unsigned char src_plen)
{
    int rc;
    unsigned char *sid=NULL;

    if(source_slots < 1) {
        return sid;
    }

    int p, m, g, c;
    p = 0; g = source_slots - 1;

    do {
        m = (p + g) / 2;
        c = prefix_compare_mp(prefix, plen, src_prefix, src_plen, sources[m]);
    	if(c == 0) {
            rc = memcmp(id, sources[m]->id, 8);
            if(rc != 0) {
                sid = sources[m]->id;
            }
		    return sid;
    	} else if(c < 0) {
            g = m - 1;
    	} else {
            p = m + 1;
    	}
    } while(p <= g);

    return sid;
}

void dump_source(FILE *out)
{
    int i;

    fprintf(out, "----- sources -----\n");
    for(i=0; i < source_slots; i++) {
        struct source *src = sources[i];
        fprintf(out, "%s%s%s sourceId=%s seqno=%u my_FD=%u route_count=%u remain=%ld\n",
                format_cefore_prefix(src->prefix, src->plen),
                src->src_plen > 0 ? " from " : "",
                src->src_plen > 0 ?
                format_prefix(src->src_prefix, src->src_plen) : "",
                format_eui64(src->id),
                src->seqno, src->metric, src->route_count,
                SOURCE_GC_TIME - (now.tv_sec - src->time));
    }
}

#endif //----- ADD for MPSS -----

#ifndef BABELD_CODE //+++++ ADD for MPMS +++++
void
update_source_mpms( const unsigned char *prefix, uint16_t plen,
           const unsigned char *src_prefix, unsigned char src_plen)
{
    int metric;
    struct babel_route *route;
    
    route = find_installed_route(prefix, plen, src_prefix, src_plen);

    if(!route) {
        return;
    }

    while(route) {
        if (route->installed) {
            metric = route_metric(route);
            update_source(route->src, route->src->seqno, metric);
        }
        route = route->next;
    }
}
#endif //----- ADD for MPMS -----

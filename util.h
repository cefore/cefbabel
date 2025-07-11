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

#ifndef BABELD_CODE //+++++ ADD +++++
#include <stdint.h>
#endif //----- ADD -----

#define DO_NTOHS(_d, _s) \
    do { unsigned short _dd; \
         memcpy(&(_dd), (_s), 2); \
         _d = ntohs(_dd); } while(0)
#define DO_NTOHL(_d, _s) \
    do { unsigned int _dd; \
         memcpy(&(_dd), (_s), 4); \
         _d = ntohl(_dd); } while(0)
#define DO_HTONS(_d, _s) \
    do { unsigned short _dd; \
         _dd = htons(_s); \
         memcpy((_d), &(_dd), 2); } while(0)
#define DO_HTONL(_d, _s) \
    do { unsigned _dd; \
         _dd = htonl(_s); \
         memcpy((_d), &(_dd), 4); } while(0)

static inline int
seqno_compare(unsigned short s1, unsigned short s2)
{
    if(s1 == s2)
        return 0;
    else
        return ((s2 - s1) & 0x8000) ? 1 : -1;
}

static inline short
seqno_minus(unsigned short s1, unsigned short s2)
{
    return (short)((s1 - s2) & 0xFFFF);
}

static inline unsigned short
seqno_plus(unsigned short s, int plus)
{
    return ((s + plus) & 0xFFFF);
}

/* Returns a time in microseconds on 32 bits (thus modulo 2^32,
   i.e. about 4295 seconds). */
static inline unsigned int
time_us(const struct timeval t)
{
    return (unsigned int) (t.tv_sec * 1000000 + t.tv_usec);
}

int roughly(int value);
void timeval_minus(struct timeval *d,
                   const struct timeval *s1, const struct timeval *s2);
unsigned timeval_minus_msec(const struct timeval *s1, const struct timeval *s2)
    ATTRIBUTE ((pure));
void timeval_add_msec(struct timeval *d,
                      const struct timeval *s, int msecs);
int timeval_compare(const struct timeval *s1, const struct timeval *s2)
    ATTRIBUTE ((pure));
void timeval_min(struct timeval *d, const struct timeval *s);
void timeval_min_sec(struct timeval *d, time_t secs);
int parse_nat(const char *string) ATTRIBUTE ((pure));
int parse_thousands(const char *string) ATTRIBUTE ((pure));
void do_debugf(int level, const char *format, ...)
    ATTRIBUTE ((format (printf, 2, 3))) COLD;
int in_prefix(const unsigned char *restrict address,
#ifdef BABELD_CODE //+++++ REPLACE +++++
              const unsigned char *restrict prefix, unsigned char plen)
#else // CEFBABELD
              const unsigned char *restrict prefix, uint16_t plen)
#endif //----- REPLACE -----
    ATTRIBUTE ((pure));
unsigned char *normalize_prefix(unsigned char *restrict ret,
                                const unsigned char *restrict prefix,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                                unsigned char plen);
#else // CEFBABELD
                                uint16_t plen);
#endif //----- REPLACE -----
const char *format_address(const unsigned char *address);
#ifdef BABELD_CODE //+++++ REPLACE +++++
const char *format_prefix(const unsigned char *prefix, unsigned char plen);
#else // CEFBABELD
const char *format_prefix(const unsigned char *prefix, uint16_t plen);
#endif //----- REPLACE -----

#ifndef BABELD_CODE //+++++ ADD +++++
const char *format_cefore_prefix(const unsigned char *address, uint16_t plen);
#endif //----- ADD -----
const char *format_eui64(const unsigned char *eui);
const char *format_thousands(unsigned int value);
int parse_address(const char *address, unsigned char *addr_r, int *af_r);
#ifdef BABELD_CODE //+++++ REPLACE +++++
int parse_net(const char *net, unsigned char *prefix_r, unsigned char *plen_r,
#else // CEFBABELD
int parse_net(const char *net, unsigned char *prefix_r, uint16_t *plen_r,
#endif //----- REPLACE -----
              int *af_r);
int parse_eui64(const char *eui, unsigned char *eui_r);
int wait_for_fd(int direction, int fd, int msecs);
int martian_prefix(const unsigned char *prefix, int plen) ATTRIBUTE ((pure));
int linklocal(const unsigned char *address) ATTRIBUTE ((pure));
int v4mapped(const unsigned char *address) ATTRIBUTE ((pure));
void v4tov6(unsigned char *dst, const unsigned char *src);
int daemonise(void);
int set_src_prefix(unsigned char *src_addr, unsigned char *src_plen);

static inline int
is_default(const unsigned char *prefix, int plen)
{
    return plen == 0 || (plen == 96 && v4mapped(prefix));
}

enum prefix_status {
    PST_EQUALS = 0,
    PST_DISJOINT,
    PST_MORE_SPECIFIC,
    PST_LESS_SPECIFIC
};
enum prefix_status
#ifdef BABELD_CODE //+++++ REPLACE +++++
prefix_cmp(const unsigned char *p1, unsigned char plen1,
           const unsigned char *p2, unsigned char plen2);
#else // CEFBABELD
prefix_cmp(const unsigned char *p1, uint16_t plen1,
           const unsigned char *p2, uint16_t plen2);
#endif //----- REPLACE -----

/* If debugging is disabled, we want to avoid calling format_address
   for every omitted debugging message.  So debug is a macro.  But
   vararg macros are not portable. */
#if defined NO_DEBUG

#if defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L
#define debugf(...) do {} while(0)
#define kdebugf(...) do {} while(0)
#elif defined __GNUC__
#define debugf(_args...) do {} while(0)
#define kdebugf(_args...) do {} while(0)
#else
static inline void debugf(const char *format, ...) { return; }
static inline void kdebugf(const char *format, ...) { return; }
#endif

#else

#if defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L
#define debugf(...) \
    do { \
        if(UNLIKELY(debug >= 2)) do_debugf(2, __VA_ARGS__);     \
    } while(0)
#define kdebugf(...) \
    do { \
        if(UNLIKELY(debug >= 3)) do_debugf(3, __VA_ARGS__);     \
    } while(0)
#elif defined __GNUC__
#define debugf(_args...) \
    do { \
        if(UNLIKELY(debug >= 2)) do_debugf(2, _args);   \
    } while(0)
#define kdebugf(_args...) \
    do { \
        if(UNLIKELY(debug >= 3)) do_debugf(3, _args);   \
    } while(0)
#else
static inline void debugf(const char *format, ...) { return; }
static inline void kdebugf(const char *format, ...) { return; }
#endif

#endif

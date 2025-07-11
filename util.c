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
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef BABELD_CODE //+++++ ADD +++++
#include <ctype.h>
#endif //----- ADD -----

#include "babeld.h"
#include "util.h"

int
roughly(int value)
{
    if(value < 0)
        return -roughly(-value);
    else if(value <= 1)
        return value;
    else
        return value * 3 / 4 + random() % (value / 2);
}

void
timeval_minus(struct timeval *d,
              const struct timeval *s1, const struct timeval *s2)
{
    if(s1->tv_usec >= s2->tv_usec) {
        d->tv_usec = s1->tv_usec - s2->tv_usec;
        d->tv_sec = s1->tv_sec - s2->tv_sec;
    } else {
        d->tv_usec = s1->tv_usec + 1000000 - s2->tv_usec;
        d->tv_sec = s1->tv_sec - s2->tv_sec - 1;
    }
}

unsigned
timeval_minus_msec(const struct timeval *s1, const struct timeval *s2)
{
    if(s1->tv_sec < s2->tv_sec)
        return 0;

    /* Avoid overflow. */
    if(s1->tv_sec - s2->tv_sec > 2000000)
        return 2000000000;

    if(s1->tv_sec > s2->tv_sec)
        return
            (unsigned)((unsigned)(s1->tv_sec - s2->tv_sec) * 1000 +
                       ((int)s1->tv_usec - s2->tv_usec) / 1000);

    if(s1->tv_usec <= s2->tv_usec)
        return 0;

    return (unsigned)(s1->tv_usec - s2->tv_usec) / 1000u;
}

void
timeval_add_msec(struct timeval *d, const struct timeval *s, int msecs)
{
    int usecs;
    d->tv_sec = s->tv_sec + msecs / 1000;
    usecs = s->tv_usec + (msecs % 1000) * 1000;
    if(usecs < 1000000) {
        d->tv_usec = usecs;
    } else {
        d->tv_usec = usecs - 1000000;
        d->tv_sec++;
    }
}

int
timeval_compare(const struct timeval *s1, const struct timeval *s2)
{
    if(s1->tv_sec < s2->tv_sec)
        return -1;
    else if(s1->tv_sec > s2->tv_sec)
        return 1;
    else if(s1->tv_usec < s2->tv_usec)
        return -1;
    else if(s1->tv_usec > s2->tv_usec)
        return 1;
    else
        return 0;
}

/* {0, 0} represents infinity */
void
timeval_min(struct timeval *d, const struct timeval *s)
{
    if(s->tv_sec == 0)
        return;

    if(d->tv_sec == 0 || timeval_compare(d, s) > 0) {
        *d = *s;
    }
}

void
timeval_min_sec(struct timeval *d, time_t secs)
{
    if(d->tv_sec == 0 || d->tv_sec > secs) {
        d->tv_sec = secs;
        d->tv_usec = random() % 1000000;
    }
}

/* There's no good name for a positive int in C, call it nat. */
int
parse_nat(const char *string)
{
    long l;
    char *end;

    l = strtol(string, &end, 0);

    while(*end == ' ' || *end == '\t')
        end++;
    if(*end != '\0')
        return -1;

    if(l < 0 || l > INT_MAX)
        return -1;

    return (int)l;
}

/* Given a fixed-point string such as "42.1337", returns 1000 times
   the value of the string, here 42133. */
int
parse_thousands(const char *string)
{
    unsigned int in, fl;
    int i, j;

    in = fl = 0;
    i = 0;
    while(string[i] == ' ' || string[i] == '\t')
        i++;
    while(string[i] >= '0' && string[i] <= '9') {
        in = in * 10 + string[i] - '0';
        i++;
    }
    if(string[i] == '.') {
        i++;
        j = 0;
        while(string[i] >= '0' && string[i] <= '9') {
            fl = fl * 10 + string[i] - '0';
            i++;
            j++;
        }

        while(j > 3) {
            fl /= 10;
            j--;
        }
        while(j < 3) {
            fl *= 10;
            j++;
        }
    }

    while(string[i] == ' ' || string[i] == '\t')
        i++;

    if(string[i] == '\0')
        return in * 1000 + fl;

    return -1;
}

void
do_debugf(int level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if(debug >= level) {
        vfprintf(stderr, format, args);
        fflush(stderr);
    }
    va_end(args);
}

int
in_prefix(const unsigned char *restrict address,
#ifdef BABELD_CODE //+++++ REPLACE +++++
          const unsigned char *restrict prefix, unsigned char plen)
#else // CEFBABELD
          const unsigned char *restrict prefix, uint16_t plen)
#endif //----- REPLACE -----
{
    unsigned char m;

    if(plen > 128)
        plen = 128;

    if(memcmp(address, prefix, plen / 8) != 0)
        return 0;

    if(plen % 8 == 0)
        return 1;

    m = 0xFF << (8 - (plen % 8));

    return ((address[plen / 8] & m) == (prefix[plen / 8] & m));
}

unsigned char *
normalize_prefix(unsigned char *restrict ret,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                 const unsigned char *restrict prefix, unsigned char plen)
#else // CEFBABELD
                 const unsigned char *restrict prefix, uint16_t plen)
#endif //----- REPLACE -----
{
    if(plen >= 128) {
        memcpy(ret, prefix, 16);
        return ret;
    }

    memset(ret, 0, 16);
    memcpy(ret, prefix, plen / 8);
    if(plen % 8 != 0)
        ret[plen / 8] =
            (prefix[plen / 8] & ((0xFF << (8 - (plen % 8))) & 0xFF));
    return ret;
}

const unsigned char v4prefix[16] =
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0 };

static const unsigned char llprefix[16] =
    {0xFE, 0x80};

const char *
format_address(const unsigned char *address)
{
    static char buf[4][INET6_ADDRSTRLEN];
    static int i = 0;
    i = (i + 1) % 4;
    if(v4mapped(address))
        inet_ntop(AF_INET, address + 12, buf[i], INET6_ADDRSTRLEN);
    else
        inet_ntop(AF_INET6, address, buf[i], INET6_ADDRSTRLEN);
    return buf[i];
}

const char *
#ifdef BABELD_CODE //+++++ REPLACE +++++
format_prefix(const unsigned char *prefix, unsigned char plen)
#else // CEFBABELD
format_prefix(const unsigned char *prefix, uint16_t plen)
#endif //----- REPLACE -----
{
    static char buf[4][INET6_ADDRSTRLEN + 4];
    static int i = 0;
    int n;
    i = (i + 1) % 4;
    if(plen >= 96 && v4mapped(prefix)) {
        inet_ntop(AF_INET, prefix + 12, buf[i], INET6_ADDRSTRLEN);
        n = strlen(buf[i]);
        snprintf(buf[i] + n, INET6_ADDRSTRLEN + 4 - n, "/%d", plen - 96);
    } else {
        inet_ntop(AF_INET6, prefix, buf[i], INET6_ADDRSTRLEN);
        n = strlen(buf[i]);
        snprintf(buf[i] + n, INET6_ADDRSTRLEN + 4 - n, "/%d", plen);
    }
    return buf[i];
}

#ifndef BABELD_CODE //+++++ ADD +++++
const char *
format_cefore_prefix(const unsigned char *prefix, uint16_t plen)
{
    struct cefore_tlv_hdr {
        uint16_t        type;
        uint16_t        length;
    } __attribute__((__packed__));
    
    static char buf[4][65535];
    static int i = 0;
    
    struct cefore_tlv_hdr* hdp;

    unsigned char name[65535];
    char uri[65535];
    int j;
    int x = 0;
    int seg_len, uri_len, name_len;
    uint16_t sub_type;
    uint16_t app_num;
    char work[16];

    i = (i + 1) % 4;

    uri_len = 0;
    memset( uri, 0x00, 65535 );
    memset( &buf[i][0], 0x00, 65535 );
    name_len = (int)plen;
    memcpy( name, prefix, name_len );
    uri[uri_len] = '/';
    uri_len++;

    while (x < name_len) {
        int hname = 0;
        hdp = (struct cefore_tlv_hdr*) &prefix[x];
        seg_len = ntohs (hdp->length);
        x += 4;

        /* Check if it contains non-print character */
        for (j = 0 ; j < seg_len ; j++) {
            if(!(isprint(name[x+j]))) { /* HEX Name */
                hname = 1;
                break;
            }
        }
        if(hname == 0){

            char char_num[8];
            int  num_len;
            sub_type = ntohs (hdp->type);
            if ((sub_type >= 0x1000) &&
                (sub_type <= 0x1FFF)) {
                memset( char_num, 0x00, 8 );
                /* T_APP */
                app_num = sub_type - 0x1000;
                sprintf( char_num, "%d", app_num );
                num_len = strlen( char_num );
                memcpy( &uri[uri_len], "APP:", 4 );
                uri_len += 4;
                memcpy( &uri[uri_len], char_num, num_len );
                uri_len += num_len;
                uri[uri_len] = '=';
                uri_len++;
            } else if ( sub_type == 0x0001 ) {
                /* T_NAMESEGMENT */
            } else {
                /* HEX Type */
                unsigned char   sub_wk[8];
                memset( sub_wk, 0x00, 8 );
                memcpy( sub_wk, &hdp->type, 2 );
                memset( char_num, 0x00, 8 );
                uri[uri_len++] = '0';
                uri[uri_len++] = 'x';
                sprintf( char_num, "%02x", sub_wk[0] );
                memcpy( &uri[uri_len], char_num, 2 );
                uri_len += 2;
                sprintf( char_num, "%02x", sub_wk[1] );
                memcpy( &uri[uri_len], char_num, 2 );
                uri_len += 2;
                uri[uri_len] = '=';
                uri_len++;
            }

            for (j = 0 ; j < seg_len ; j++) {
                if(((name[x + j] >= 0x30) && (name[x + j] <= 0x39)) ||      /* 0~9 */
                    ((name[x + j] >= 0x41) && (name[x + j] <= 0x5a)) ||     /* A~Z */
                    ((name[x + j] >= 0x61) && (name[x + j] <= 0x7a))) {     /* a~z */
                    
                    uri[uri_len] = name[x + j];
                    uri_len++;
                } else if ((name[x + j] == 0x2d) ||                     /* - */
                        (name[x + j] == 0x2e) ||                        /* . */
                        (name[x + j] == 0x2f) ||                        /* / */
                        (name[x + j] == 0x5f) ||                        /* _ */
                        (name[x + j] == 0x7e)) {                        /* ~ */
                    uri[uri_len] = name[x + j];
                    uri_len++;
                } else {
                    sprintf (work, "%02x", name[x + j]);
                    strcpy (&uri[uri_len], work);
                    uri_len += strlen (work);
                }
            }
        } else {
            uri[uri_len++] = '0';
            uri[uri_len++] = 'x';
            for (j = 0 ; j < seg_len ; j++) {
                sprintf (work, "%02x", name[x + j]);
                strcpy (&uri[uri_len], work);
                uri_len += strlen (work);
            }
        }
        uri[uri_len] = '/';
        uri_len++;

        x += seg_len;
    }   //end while

    uri[uri_len] = 0x00;
    /* delete last '/' */
    if(uri[uri_len-1] == '/'){
        uri[uri_len-1] = 0x00;
        uri_len--;
    }

    memcpy( &buf[i][0], uri, uri_len );

    return buf[i];
}
#endif //----- ADD -----
const char *
format_eui64(const unsigned char *eui)
{
    static char buf[4][28];
    static int i = 0;
    i = (i + 1) % 4;
    snprintf(buf[i], 28, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
             eui[0], eui[1], eui[2], eui[3],
             eui[4], eui[5], eui[6], eui[7]);
    return buf[i];
}

const char *
format_thousands(unsigned int value)
{
    static char buf[4][15];
    static int i = 0;
    i = (i + 1) % 4;
    snprintf(buf[i], 15, "%u.%.3u", value / 1000, value % 1000);
    return buf[i];
}

int
parse_address(const char *address, unsigned char *addr_r, int *af_r)
{
    struct in_addr ina;
    struct in6_addr ina6;
    int rc;

    rc = inet_pton(AF_INET, address, &ina);
    if(rc > 0) {
        memcpy(addr_r, v4prefix, 12);
        memcpy(addr_r + 12, &ina, 4);
        if(af_r) *af_r = AF_INET;
        return 0;
    }

    rc = inet_pton(AF_INET6, address, &ina6);
    if(rc > 0) {
        memcpy(addr_r, &ina6, 16);
        if(af_r) *af_r = AF_INET6;
        return 0;
    }

    return -1;
}

int
#ifdef BABELD_CODE //+++++ REPLACE +++++
parse_net(const char *net, unsigned char *prefix_r, unsigned char *plen_r,
#else // CEFBABELD
parse_net(const char *net, unsigned char *prefix_r, uint16_t *plen_r,
#endif //----- REPLACE -----
          int *af_r)
{
    char buf[INET6_ADDRSTRLEN];
    char *slash, *end;
    unsigned char prefix[16];
    long plen;
    int af;
    struct in_addr ina;
    struct in6_addr ina6;
    int rc;

    if(strcmp(net, "default") == 0) {
        memset(prefix, 0, 16);
        plen = 0;
        af = AF_INET6;
    } else {
        slash = strchr(net, '/');
        if(slash == NULL) {
            rc = parse_address(net, prefix, &af);
            if(rc < 0)
                return rc;
            plen = 128;
        } else {
            if(slash - net >= INET6_ADDRSTRLEN)
                return -1;
            memcpy(buf, net, slash - net);
            buf[slash - net] = '\0';
            rc = inet_pton(AF_INET, buf, &ina);
            if(rc > 0) {
                memcpy(prefix, v4prefix, 12);
                memcpy(prefix + 12, &ina, 4);
                plen = strtol(slash + 1, &end, 0);
                if(*end != '\0' || plen < 0 || plen > 32)
                    return -1;
                plen += 96;
                af = AF_INET;
            } else {
                rc = inet_pton(AF_INET6, buf, &ina6);
                if(rc > 0) {
                    memcpy(prefix, &ina6, 16);
                    plen = strtol(slash + 1, &end, 0);
                    if(*end != '\0' || plen < 0 || plen > 128)
                        return -1;
                    af = AF_INET6;
                } else {
                    return -1;
                }
            }
        }
    }
    normalize_prefix(prefix_r, prefix, plen);
    *plen_r = plen;
    if(af_r) *af_r = af;
    return 0;
}

int
parse_eui64(const char *eui, unsigned char *eui_r)
{
    int n;
    n = sscanf(eui, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &eui_r[0], &eui_r[1], &eui_r[2], &eui_r[3],
               &eui_r[4], &eui_r[5], &eui_r[6], &eui_r[7]);
    if(n == 8)
        return 0;

    n = sscanf(eui, "%02hhx-%02hhx-%02hhx-%02hhx-%02hhx-%02hhx-%02hhx-%02hhx",
               &eui_r[0], &eui_r[1], &eui_r[2], &eui_r[3],
               &eui_r[4], &eui_r[5], &eui_r[6], &eui_r[7]);
    if(n == 8)
        return 0;

    n = sscanf(eui, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &eui_r[0], &eui_r[1], &eui_r[2],
               &eui_r[5], &eui_r[6], &eui_r[7]);
    if(n == 6) {
        eui_r[3] = 0xFF;
        eui_r[4] = 0xFE;
        return 0;
    }
    return -1;
}

int
wait_for_fd(int direction, int fd, int msecs)
{
    fd_set fds;
    int rc;
    struct timeval tv;

    tv.tv_sec = msecs / 1000;
    tv.tv_usec = (msecs % 1000) * 1000;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    if(direction)
        rc = select(fd + 1, NULL, &fds, NULL, &tv);
    else
        rc = select(fd + 1, &fds, NULL, NULL, &tv);

    return rc;
}

int
martian_prefix(const unsigned char *prefix, int plen)
{
    static const unsigned char ones[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    return
        (plen >= 8 && prefix[0] == 0xFF) ||
        (plen >= 10 && prefix[0] == 0xFE && (prefix[1] & 0xC0) == 0x80) ||
        (plen >= 128 && memcmp(prefix, zeroes, 15) == 0 &&
         (prefix[15] == 0 || prefix[15] == 1)) ||
        (plen >= 96 && v4mapped(prefix) &&
         ((plen >= 104 && (prefix[12] == 127 || prefix[12] == 0)) ||
          (plen >= 100 && (prefix[12] & 0xF0) == 0xE0) ||
          (plen >= 128 && memcmp(prefix + 12, ones, 4) == 0)));
}

int
linklocal(const unsigned char *address)
{
    return memcmp(address, llprefix, 8) == 0;
}

int
v4mapped(const unsigned char *address)
{
    return memcmp(address, v4prefix, 12) == 0;
}

void
v4tov6(unsigned char *dst, const unsigned char *src)
{
    memcpy(dst, v4prefix, 12);
    memcpy(dst + 12, src, 4);
}

int
daemonise()
{
    int rc;

    fflush(stdout);
    fflush(stderr);

    rc = fork();
    if(rc < 0)
        return -1;

    if(rc > 0)
        exit(0);

    rc = setsid();
    if(rc < 0)
        return -1;

    return 1;
}

enum prefix_status
#ifdef BABELD_CODE //+++++ REPLACE +++++
prefix_cmp(const unsigned char *p1, unsigned char plen1,
           const unsigned char *p2, unsigned char plen2)
#else // CEFBABELD
prefix_cmp(const unsigned char *p1, uint16_t plen1,
           const unsigned char *p2, uint16_t plen2)
#endif //----- REPLACE -----
{
    int plen = MIN(plen1, plen2);

    if(memcmp(p1, p2, plen / 8) != 0)
        return PST_DISJOINT;

    if(plen % 8 != 0) {
        int i = plen / 8 + 1;
        unsigned char mask = (0xFF << (plen % 8)) & 0xFF;
        if((p1[i] & mask) != (p2[i] & mask))
            return PST_DISJOINT;
    }

    if(plen1 < plen2)
        return PST_LESS_SPECIFIC;
    else if(plen1 > plen2)
        return PST_MORE_SPECIFIC;
    else
        return PST_EQUALS;
}

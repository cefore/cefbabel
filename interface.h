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

struct buffered_update {
    unsigned char id[8];
#ifdef BABELD_CODE //+++++ REPLACE +++++
    unsigned char prefix[16];
#else // CEFBABELD
    unsigned char prefix[NAME_PREFIX_LEN];
#endif //----- REPLACE -----
    unsigned char src_prefix[16];
#ifdef BABELD_CODE //+++++ REPLACE +++++
    unsigned char plen;
#else // CEFBABELD
    uint16_t plen;
#endif //----- REPLACE -----
    unsigned char src_plen;
    unsigned char pad[2];
};

#define IF_TYPE_DEFAULT 0
#define IF_TYPE_WIRED 1
#define IF_TYPE_WIRELESS 2
#define IF_TYPE_TUNNEL 3

/* If you modify this structure, also modify the merge_ifconf function. */

struct interface_conf {
    char *ifname;
    unsigned hello_interval;
    unsigned update_interval;
    unsigned short cost;
    char type;
    char split_horizon;
    char lq;
    char faraway;
    char unicast;
    int channel;
    int enable_timestamps;
    int rfc6126;
    unsigned int rtt_decay;
    unsigned int rtt_min;
    unsigned int rtt_max;
    unsigned int max_rtt_penalty;
    struct interface_conf *next;
};

#define CONFIG_DEFAULT 0
#define CONFIG_NO 1
#define CONFIG_YES 2

/* Interface is up. */
#define IF_UP (1 << 0)
/* Interface known to be wireless, unknown otherwise. */
#define IF_WIRELESS (1<<1)
/* Apply split horizon. */
#define IF_SPLIT_HORIZON (1 << 2)
/* Perform link-quality estimation. */
#define IF_LQ (1 << 3)
/* Nodes on the far end don't interfere with nodes on the near end. */
#define IF_FARAWAY (1 << 4)
/* Send most TLVs over unicast. */
#define IF_UNICAST (1 << 5)
/* Send timestamps in Hello and IHU. */
#define IF_TIMESTAMPS (1 << 6)
/* Remain compatible with RFC 6126. */
#define IF_RFC6126 (1 << 7)
/* Use Babel over DTLS on this interface. */
#define IF_DTLS (1 << 9)

/* Only INTERFERING can appear on the wire. */
#define IF_CHANNEL_UNKNOWN 0
#define IF_CHANNEL_INTERFERING 255
#define IF_CHANNEL_NONINTERFERING -2

struct buffered {
    struct sockaddr_in6 sin6;
    unsigned char *buf;
    int len;
    int size;
    int flush_interval;
    struct timeval timeout;
    char have_id;
    char have_nh;
    char have_prefix;
    unsigned char id[8];
    unsigned char nh[4];
    unsigned char prefix[16];
    /* Relative position of the Hello message in the send buffer, or
       (-1) if there is none. */
    int hello;
};

struct interface {
    struct interface *next;
    struct interface_conf *conf;
    unsigned int ifindex;
    unsigned short flags;
    unsigned short cost;
    int channel;
    struct timeval hello_timeout;
    struct timeval update_timeout;
    struct timeval update_flush_timeout;
    char name[IF_NAMESIZE];
    unsigned char *ipv4;
    int numll;
    unsigned char (*ll)[16];
#ifndef BABELD_CODE //+++++ ADD +++++
    unsigned char *specified_ipv4;
    unsigned char *specified_ipv6;
#endif //----- ADD -----
    struct buffered buf;
    struct buffered_update *buffered_updates;
    int num_buffered_updates;
    int update_bufsize;
    time_t last_update_time;
    unsigned short hello_seqno;
    unsigned hello_interval;
    unsigned update_interval;
    /* A higher value means we forget old RTT samples faster. Must be
       between 1 and 256, inclusive. */
    unsigned int rtt_decay;
    /* Parameters for computing the cost associated to RTT. */
    unsigned int rtt_min;
    unsigned int rtt_max;
    unsigned int max_rtt_penalty;
};

#define IF_CONF(_ifp, _field) \
    ((_ifp)->conf ? (_ifp)->conf->_field : 0)

extern struct interface *interfaces;

#define FOR_ALL_INTERFACES(_ifp) for(_ifp = interfaces; _ifp; _ifp = _ifp->next)

static inline int
if_up(struct interface *ifp)
{
    return !!(ifp->flags & IF_UP);
}

struct interface *add_interface(char *ifname, struct interface_conf *if_conf);
int flush_interface(char *ifname);
unsigned jitter(struct buffered *buf, int urgent);
unsigned update_jitter(struct interface *ifp, int urgent);
void set_timeout(struct timeval *timeout, int msecs);
int interface_updown(struct interface *ifp, int up);
int interface_ll_address(struct interface *ifp, const unsigned char *address);
void check_interfaces(void);

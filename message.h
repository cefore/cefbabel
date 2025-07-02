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

#define MAX_BUFFERED_UPDATES 200

#define MESSAGE_PAD1 0
#define MESSAGE_PADN 1
#define MESSAGE_ACK_REQ 2
#define MESSAGE_ACK 3
#define MESSAGE_HELLO 4
#define MESSAGE_IHU 5
#define MESSAGE_ROUTER_ID 6
#define MESSAGE_NH 7
#define MESSAGE_UPDATE 8
#define MESSAGE_REQUEST 9
#define MESSAGE_MH_REQUEST 10

/* Protocol extension through sub-TLVs. */
#define SUBTLV_PAD1 0
#define SUBTLV_PADN 1
#define SUBTLV_DIVERSITY 2       /* Also known as babelz. */
#define SUBTLV_TIMESTAMP 3       /* Used to compute RTT. */
#define SUBTLV_SOURCE_PREFIX 128 /* Source-specific routing. */

extern unsigned short myseqno;
extern struct timeval seqno_time;

extern int broadcast_ihu;
extern int split_horizon;

extern unsigned char packet_header[4];

void parse_packet(const unsigned char *from, struct interface *ifp,
                  const unsigned char *packet, int packetlen);
void flushbuf(struct buffered *buf, struct interface *ifp);
void flushupdates(struct interface *ifp);
void send_ack(struct neighbour *neigh, unsigned short nonce,
              unsigned short interval);
void send_multicast_hello(struct interface *ifp, unsigned interval, int force);
void send_unicast_hello(struct neighbour *neigh, unsigned interval, int force);
void send_hello(struct interface *ifp);
void flush_unicast(int dofree);
void send_update(struct interface *ifp, int urgent,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                 const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                 const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                 const unsigned char *src_prefix, unsigned char src_plen);
void send_update_resend(struct interface *ifp,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                        const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                        const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                        const unsigned char *src_prefix,
                        unsigned char src_plen);
void send_wildcard_retraction(struct interface *ifp);
void update_myseqno(void);
void send_self_update(struct interface *ifp);
void send_ihu(struct neighbour *neigh, struct interface *ifp);
void send_marginal_ihu(struct interface *ifp);
void send_multicast_request(struct interface *ifp,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                  const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                  const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                  const unsigned char *src_prefix, unsigned char src_plen);
void send_unicast_request(struct neighbour *neigh,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                          const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                          const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                          const unsigned char *src_prefix,
                          unsigned char src_plen);
void
send_multicast_multihop_request(struct interface *ifp,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                                const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
                                const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                                const unsigned char *src_prefix,
                                unsigned char src_plen,
                                unsigned short seqno, const unsigned char *id,
                                unsigned short hop_count);
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
                              unsigned short hop_count);
#ifdef BABELD_CODE //+++++ REPLACE +++++
void send_request_resend(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
void send_request_resend(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                         const unsigned char *src_prefix,
                         unsigned char src_plen,
                         unsigned short seqno, unsigned char *id);
void handle_request(struct neighbour *neigh, const unsigned char *prefix,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                    unsigned char plen,
#else // CEFBABELD
                    uint16_t plen,
#endif //----- REPLACE -----
                    const unsigned char *src_prefix, unsigned char src_plen,
                    unsigned char hop_count,
                    unsigned short seqno, const unsigned char *id);

#ifndef BABELD_CODE //+++++ ADD for MP +++++
void really_send_update_mp(struct interface *ifp, const unsigned char *id,
                   const unsigned char *prefix, uint16_t plen,
                   const unsigned char *src_prefix, unsigned char src_plen,
                   unsigned short seqno, unsigned short metric, unsigned short port);
#endif //----- ADD for MP -----

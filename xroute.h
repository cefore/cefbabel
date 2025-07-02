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

struct xroute {
#ifdef BABELD_CODE //+++++ REPLACE +++++
    unsigned char prefix[16];
    unsigned char plen;
#else // CEFBABELD
    unsigned char prefix[NAME_PREFIX_LEN];
    uint16_t plen;
#endif //----- REPLACE -----
    unsigned char src_prefix[16];
    unsigned char src_plen;
    unsigned short metric;
    unsigned int ifindex;
    int proto;
};

struct xroute_stream;

#ifdef BABELD_CODE //+++++ REPLACE +++++
struct xroute *find_xroute(const unsigned char *prefix, unsigned char plen,
#else // CEFBABELD
struct xroute *find_xroute(const unsigned char *prefix, uint16_t plen,
#endif //----- REPLACE -----
                const unsigned char *src_prefix, unsigned char src_plen);
#ifdef BABELD_CODE //+++++ REPLACE +++++
int add_xroute(unsigned char prefix[16], unsigned char plen,
#else // CEFBABELD
int add_xroute(unsigned char prefix[NAME_PREFIX_LEN], uint16_t plen,
#endif //----- REPLACE -----
               unsigned char src_prefix[16], unsigned char src_plen,
               unsigned short metric, unsigned int ifindex, int proto);
void flush_xroute(struct xroute *xroute);
int xroutes_estimate(void);
struct xroute_stream *xroute_stream();
struct xroute *xroute_stream_next(struct xroute_stream *stream);
void xroute_stream_done(struct xroute_stream *stream);
int kernel_addresses(int ifindex, int ll,
                     struct kernel_route *routes, int maxroutes);
int check_xroutes(int send_updates);
#ifndef BABELD_CODE //+++++ ADD for STAT +++++
/**************************************************************************************************/    
/***** STAT Common Functions                                                                  *****/
/**************************************************************************************************/    
int exist_xroute();
#endif //----- ADD for STAT -----
#ifndef BABELD_CODE //+++++ ADD for MP +++++
/**************************************************************************************************/    
/***** Multi path Common Functions                                                            *****/
/**************************************************************************************************/    
struct xroute *find_xroute_mp(const unsigned char *prefix, uint16_t plen);
#endif  //----- ADD for MP -----

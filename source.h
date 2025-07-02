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

#define SOURCE_GC_TIME 200

struct source {
    unsigned char id[8];
#ifdef BABELD_CODE //+++++ REPLACE +++++
    unsigned char prefix[16];
    unsigned char plen;
#else // CEFBABELD
    unsigned char prefix[NAME_PREFIX_LEN];
    uint16_t plen;
#endif //----- REPLACE -----
    unsigned char src_prefix[16];
    unsigned char src_plen;
    unsigned short seqno;
    unsigned short metric;
    unsigned short route_count;
    time_t time;
};

struct source *find_source(const unsigned char *id,
                           const unsigned char *prefix,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                           unsigned char plen,
#else // CEFBABELD
                           uint16_t plen,
#endif //----- REPLACE -----
                           const unsigned char *src_prefix,
                           unsigned char src_plen,
                           int create, unsigned short seqno);
struct source *retain_source(struct source *src);
void release_source(struct source *src);
void update_source(struct source *src,
                   unsigned short seqno, unsigned short metric);
void expire_sources(void);
void check_sources_released(void);
#ifndef BABELD_CODE //+++++ ADD for MP +++++
int exist_source_mp (
                 const unsigned char *prefix, uint16_t plen,
                 const unsigned char *src_prefix, unsigned char src_plen);
void delete_source_mp(struct source * delsrc);
struct source*
get_src_rcd_mp(const unsigned char *prefix, uint16_t plen,
              const unsigned char *src_prefix, unsigned char src_plen);
#endif //----- ADD for MP -----
#ifndef BABELD_CODE //+++++ ADD for MPSS +++++
unsigned char * find_other_source_mpss (const unsigned char *id,
                 const unsigned char *prefix, uint16_t plen,
                 const unsigned char *src_prefix, unsigned char src_plen);
void dump_source(FILE *out);
#endif //----- ADD for MPSS -----
#ifndef BABELD_CODE //+++++ ADD for MPMS +++++
void update_source_mpms(const unsigned char *prefix, uint16_t plen,
           const unsigned char *src_prefix, unsigned char src_plen);
#endif //----- ADD for MPMS -----


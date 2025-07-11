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

/* Values returned by parse_config_from_string. */

#define CONFIG_ACTION_DONE 0
#define CONFIG_ACTION_QUIT 1
#define CONFIG_ACTION_DUMP 2
#define CONFIG_ACTION_MONITOR 3
#define CONFIG_ACTION_UNMONITOR 4
#define CONFIG_ACTION_NO 5

struct filter_result {
    unsigned int add_metric; /* allow = 0, deny = INF, metric = <0..INF> */
    unsigned char *src_prefix;
    unsigned char src_plen;
    unsigned int table;
    unsigned char *pref_src;
};

struct filter {
    int af;
    char *ifname;
    unsigned int ifindex;
    unsigned char *id;
    unsigned char *prefix;
#ifdef BABELD_CODE //+++++ REPLACE +++++
    unsigned char plen;
    unsigned char plen_ge, plen_le;
#else // CEFBABELD
    uint16_t plen;
    uint16_t plen_ge, plen_le;
#endif //----- REPLACE -----
    unsigned char *src_prefix;
    unsigned char src_plen;
    unsigned char src_plen_ge, src_plen_le;
    unsigned char *neigh;
    int proto;                  /* May be negative */
    struct filter_result action;
    struct filter *next;
};

extern struct interface_conf *default_interface_conf;

void flush_ifconf(struct interface_conf *if_conf);

int parse_config_from_file(const char *filename, int *line_return);
int parse_config_from_string(char *string, int n, const char **message_return);
void renumber_filters(void);

int input_filter(const unsigned char *id,
                 const unsigned char *prefix, unsigned short plen,
                 const unsigned char *src_prefix, unsigned short src_plen,
                 const unsigned char *neigh, unsigned int ifindex);
int output_filter(const unsigned char *id,
                  const unsigned char *prefix, unsigned short plen,
                  const unsigned char *src_prefix, unsigned short src_plen,
                  unsigned int ifindex);
int redistribute_filter(const unsigned char *prefix, unsigned short plen,
                    const unsigned char *src_prefix, unsigned short src_plen,
                    unsigned int ifindex, int proto,
                    struct filter_result *result);
int install_filter(const unsigned char *prefix, unsigned short plen,
                   const unsigned char *src_prefix, unsigned short src_plen,
                   unsigned int ifindex, struct filter_result *result);
int finalise_config(void);

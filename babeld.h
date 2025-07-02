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

#define INFINITY ((unsigned short)(~0))

#ifndef RTPROT_BABEL
#define RTPROT_BABEL 42
#endif

#define RTPROT_BABEL_LOCAL -2

#undef MAX
#undef MIN

#define MAX(x,y) ((x)<=(y)?(y):(x))
#define MIN(x,y) ((x)<=(y)?(x):(y))

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/* nothing */
#elif defined(__GNUC__)
#define inline __inline
#if  (__GNUC__ >= 3)
#define restrict __restrict
#else
#define restrict /**/
#endif
#else
#define inline /**/
#define restrict /**/
#endif

#if defined(__GNUC__) && (__GNUC__ >= 3)
#define ATTRIBUTE(x) __attribute__ (x)
#define LIKELY(_x) __builtin_expect(!!(_x), 1)
#define UNLIKELY(_x) __builtin_expect(!!(_x), 0)
#else
#define ATTRIBUTE(x) /**/
#define LIKELY(_x) !!(_x)
#define UNLIKELY(_x) !!(_x)
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 3)
#define COLD __attribute__ ((cold))
#else
#define COLD /**/
#endif

#ifndef IF_NAMESIZE
#include <sys/socket.h>
#include <net/if.h>
#endif

#ifdef HAVE_VALGRIND
#include <valgrind/memcheck.h>
#else
#ifndef VALGRIND_MAKE_MEM_UNDEFINED
#define VALGRIND_MAKE_MEM_UNDEFINED(a, b) do {} while(0)
#endif
#ifndef VALGRIND_CHECK_MEM_IS_DEFINED
#define VALGRIND_CHECK_MEM_IS_DEFINED(a, b) do {} while(0)
#endif
#endif

#ifndef BABELD_CODE //+++++ ADD +++++
#define NAME_PREFIX_LEN     1024
#define ROUTE_CTRL_TYPE_S   0   /* "Single path"                                    */
#define ROUTE_CTRL_TYPE_MS  1   /* "Multipath Routing with Single Sources"(MPSS)    */
#define ROUTE_CTRL_TYPE_MM  2   /* "Multipath Routing with Multiple Sources"(MPMS)  */
#endif //----- ADD -----

extern struct timeval now;
extern int debug;
extern time_t reboot_time;
extern int default_wireless_hello_interval, default_wired_hello_interval;
extern int resend_delay;
extern int random_id;
extern int skip_kernel_setup;
extern int do_daemonise;
extern const char *logfile, *pidfile, *state_file;
extern int link_detect;
extern int all_wireless;
extern int has_ipv6_subtrees;

extern unsigned char myid[8];
extern int have_id;

extern const unsigned char zeroes[16], ones[16];

extern int protocol_port, local_server_port;
extern char *local_server_path;
extern int local_server_write;
extern unsigned char protocol_group[16];
extern int protocol_socket;
extern int kernel_socket;
#ifndef BABELD_CODE //+++++ ADD +++++
extern int cefore_socket;
extern int route_ctrl_type;
extern unsigned short cefore_portnum;
extern int cefstat_sent_update_num;
#endif //----- ADD -----
extern int max_request_hopcount;

void schedule_neighbours_check(int msecs, int override);
void schedule_interfaces_check(int msecs, int override);
int resize_receive_buffer(int size);
int reopen_logfile(void);

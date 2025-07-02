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
 * cefore.h
 */

#ifndef __CEFORE_HEADER__
#define __CEFORE_HEADER__

#define CefC_Default_Tcp_Prot           9897        /* cefbubeld's listen port num  */
#define CefC_Cbabel_Stat_Mtu            65535
#define CefC_O_Fix_Ver                  0
#define CefC_Version                    0x01
#define CefC_O_Fix_Type                 1
#define CefC_Cbabel_Msg_Type_Status     0x10        /* Type Get Status              */
#define CefC_Cbabel_CmdMsg_HeaderLen    4
#define CefC_Cbabel_RspMsg_HeaderLen    6
#define CefC_O_Length                   2
#define CefC_S_Length                   2           /* Length field is 2 bytes      */
#define CefC_L_Length                   4           /* Length field is 4 bytes      */
#define CefC_Cbabel_Cmd_MaxLen          1024
#define CefC_Cbabel_Cmd_ConnOK          "CMD://CbabelConnOK"
#define CefC_S_Length                   2           /* Length field is 2 bytes      */

int 
cefore_init (
    int port_num, 
    const char* config_file_dir
);
int 
cefore_xroute_init (
    void
);
int 
cefore_xroute_update (
    unsigned char* msg, 
    int msg_len
);
int 
cefore_socket_create (
    void
);
void
cefire_socket_close (
    void 
);
int 
cefore_fib_add_req_send (
    const unsigned char* prefix, 
    int plen, 
    const unsigned char* nexthop, 
    unsigned short port,
    char* interface
);
int 
cefore_fib_del_req_send (
    const unsigned char* prefix, 
    int plen, 
    const unsigned char* nexthop, 
    unsigned short port,
    char* interface
);

int
cefbabeld_tcp_sock_create (
    uint16_t        port_num
);

void
cefbabel_tcp_stat_prcess ();

#endif // __CEFORE_HEADER__


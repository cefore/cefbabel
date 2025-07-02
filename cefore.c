#ifndef BABELD_CODE //+++++ ADD +++++
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
 * cefore.c
 */

#define __CEFORE_SOURCE__

/****************************************************************************************
 Include Files
 ****************************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <poll.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#ifndef BABELD_CODE //+++++ ADD +++++
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif //----- ADD -----

#include "babeld.h"
#include "util.h"
#include "net.h"
#include "kernel.h"
#include "interface.h"
#include "neighbour.h"
#include "message.h"
#include "route.h"
#include "xroute.h"
#include "cefore.h"
#ifndef BABELD_CODE //+++++ ADD +++++
#include "source.h"
#endif //----- ADD -----

/****************************************************************************************
 Macros
 ****************************************************************************************/

#define Default_PortNum             9896

#define Cmd_FibRet                  "/CTRLBABELR"
#define Cmd_FibRet_Len              strlen (Cmd_FibRet)
#define Cmd_FibAdd                  "/CTRLBABELA"
#define Cmd_FibAdd_Len              strlen (Cmd_FibAdd)
#define Cmd_FibDel                  "/CTRLBABELD"
#define Cmd_FibDel_Len              strlen (Cmd_FibDel)

/****************************************************************************************
 Structures Declaration
 ****************************************************************************************/

struct tlv_hdr {
    uint16_t    type;
    uint16_t    length;
} __attribute__((__packed__));

typedef struct _CefT_Route {
    
    unsigned char prefix[NAME_PREFIX_LEN];
    uint16_t plen;
    unsigned char src_prefix[16];
    unsigned char src_plen;
    unsigned short metric;
    unsigned int ifindex;
    
    struct _CefT_Route* next;
    
} CefT_Route;

/****************************************************************************************
 State Variables
 ****************************************************************************************/

static char cef_lsock_name[256] = {"/tmp/cbd_9896.0"};
static char cef_conf_dir[PATH_MAX] = {"/usr/local/cefore"};
static unsigned char v4prefix[16] =
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0 };

/****************************************************************************************
 Static Function Declaration
 ****************************************************************************************/

/*--------------------------------------------------------------------------------------
    Trims the string buffer read from the config file
----------------------------------------------------------------------------------------*/
static int
cefore_trim_line_string (
    const char* p1,                             /* target string for trimming           */
    char* p2,                                   /* name string after trimming           */
    char* p3                                    /* value string after trimming          */
);


/*--------------------------------------------------------------------------------------
    Send status command to cefbbabald.
----------------------------------------------------------------------------------------*/
static int                                  /* The return value is negative if an error occurs  */
cef_cbabel_send_msg (
    int fd,                                 /* socket fd                                */
    unsigned char* msg,                     /* send message                             */
    uint16_t msg_len                        /* message length                           */
);


/****************************************************************************************
 ****************************************************************************************/

#if 0
void
cef_buff_print (
    const unsigned char* buff,
    uint16_t len
) {
    int i;
    int n = 0;
    int s = 0;


    fprintf (stderr, "======================================================\n");
    fprintf (stderr, "      0  1  2  3  4  5  6  7    8  9  0  1  2  3  4  5\n");
    for (i = 0 ; i < len ; i++) {
        if (n == 0) {
            fprintf (stderr, "%3d: ", s);
            s++;
        }
        fprintf (stderr, "%02X ", buff[i]);

        if (n == 7) {
            fprintf (stderr, "  ");
        }
        n++;
        if (n > 15) {
            n = 0;
            fprintf (stderr, "\n");
        }
    }
    fprintf (stderr, "\n======================================================\n");
}
#endif

/*--------------------------------------------------------------------------------------
    Creats the local socket name 
----------------------------------------------------------------------------------------*/
int 
cefore_init (
    int port_num, 
    const char* config_file_dir
) {
    char*   wp;
    FILE*   fp;
    char    file_path[PATH_MAX];
    char    buff[1024];
    char    ws[1024];
    char    pname[1024];
    char    lsock_id[1024] = {"0"};
    int     res;
    
    cefore_portnum = Default_PortNum;
    
    if (config_file_dir[0] != 0x00) {
        sprintf (file_path, "%s/cefnetd.conf", config_file_dir);
        strcpy (cef_conf_dir, config_file_dir);
    } else {
        wp = getenv ("CEFORE_DIR");
        if (wp != NULL && wp[0] != 0) {
            sprintf (file_path, "%s/cefore/cefnetd.conf", wp);
            sprintf (cef_conf_dir, "%s/cefore", wp);
        } else {
            sprintf (file_path, "/usr/local/cefore/cefnetd.conf");
            strcpy (cef_conf_dir, "/usr/local/cefore");
        }
    }
    
    fp = fopen (file_path, "r");
    if (fp == NULL) {
        fprintf (stderr,"[cefore] Failed to open %s\n", file_path);
        return (-1);
    }
    
    /* Reads and records written values in the cefnetd's config file. */
    while (fgets (buff, 1023, fp) != NULL) {
        buff[1023] = 0;
        
        if (buff[0] == 0x23/* '#' */) {
            continue;
        }
        
        res = cefore_trim_line_string (buff, pname, ws);
        if (res < 0) {
            continue;
        }
        
        if (strcmp (pname, "PORT_NUM") == 0) {
            res = atoi (ws);
            if (res < 1025) {
                fprintf (stderr,
                    "[cefore] PORT_NUM must be higher than 1024.\n");
                fclose (fp);
                return (-1);
            }
            if (res > 65535) {
                fprintf (stderr,
                    "[cefore] PORT_NUM must be lower than 65536.\n");
                fclose (fp);
                return (-1);
            }
            if (port_num == 0) {
                port_num = res;
            }
        } else if (strcmp (pname, "LOCAL_SOCK_ID") == 0) {
            strcpy (lsock_id, ws);
        }
    }
    if (port_num == 0) {
        port_num = Default_PortNum;
    }
    
    sprintf (cef_lsock_name, "/tmp/cbd_%d.%s", port_num, lsock_id);
    cefore_portnum = (unsigned short) port_num;
    
    fclose (fp);
    
    return (1);
}

int 
cefore_socket_create (
    void
) {
    struct sockaddr_un saddr;
    int sock;
    int flag;
    
    if ((sock = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
        return (-1);
    }
    
    /* initialize sockaddr_un       */
    memset (&saddr, 0, sizeof (saddr));
    saddr.sun_family = AF_UNIX;
    strcpy (saddr.sun_path, cef_lsock_name);
    
    /* prepares a source socket     */
#ifdef __APPLE__
    saddr.sun_len = sizeof (saddr);

    if (connect (sock, (struct sockaddr *)&saddr, SUN_LEN (&saddr)) < 0) {
        close (sock);
        return (-1);
    }
#else // __APPLE__
    if (connect (sock, (struct sockaddr*) &saddr,
            sizeof (saddr.sun_family) + strlen (cef_lsock_name)) < 0) {
        close (sock);
        return (-1);
    }
#endif // __APPLE__
    
    flag = fcntl (sock, F_GETFL, 0);
    if (flag < 0) {
        close (sock);
        return (-1);
    }
    if (fcntl (sock, F_SETFL, flag | O_NONBLOCK) < 0) {
        close (sock);
        return (-1);
    }
    return (sock);
}

void
cefire_socket_close (
    void
) {
    if (cefore_socket > 0) {
        send (cefore_socket, "/CLOSE:Face", strlen ("/CLOSE:Face"), 0);
        usleep (500000);
        close (cefore_socket);
        cefore_socket = -1;
    }
    return;
}

int 
cefore_xroute_init (
    void 
) {
    unsigned char *buff;
    int blocks;
    int rc;
    uint32_t msg_len, index, rcvd_size;
    uint16_t length;
    struct interface* ifp;
    CefT_Route route;
    struct pollfd fds[1];

    buff = calloc(1, 65535);
    
    rc = send (cefore_socket, Cmd_FibRet, Cmd_FibRet_Len, 0);
    if (rc < 0) {
        cefore_socket = -1;
        return (-1);
    }

    rcvd_size = 0;
    msg_len = 0;
    
RERECV:;
    fds[0].fd = cefore_socket;
    fds[0].events = POLLIN | POLLERR;
    poll(fds, 1, -1);
    if (fds[0].revents & POLLIN) {  
        rc = recv (cefore_socket, buff+rcvd_size , 65535, 0);
        if (rc < 0) {
            cefore_socket = -1;
            return (-1);
        }
    } else {
        cefore_socket = -1;
        return (-1);
    }
    rcvd_size += rc;
    if (rcvd_size == rc) {
        if ((rc < 5) || (buff[0] != 0x01)) {
            free (buff);
            return (-1);
        }
        memcpy (&msg_len, &buff[1], sizeof (uint32_t));
        blocks = (msg_len + 5) / 65535;
        if (((msg_len + 5) % 65535) != 0){
            blocks += 1;
        }
        if (blocks > 1) {
            void *new = realloc(buff, blocks * 65535);
            if (new == NULL) {
                free (buff);
                return (-1);
            }
            buff = new;
        }
    }
    if (rcvd_size < (msg_len+5)){
        goto RERECV;
    }
    index = 5;
    
    while (index < msg_len) {
        memcpy (&length, buff+index, sizeof (uint16_t));
        
        FOR_ALL_INTERFACES(ifp) {
            if(!if_up(ifp))
                continue;
            
            memset (&route, 0, sizeof (CefT_Route));
            
            memcpy (route.prefix, buff+(index + 2), length);
            route.plen      = length;
            route.metric    = 0;
            route.ifindex   = ifp->ifindex;
            route.src_plen  = 128;
            
            if (ifp->ipv4) {
                memcpy (&route.src_prefix[0], v4prefix, 16);
                memcpy (&route.src_prefix[12], ifp->ipv4, 4);
            } else {
                memcpy (&route.src_prefix[0], ifp->ll, 16);
            }
            add_xroute(route.prefix, route.plen, route.src_prefix, 0, 
                       route.metric, route.ifindex, 0);
            break;
        }
        index += sizeof (uint16_t) + length;
    }
    
    free (buff);
    return (1);
}

int 
cefore_xroute_update (
    unsigned char* msg, 
    int msg_len
) {
    int rc;
    uint16_t length, index;
    CefT_Route croute;
    struct xroute *xroute;
    struct babel_route *route;
    struct interface* ifp;
    
    if (msg[0] != 0x03) {
        return (-1);
    }
    memcpy (&length, &msg[1], sizeof (uint16_t));
    if (msg_len != length + 3) {
        return (-1);
    }
    index = 4;
    memcpy (&length, &msg[index], sizeof (uint16_t));
    memset (&croute, 0, sizeof (CefT_Route));
    
    memcpy (croute.prefix, &msg[index + 2], length);
    croute.plen     = length;
    croute.metric   = 0;
    croute.src_plen = 128;
    
    FOR_ALL_INTERFACES(ifp) {
        
        croute.ifindex   = ifp->ifindex;
        if (ifp->ipv4) {
            memcpy (&croute.src_prefix[0], v4prefix, 16);
            memcpy (&croute.src_prefix[12], ifp->ipv4, 4);
        } else {
            if(ifp->ll){
                memcpy (&croute.src_prefix[0], ifp->ll, 16);
            } else {
                continue;
            }
        }
        
        if (msg[3] == 0x01) {
            rc = add_xroute(
                croute.prefix, croute.plen, croute.src_prefix, 0, 
                croute.metric, croute.ifindex, 0);
            if (rc == 1) {
                if (route_ctrl_type == ROUTE_CTRL_TYPE_MM) {        	
                    struct source* src;
	                unsigned char src_prefix[16];
	                memset(src_prefix, 0, sizeof(src_prefix));
   	                clear_all_routes_mpms(croute.prefix, croute.plen, src_prefix, 0);
                    delete_bestroute_entry(croute.prefix, croute.plen, src_prefix, 0);
                    while(1) {
                        src = get_src_rcd_mp(croute.prefix, croute.plen, src_prefix, 0);
                        if (src == NULL){
        	                break;
                        }
                        delete_source_mp(src);
                    }
                }
                send_update(NULL, 0, 
                croute.prefix, croute.plen, croute.src_prefix, croute.src_plen);
            }
            break;
        } else {
            rc = 0;
            xroute = find_xroute(
                croute.prefix, croute.plen, croute.src_prefix, 0);
            if (xroute) {
                flush_xroute(xroute);
                rc = 1;
            }
            
            route = find_installed_route(
                croute.prefix, croute.plen, zeroes, 0);
            
            if (route) {
                route->installed = 0;
                route->refmetric = 0xFFFF;
                rc = 1;
            }
            
            if (rc == 1) {
                really_send_update_mp(NULL, myid, croute.prefix, croute.plen, croute.src_prefix, croute.src_plen,
                                    myseqno, INFINITY, cefore_portnum);
            }
        }
    }
    
    return (1);
}
int 
cefore_fib_add_req_send (
    const unsigned char* prefix, 
    int plen, 
    const unsigned char* nexthop, 
    unsigned short port,
    char* interface
) {
    unsigned char msg[65535];
    uint16_t index;
    struct tlv_hdr tlv_hdr;
    const char* node;
    uint16_t value16;
    int rc;
    char hostname[1024];

    if(cefore_socket == -1){
        return(-1);
    }
    
    /*-----------------------------------------------------------
        Create FIB Add Request
    -------------------------------------------------------------*/
    memcpy (&msg[0], Cmd_FibAdd, Cmd_FibAdd_Len);
    index = Cmd_FibAdd_Len + sizeof (uint16_t);
    
    /* Set T_NAME       */
    tlv_hdr.type   = 0x0000/* T_NAME */;
    tlv_hdr.length = (uint16_t) plen;
    memcpy (&msg[index], &tlv_hdr, sizeof (struct tlv_hdr));
    index += sizeof (struct tlv_hdr);
    memcpy (&msg[index], prefix, plen);
    index += plen;
    
    /* Set T_NODE       */
    node = format_address (nexthop);
    if (v4mapped(nexthop)) {
        sprintf (hostname, "%s:%d", node, port);
    } else {
        if (strncmp (node, "fe80", strlen("fe80")) == 0) {
            sprintf (hostname, "[%s%%%s]:%d", node, interface, port);
        } else {
            sprintf (hostname, "[%s]:%d", node, port);
        }
    }
    value16 = (uint16_t) strlen (hostname);
    
    tlv_hdr.type   = 0x0001/* T_NODE */;
    tlv_hdr.length = value16;
    memcpy (&msg[index], &tlv_hdr, sizeof (struct tlv_hdr));
    index += sizeof (struct tlv_hdr);
    
    memcpy (&msg[index], hostname, value16);
    index += value16;
    
    memcpy (&msg[index], &port, sizeof (unsigned short));
    index += sizeof (unsigned short);
    
    value16 = index - (Cmd_FibAdd_Len + sizeof (uint16_t));
    memcpy (&msg[Cmd_FibAdd_Len], &value16, sizeof (uint16_t));
    

    rc = send (cefore_socket, msg, index, 0);
    if (rc < 0) {
        cefore_socket = -1;
        return (-1);
    }
{
    struct pollfd fds[1];
    fds[0].fd = cefore_socket;
    fds[0].events = POLLIN | POLLERR;
    poll(fds, 1, -1);
    if (fds[0].revents & POLLIN) {
        rc = recv (cefore_socket, msg, 65535, 0);
        if (rc < 0) {
            cefore_socket = -1;
            return (-1);
        }
    } else {
        cefore_socket = -1;
        return (-1);
    }
}

    if ((msg[0] != 0x02) || (rc != 3)) {
        return (-1);
    }
    
    return (1);
}

int 
cefore_fib_del_req_send (
    const unsigned char* prefix, 
    int plen, 
    const unsigned char* nexthop, 
    unsigned short port,
    char* interface
) {
    unsigned char msg[65535];
    uint16_t index;
    struct tlv_hdr tlv_hdr;
    const char* node;
    uint16_t value16;
    int rc;
    char hostname[1024];
    
    /*-----------------------------------------------------------
        Create FIB Delete Request
    -------------------------------------------------------------*/
    memcpy (&msg[0], Cmd_FibDel, Cmd_FibDel_Len);
    index = Cmd_FibDel_Len + sizeof (uint16_t);
    
    /* Set T_NAME       */
    tlv_hdr.type   = 0x0000/* T_NAME */;
    tlv_hdr.length = plen;
    memcpy (&msg[index], &tlv_hdr, sizeof (struct tlv_hdr));
    index += sizeof (struct tlv_hdr);
    memcpy (&msg[index], prefix, plen);
    index += plen;
    
    /* Set T_NODE       */
    node = format_address (nexthop);
    if (v4mapped(nexthop)) {
        sprintf (hostname, "%s:%d", node, port);
    } else {
        if (strncmp (node, "fe80", strlen("fe80")) == 0) {
            sprintf (hostname, "[%s%%%s]:%d", node, interface, port);
        } else {
            sprintf (hostname, "[%s]:%d", node, port);
        }
    }
    value16 = (uint16_t) strlen (hostname);
    
    tlv_hdr.type   = 0x0001/* T_NODE */;
    tlv_hdr.length = value16;
    memcpy (&msg[index], &tlv_hdr, sizeof (struct tlv_hdr));
    index += sizeof (struct tlv_hdr);
    
    memcpy (&msg[index], hostname, value16);
    index += value16;
    
    value16 = index - (Cmd_FibDel_Len + sizeof (uint16_t));
    memcpy (&msg[Cmd_FibDel_Len], &value16, sizeof (uint16_t));
    
//  cef_buff_print (msg, index);
    
    rc = send (cefore_socket, msg, index, 0);
    if (rc < 0) {
        cefore_socket = -1;
        return (-1);
    }
{
    struct pollfd fds[1];
    fds[0].fd = cefore_socket;
    fds[0].events = POLLIN | POLLERR;
    poll(fds, 1, -1);
    if (fds[0].revents & POLLIN) {
        rc = recv (cefore_socket, msg, 65535, 0);
        if (rc < 0) {
            cefore_socket = -1;
            return (-1);
        }
    } else {
        cefore_socket = -1;
        return (-1);
    }
}
    if ((msg[0] != 0x02) || (rc != 3)) {
        return (-1);
    }
    
    return (index);
}

static int
cefore_trim_line_string (
    const char* p1,                             /* target string for trimming           */
    char* p2,                                   /* name string after trimming           */
    char* p3                                    /* value string after trimming          */
) {
    char ws[1024];
    char* wp = ws;
    char* rp = p2;
    int equal_f = -1;

    while (*p1) {
        if ((*p1 == 0x0D) || (*p1 == 0x0A)) {
            break;
        }

        if ((*p1 == 0x20) || (*p1 == 0x09)) {
            p1++;
            continue;
        } else {
            *wp = *p1;
        }

        p1++;
        wp++;
    }
    *wp = 0x00;
    wp = ws;

    while (*wp) {
        if (*wp == 0x3d /* '=' */) {
            if (equal_f > 0) {
                return (-1);
            }
            equal_f = 1;
            *rp = 0x00;
            rp = p3;
        } else {
            *rp = *wp;
            rp++;
        }
        wp++;
    }
    *rp = 0x00;

    return (equal_f);
}

int
cefbabeld_tcp_sock_create (
    uint16_t        port_num
) {

    int tcp_listen_fd = -1;
    struct sockaddr *ai_addr;
    socklen_t           ai_addrlen;
    struct addrinfo hints;
    struct addrinfo* res;
    struct addrinfo* cres;
    int err;
    char port_str[32];
    int sock;
    int create_sock_f = 0;
    int reuse_f = 1;
    int flag;

    /* Creates the hint         */
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    sprintf (port_str, "%d", port_num);

    /* Obtains the addrinfo         */
    if ((err = getaddrinfo (NULL, port_str, &hints, &res)) != 0) {
        fprintf(stderr, "[cefore] ERROR: Could not create the TCP listen socket. : %s\n", gai_strerror (err));
        return (-1);
    }

    for (cres = res; cres != NULL; cres = cres->ai_next) {
        sock = socket (cres->ai_family, cres->ai_socktype, 0);
        if (sock < 0) {
            fprintf(stderr, "[cefore] Warning: Could not create the TCP listen socket. : %s\n", strerror (errno));
            continue;
        }
        setsockopt (sock,
            SOL_SOCKET, SO_REUSEADDR, &reuse_f, sizeof (reuse_f));
{
        int optval = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char *)&optval, sizeof(optval)) < 0){
            fprintf(stderr, "[cefore] ERROR: %s (setsockopt:%s)\n", __func__, strerror(errno));
            close (sock);
            continue;
        }
}

        switch (cres->ai_family) {
            case AF_INET: {
                create_sock_f = 1;
                ai_addr         = cres->ai_addr;
                ai_addrlen  = cres->ai_addrlen;
                tcp_listen_fd   = sock;
                break;
            }
            case AF_INET6: {
                create_sock_f = 1;
                ai_addr         = cres->ai_addr;
                ai_addrlen  = cres->ai_addrlen;
                tcp_listen_fd   = sock;
                break;
            }
            default: {
                /* NOP */
                fprintf(stderr, "[cefore] Warning: Unknown socket family : %d\n", cres->ai_family);
                break;
            }
        }
        if (create_sock_f) {
            break;
        }
    }

    if (create_sock_f) {

        if (bind (tcp_listen_fd, ai_addr, ai_addrlen) < 0) {
            close (tcp_listen_fd);
            fprintf(stderr, "[cefore] ERROR: Could not create the TCP listen socket. : %s\n", strerror (errno));
            return (-1);
        }
        if (listen (tcp_listen_fd, 32) < 0) {
            fprintf(stderr, "[cefore] ERROR: Could not create the TCP listen socket. : %s\n", strerror (errno));
            return (-1);
        }
        flag = fcntl (tcp_listen_fd, F_GETFL, 0);
        if (flag < 0) {
            fprintf(stderr, "[cefore] ERROR: Could not create the TCP listen socket. : %s\n", strerror (errno));
            return (-1);
        }
        if (fcntl (tcp_listen_fd, F_SETFL, flag | O_NONBLOCK) < 0) {
            fprintf(stderr, "[cefore] ERROR: Could not create the TCP listen socket. : %s\n", strerror (errno));
            return (-1);
        }

        return (tcp_listen_fd);
    }

    return (-1);
}

void
cefbabel_tcp_stat_prcess ()
{
    struct sockaddr_storage* sa;
    socklen_t len = sizeof (struct sockaddr_storage);
    static int tcp_listen_fd = -1;
    int cs;
    int flag;
    char ip_str[NI_MAXHOST];
    char port_str[NI_MAXSERV];
    int new_accept_f = 1;
    int err;
    unsigned char buff[CefC_Cbabel_Stat_Mtu];
    struct pollfd fds[1];
    int rlen;
    ///int blen;
    int res;
    uint16_t value16; 
    uint32_t value32; 
    uint32_t index = 0;

    /* Create listen socket */
    if (tcp_listen_fd == -1) {
        tcp_listen_fd = cefbabeld_tcp_sock_create(protocol_port);
        if (tcp_listen_fd < 0) {
            fprintf(stderr, "[cefore] ERROR: Fail to create the TCP listen socket.\n");
            return;
        }
    }
    
    /* Accepts the TCP SYN      */
    sa = (struct sockaddr_storage*) malloc (sizeof (struct sockaddr_storage));
    memset (sa, 0, sizeof (struct sockaddr_storage));
    cs = accept (tcp_listen_fd, (struct sockaddr*) sa, &len);
    if (cs < 0) {
        if (sa) {
            free (sa);
        }
        return;
    }

    flag = fcntl (cs, F_GETFL, 0);
    if (flag < 0) {
        fprintf(stderr, "[cefore] Warning: Failed to create new tcp connection : %s\n", strerror (errno));
        goto POST_ACCEPT;
    }
    if (fcntl (cs, F_SETFL, flag | O_NONBLOCK) < 0) {
        fprintf(stderr, "[cefore] Warning: Failed to create new tcp connection : %s\n", strerror (errno));
        goto POST_ACCEPT;
    }
    if ((err = getnameinfo ((struct sockaddr*) sa, len, ip_str, sizeof (ip_str),
            port_str, sizeof (port_str), NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
        fprintf(stderr, "[cefore] Warning: Failed to create new tcp connection : %s\n", gai_strerror (err));
        goto POST_ACCEPT;
    }

    if (new_accept_f) {
        cef_cbabel_send_msg (cs,
            (unsigned char*) CefC_Cbabel_Cmd_ConnOK, strlen (CefC_Cbabel_Cmd_ConnOK));

        /* Recive cmd from cefbablestatus. */
        fds[0].fd     = cs;
        fds[0].events = POLLIN | POLLERR;
        res = poll (fds, 1, 100);
        if (res == 0) {
            /* poll time out */
            goto POST_ACCEPT;
        }
        if (fds[0].revents & (POLLERR | POLLNVAL | POLLHUP)) {
            ; /* NOP */
        } else if (fds[0].revents & POLLIN) {
            rlen = recv (fds[0].fd, buff, 65535, 0);
            if (rlen > 0) {
                if (buff[CefC_O_Fix_Ver] != CefC_Version) {
                    goto POST_ACCEPT;
                }
                if (buff[CefC_O_Fix_Type] == CefC_Cbabel_Msg_Type_Status) {
                    memcpy (&value16, &buff[CefC_O_Length], CefC_S_Length);
                    ///blen = ntohs (value16);

                    /* set header   */
                    buff[CefC_O_Fix_Ver]  = CefC_Version;
                    /* Get Status   */
                    buff[CefC_O_Fix_Type] = CefC_Cbabel_Msg_Type_Status;
                    index += CefC_Cbabel_RspMsg_HeaderLen;
                    {
                        char rsp[128];
                        sprintf (rsp, "Number of Sent Update TLV  : %d", cefstat_sent_update_num);
                        memcpy (&buff[index], rsp, strlen(rsp)+1);
                        index += strlen(rsp)+1;
                    }
                    /* set Length   */
                    value32 = htonl (index);
                    memcpy (buff + CefC_O_Length, &value32, CefC_L_Length);
                    /* Send rsp to cefbablestatus. */
                    cef_cbabel_send_msg (cs, buff, index);
                } else {
                    goto POST_ACCEPT;
                }
            }
        }
    }

POST_ACCEPT:
    close (cs);
    if (sa) {
        free (sa);
    }
    return;
}
static int                                  /* The return value is negative if an error occurs  */
cef_cbabel_send_msg (
    int fd,                                 /* socket fd                                */
    unsigned char* msg,                     /* send message                             */
    uint16_t msg_len                        /* message length                           */
) {
    int res;
    res = send (fd, msg, msg_len, 0);
    if (res < 0) {
        if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
            return (-1);
        }
    }
    return (0);
}

#endif //----- ADD -----


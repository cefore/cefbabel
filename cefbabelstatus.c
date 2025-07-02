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
 * cefbabelstatus.c
 */

#define __CEFBABEL_STATUS_SOURCE__

/****************************************************************************************
 Include Files
 ****************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "cefore.h"

/****************************************************************************************
 Macros
 ****************************************************************************************/


/****************************************************************************************
 Structures Declaration
 ****************************************************************************************/


/****************************************************************************************
 State Variables
 ****************************************************************************************/


/****************************************************************************************
Function Declaration
 ****************************************************************************************/
static void
print_usage (
    void
);
static int                                  /* created socket                           */
cef_connect_tcp_to_cbabeld (
    const char* dest, 
    const char* port
);
static int                                  /* The return value is negative if an error occurs  */
cef_cbabel_send_msg (
    int fd,
    unsigned char* msg,
    uint16_t msg_len
);

/****************************************************************************************
 ****************************************************************************************/

int
main (
    int argc,
    char** argv
) {
    int tcp_sock;
    int res;
    /////int frame_size;
    unsigned char buff[CefC_Cbabel_Stat_Mtu] = {0};
    uint16_t value16;
    struct pollfd fds[1];
    unsigned char *frame;
    char dst[64] = {0};
    char port_str[32] = {0};
    int i;
    char*   work_arg;
    uint32_t msg_len, rcvd_size;
    int rc;
    int blocks;

    /***** flags        *****/
    int host_f          = 0;
    int port_f          = 0;

    /***** state variavles  *****/
    uint16_t index      = 0;

    /* Obtains options      */
    for (i = 1 ; i < argc ; i++) {

        work_arg = argv[i];
        if (work_arg == NULL || work_arg[0] == 0) {
            break;
        }

        if (strcmp (work_arg, "-h") == 0) {
            if (host_f) {
                fprintf (stderr, "cefbabelstatus: [ERROR] host is duplicated.");
                print_usage ();
                return (-1);
            }
            if (i + 1 == argc) {
                fprintf (stderr, "cefbabelstatus: [ERROR] host is not specified.");
                print_usage ();
                return (-1);
            }
            work_arg = argv[i + 1];
            strcpy (dst, work_arg);
            host_f++;
            i++;
        } else if (strcmp (work_arg, "-p") == 0) {
            if (port_f) {
                fprintf (stderr, "cefbabelstatus: [ERROR] port is duplicated.");
                print_usage ();
                return (-1);
            }
            if (i + 1 == argc) {
                fprintf (stderr, "cefbabelstatus: [ERROR] port is not specified.");
                print_usage ();
                return (-1);
            }
            work_arg = argv[i + 1];
            strcpy (port_str, work_arg);
            port_f++;
            i++;
        } else {

            work_arg = argv[i];

            if (work_arg[0] == '-') {
                fprintf (stderr, "cefbabelstatus: [ERROR] unknown option is specified.");
                print_usage ();
                return (-1);
            }
        }
    }

    /* check port flag */
    if (port_f == 0) {
        sprintf (port_str, "%d", CefC_Default_Tcp_Prot);
    }

    /* check dst flag */
    if (host_f == 0) {
        strcpy (dst, "127.0.0.1");
    }
    fprintf (stderr, "\ncefbabelstatus: Connect to %s:%s\n", dst, port_str);
    tcp_sock = cef_connect_tcp_to_cbabeld (dst, port_str);

    if (tcp_sock < 1) {
        fprintf (stderr, "cefbabelstatus: [ERROR] connect to cefbabeld\n");
        return (0);
    }
    
    /* Create Upload Request message    */
    /* set header   */
    buff[CefC_O_Fix_Ver]  = CefC_Version;
    /* Get Status   */
    buff[CefC_O_Fix_Type] = CefC_Cbabel_Msg_Type_Status;
    index += CefC_Cbabel_CmdMsg_HeaderLen;
    /* set Length   */
    value16 = htons (index);
    memcpy (buff + CefC_O_Length, &value16, CefC_S_Length);
    
    /* send message */
    res = cef_cbabel_send_msg (tcp_sock, buff, index);
    if (res < 0) {
        fprintf (stderr, "cefbabelstatus: [ERROR] Send message\n");
        close (tcp_sock);
        return (-1);
    }

    /* receive message  */
    rcvd_size = 0;
    /////frame_size = 0;
    msg_len = 0;
    frame = calloc (1, CefC_Cbabel_Stat_Mtu);
    if (frame == NULL) {
        fprintf (stderr, "cefbabelstatus: Frame buffer allocation (alloc) error\n");
        close (tcp_sock);
        return (-1);
    }

RERECV:;
    fds[0].fd = tcp_sock;
    fds[0].events = POLLIN | POLLERR;
    res = poll(fds, 1, 60000);
    if (res < 0) {
        /* poll error   */
        fprintf (stderr, "cefbabelstatus: poll error (%s)\n", strerror (errno));
        close (tcp_sock);
        free (frame);
        return (-1);
    } else  if (res == 0) {
        /* timeout  */
        fprintf (stderr, "cefbabelstatus: timeout\n");
        close (tcp_sock);
        free (frame);
        return (-1);
    }
    if (fds[0].revents & POLLIN) {  
        rc = recv (tcp_sock, frame+rcvd_size , CefC_Cbabel_Stat_Mtu, 0);
        if (rc < 0) {
            fprintf (stderr, "cefbabelstatus: Receive message error (%s)\n", strerror (errno));
            close (tcp_sock);
            free (frame);
            return (-1);
        }
    } else {
        if (fds[0].revents & POLLERR) {
            fprintf (stderr, "cefbabelstatus: Poll event is POLLERR\n");
        } else if (fds[0].revents & POLLNVAL) {
            fprintf (stderr, "cefbabelstatus: Poll event is POLLNVAL\n");
        } else {
            fprintf (stderr, "cefbabelstatus: Poll event is POLLHUP\n");
        }
        close (tcp_sock);
        free (frame);
        return (-1);
    }
    rcvd_size += rc;
    if (rcvd_size == rc) {
        if ((rc < 6/* Ver(1)+Type(1)+Length(4) */) 
            || (frame[CefC_O_Fix_Ver] != CefC_Version)
            || (frame[CefC_O_Fix_Type] != CefC_Cbabel_Msg_Type_Status) ){
            fprintf (stderr, "cefbabelstatus: Response type is not status\n");
            close (tcp_sock);
            free (frame);
            return (-1);
        }
            
        memcpy (&msg_len, &frame[2], sizeof (uint32_t));
        msg_len = ntohl (msg_len);
        blocks = (msg_len) / CefC_Cbabel_Stat_Mtu;
        if (((msg_len) % CefC_Cbabel_Stat_Mtu) != 0){
            blocks += 1;
        }
        if (blocks > 1) {
            void *new = realloc(frame, blocks * CefC_Cbabel_Stat_Mtu);
            if (new == NULL) {
                fprintf (stderr, "cefbabelstatus: Frame buffer allocation (realloc) error\n");
                close (tcp_sock);
                free (frame);
                return (-1);
            }
            frame = new;
        }
    }
    if (rcvd_size < msg_len){
        goto RERECV;
    }
    /////frame_size = msg_len;
    /* Output responce */
    fprintf (stderr, "-----\n");
    fprintf (stderr, "%s\n", &frame[CefC_Cbabel_RspMsg_HeaderLen]);
    fprintf (stderr, "\n");
    
    close (tcp_sock);
    free (frame);
    return (0);
}
static void
print_usage (
    void
) {
    fprintf (stderr,
        "\nUsage: cefbabelstatus\n\n"
        "  cefbabelstatus [-h host] [-p port]\n\n"
        "  host   Specify the host identifier (e.g., IP address) on which cefbabeld \n"
        "         is running. The default value is localhost (i.e., 127.0.0.1).\n"
        "  port   Port number to connect cefbabelstatus. The default value is 9897.\n\n"
    );
    return;
}
static int                                          /* created socket                           */
cef_connect_tcp_to_cbabeld (
    const char* dest, 
    const char* port
) {
    struct addrinfo hints;
    struct addrinfo* res;
    struct addrinfo* cres;
    struct addrinfo* nres;
    int err;
    unsigned char cmd[CefC_Cbabel_Cmd_MaxLen];
    int sock;
    int flag;
    fd_set readfds;
    struct timeval timeout;
    int ret;
    
    /* Creates the hint         */
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;
    
    /* Obtains the addrinfo     */
    if ((err = getaddrinfo (dest, port, &hints, &res)) != 0) {
        fprintf (stderr, "ERROR : connect_tcp_to_cefbabeld (getaddrinfo)\n");
        return (-1);
    }
    
    for (cres = res ; cres != NULL ; cres = nres) {
        nres = cres->ai_next;
        
        sock = socket (cres->ai_family, cres->ai_socktype, cres->ai_protocol);
        
        if (sock < 0) {
            free (cres);
            continue;
        }
        
        flag = fcntl (sock, F_GETFL, 0);
        if (flag < 0) {
            close (sock);
            free (cres);
            continue;
        }
        if (fcntl (sock, F_SETFL, flag | O_NONBLOCK) < 0) {
            close (sock);
            free (cres);
            continue;
        }
        if (connect (sock, cres->ai_addr, cres->ai_addrlen) < 0) {
            /* NOP */;
        }
        
        FD_ZERO (&readfds);
        FD_SET (sock, &readfds);
        timeout.tv_sec  = 5;
        timeout.tv_usec = 0;
        ret = select (sock + 1, &readfds, NULL, NULL, &timeout);
        
        if (ret == 0) {
            close (sock);
            free (cres);
            continue;
        } else if (ret < 0) {
            close (sock);
            free (cres);
            continue;
        } else {
            if (FD_ISSET (sock, &readfds)) {
                ret = read (sock, cmd, CefC_Cbabel_Cmd_MaxLen);
                if (ret < 1) {
                    close (sock);
                    free (cres);
                    continue;
                } else {
                    if (memcmp (CefC_Cbabel_Cmd_ConnOK, cmd, ret)) {
                        close (sock);
                        free (cres);
                        continue;
                    }
                    /* NOP */;
                }
            }
        }
        freeaddrinfo (res);
        return (sock);
    }
    return (-1);
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


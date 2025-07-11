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
Copyright (c) 2010 by Vincent Gross

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

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <assert.h>
#ifndef BABELD_CODE //+++++ ADD +++++
#include <limits.h>
#include <string.h>
#include <poll.h>
#endif //----- ADD -----

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "babeld.h"
#include "util.h"
#include "net.h"
#include "kernel.h"
#include "interface.h"
#include "source.h"
#include "neighbour.h"
#include "route.h"
#include "xroute.h"
#include "message.h"
#include "resend.h"
#include "configuration.h"
#include "local.h"
#include "rule.h"
#ifdef BABELD_CODE //+++++ REPLACE +++++
#include "version.h"
#else //CEFBAELD
#include "cefversion.h"
#endif //-----  REPLACE -----
#ifndef BABELD_CODE //+++++ ADD +++++
#include "cefore.h"
#endif //----- ADD -----

struct timeval now;

unsigned char myid[8];
int have_id = 0;
int debug = 0;

int link_detect = 0;
int all_wireless = 0;
int has_ipv6_subtrees = 0;
int default_wireless_hello_interval = -1;
int default_wired_hello_interval = -1;
int resend_delay = -1;
int random_id = 0;
int do_daemonise = 0;
int skip_kernel_setup = 0;
#ifdef BABELD_CODE //+++++ REPLACE +++++
const char *logfile = NULL,
    *pidfile = "/var/run/babeld.pid",
    *state_file = "/var/lib/babel-state";
#else // CEFBABELD
const char *logfile = NULL,
    *pidfile = "/var/run/cefbabeld.pid",
    *state_file = "/var/lib/cefbabel-state";
#endif //----- REPLACE -----

unsigned char *receive_buffer = NULL;
int receive_buffer_size = 0;

const unsigned char zeroes[16] = {0};
const unsigned char ones[16] =
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

int protocol_port;
unsigned char protocol_group[16];
int protocol_socket = -1;
int kernel_socket = -1;
#ifndef BABELD_CODE //+++++ ADD +++++
int cefore_socket = -1;
int route_ctrl_type;        /* route control type */
unsigned short cefore_portnum = 0;
int cefstat_sent_update_num = 0;
#endif //----- ADD -----
static int kernel_routes_changed = 0;
static int kernel_rules_changed = 0;
static int kernel_link_changed = 0;
static int kernel_addr_changed = 0;

struct timeval check_neighbours_timeout, check_interfaces_timeout;

static volatile sig_atomic_t exiting = 0, dumping = 0, reopening = 0;

static int accept_local_connections(void);
static void init_signals(void);
static void dump_tables(FILE *out);

static int
kernel_route_notify(struct kernel_route *route, void *closure)
{
    kernel_routes_changed = 1;
    return -1;
}

static int
kernel_addr_notify(struct kernel_addr *addr, void *closure)
{
    kernel_addr_changed = 1;
    return -1;
}

static int
kernel_link_notify(struct kernel_link *link, void *closure)
{
    struct interface *ifp;
    FOR_ALL_INTERFACES(ifp) {
        if(strcmp(ifp->name, link->ifname) == 0) {
            kernel_link_changed = 1;
            return -1;
        }
    }
    return 0;
}

static int
kernel_rule_notify(struct kernel_rule *rule, void *closure)
{
    int i;
    if(martian_prefix(rule->src, rule->src_plen))
        return 0;

    i = rule->priority - src_table_prio;

    if(i < 0 || SRC_TABLE_NUM <= i)
        return 0;

    kernel_rules_changed = 1;
    return -1;
}

int
main(int argc, char **argv)
{
    struct sockaddr_in6 sin6;
    int rc, fd, i, opt;
    time_t expiry_time, source_expiry_time, kernel_dump_time;
    const char **config_files = NULL;
    int num_config_files = 0;
    void *vrc;
    unsigned int seed;
    struct interface *ifp;
#ifndef BABELD_CODE //+++++ ADD +++++
    char    file_path[PATH_MAX] = {0};
    int     port_num = 0;
    unsigned char cefbuff[65535];
#endif //----- ADD -----

    gettime(&now);

    rc = read_random_bytes(&seed, sizeof(seed));
    if(rc < 0) {
        perror("read(random)");
        seed = 42;
    }

    seed ^= (now.tv_sec ^ now.tv_usec);
    srandom(seed);

    parse_address("ff02:0:0:0:0:0:1:6", protocol_group, NULL);
#ifdef BABELD_CODE //+++++ REPLACE +++++
    protocol_port = 6696;
#else // CEFBABELD
    protocol_port = 9897;
    route_ctrl_type = ROUTE_CTRL_TYPE_S;
#endif //----- REPLACE -----
    change_smoothing_half_life(4);
    has_ipv6_subtrees = kernel_has_ipv6_subtrees();

    while(1) {
        opt = getopt(argc, argv,
#ifdef BABELD_CODE //+++++ REPLACE +++++
                     "m:p:h:H:i:k:A:srS:d:g:G:lwz:M:t:T:c:C:DL:I:V");
#else // CEFBABELD
                     "X:x:P:m:p:h:H:S:d:M:c:C:DL:I:V");
#endif //----- REPLACE -----
        if(opt < 0)
            break;

        switch(opt) {
#ifndef BABELD_CODE //+++++ ADD +++++
        case 'x':
            strcpy (file_path, optarg);
            break;
        case 'X':
            port_num = atoi (optarg);
            if (port_num < 1025) {
                fprintf(stderr,
                    "cefore port must be higher than 1024.\n");
                goto usage;
            }
            if (port_num > 65535) {
                fprintf(stderr,
                    "cefore port must be lower than 65536.\n");
                goto usage;
            }
            break;
        case 'P':
            if (strcmp (optarg, "S") == 0){
                route_ctrl_type = ROUTE_CTRL_TYPE_S;
            } else if (strcmp (optarg, "MS") == 0){
                route_ctrl_type = ROUTE_CTRL_TYPE_MS;
            } else if (strcmp (optarg, "MM") == 0){
                route_ctrl_type = ROUTE_CTRL_TYPE_MM;
            } else {
                fprintf(stderr,
                    "route_control_type must be \"S\", \"MS\" or \"MM\".\n");
                goto usage;
            }
            break;
#endif //----- ADD -----
        case 'm':
            rc = parse_address(optarg, protocol_group, NULL);
            if(rc < 0)
                goto usage;
            if(protocol_group[0] != 0xff) {
                fprintf(stderr,
                        "%s is not a multicast address\n", optarg);
                goto usage;
            }
            if(protocol_group[1] != 2) {
                fprintf(stderr,
                        "Warning: %s is not a link-local multicast address\n",
                        optarg);
            }
            break;
        case 'p':
            protocol_port = parse_nat(optarg);
            if(protocol_port <= 0 || protocol_port > 0xFFFF)
                goto usage;
            break;
        case 'h':
            default_wireless_hello_interval = parse_thousands(optarg);
            if(default_wireless_hello_interval <= 0 ||
               default_wireless_hello_interval > 0xFFFF * 10)
                goto usage;
            break;
        case 'H':
            default_wired_hello_interval = parse_thousands(optarg);
            if(default_wired_hello_interval <= 0 ||
               default_wired_hello_interval > 0xFFFF * 10)
                goto usage;
            break;
        case 'k':
            kernel_metric = parse_nat(optarg);
            if(kernel_metric < 0 || kernel_metric > 0xFFFF)
                goto usage;
            break;
        case 'A':
            allow_duplicates = parse_nat(optarg);
            if(allow_duplicates < 0 || allow_duplicates > 0xFFFF)
                goto usage;
            break;
        case 's':
            split_horizon = 0;
            break;
        case 'r':
            random_id = 1;
            break;
        case 'S':
            state_file = optarg;
            break;
        case 'd':
            debug = parse_nat(optarg);
            if(debug < 0)
                goto usage;
#ifndef BABELD_CODE //+++++ ADD +++++
            if(debug > 2)
                goto usage;
#endif //----- ADD -----
            break;
#ifdef BABELD_CODE //+++++ DEL +++++
        case 'g':
        case 'G':
            if(opt == 'g')
                local_server_write = 0;
            else
                local_server_write = 1;
            if(optarg[0] == '/') {
                local_server_port = -1;
                free(local_server_path);
                local_server_path = strdup(optarg);
            } else {
                local_server_port = parse_nat(optarg);
                free(local_server_path);
                local_server_path = NULL;
                if(local_server_port <= 0 || local_server_port > 0xFFFF)
                    goto usage;
            }
            break;
        case 'l':
            link_detect = 1;
            break;
        case 'w':
            all_wireless = 1;
            break;
        case 'z':
            {
                char *comma;
                diversity_kind = (int)strtol(optarg, &comma, 0);
                if(*comma == '\0')
                    diversity_factor = 128;
                else if(*comma == ',')
                    diversity_factor = parse_nat(comma + 1);
                else
                    goto usage;
                if(diversity_factor <= 0 || diversity_factor > 256)
                    goto usage;
            }
            break;
#endif //----- DEL -----
        case 'M': {
            int l = parse_nat(optarg);
            if(l < 0 || l > 3600)
                goto usage;
#ifndef BABELD_CODE //+++++ ADD +++++
            do_debugf(1, "-M half-time=%d\n", l);
#endif //----- ADD -----
            change_smoothing_half_life(l);
            break;
        }
#ifdef BABELD_CODE //+++++ DEL +++++
        case 't':
            export_table = parse_nat(optarg);
            if(export_table < 0 || export_table > 0xFFFF)
                goto usage;
            break;
        case 'T':
            if(add_import_table(parse_nat(optarg)))
                goto usage;
            break;
#endif //----- DEL -----
        case 'c':
            config_files = realloc(config_files,
                                   (num_config_files + 1) * sizeof(char*));
            if(config_files == NULL) {
                fprintf(stderr, "Couldn't allocate config file.\n");
                exit(1);
            }
            config_files[num_config_files++] = optarg;
            break;
        case 'C':
            rc = parse_config_from_string(optarg, strlen(optarg), NULL);
            if(rc != CONFIG_ACTION_DONE) {
                fprintf(stderr,
                        "Couldn't parse configuration from command line.\n");
                exit(1);
            }
            break;
        case 'D':
            do_daemonise = 1;
            break;
        case 'L':
            logfile = optarg;
            break;
        case 'I':
            pidfile = optarg;
            break;
        case 'V':
#ifdef BABELD_CODE //+++++ REPLACE +++++
            fprintf(stderr, "%s\n", BABELD_VERSION);
#else // CEFBABELD
            fprintf(stderr, "%s\n", CEFBABELD_VERSION);
#endif //----- REPLACE -----
            exit(0);
            break;
        default:
            goto usage;
        }
    }

    if(num_config_files == 0) {
#ifdef BABELD_CODE //+++++ REPLACE +++++
        if(access("/etc/babeld.conf", F_OK) >= 0) {
#else // CEFBABELD
        if(access("/usr/local/cefore/cefbabeld.conf", F_OK) >= 0) {
#endif //----- REPLACE -----
            config_files = malloc(sizeof(char*));
            if(config_files == NULL) {
                fprintf(stderr, "Couldn't allocate config file.\n");
                exit(1);
            }
#ifdef BABELD_CODE //+++++ REPLACE +++++
            config_files[num_config_files++] = "/etc/babeld.conf";
#else // CEFBABELD
            config_files[num_config_files++] = "/usr/local/cefore/cefbabeld.conf";
#endif //----- REPLACE -----
        }
    }

    for(i = 0; i < num_config_files; i++) {
        int line;
        rc = parse_config_from_file(config_files[i], &line);
        if(rc < 0) {
            fprintf(stderr,
                    "Couldn't parse configuration from file %s "
                    "(error at line %d).\n",
                    config_files[i], line);
            exit(1);
        }
    }

    free(config_files);

    if(default_wireless_hello_interval <= 0)
        default_wireless_hello_interval = 4000;
    default_wireless_hello_interval = MAX(default_wireless_hello_interval, 5);

    if(default_wired_hello_interval <= 0)
        default_wired_hello_interval = 4000;
    default_wired_hello_interval = MAX(default_wired_hello_interval, 5);

    resend_delay = 2000;
    resend_delay = MIN(resend_delay, default_wireless_hello_interval / 2);
    resend_delay = MIN(resend_delay, default_wired_hello_interval / 2);
    resend_delay = MAX(resend_delay, 20);

    if(do_daemonise) {
        if(logfile == NULL)
#ifdef BABELD_CODE //+++++ REPLACE +++++
            logfile = "/var/log/babeld.log";
#else // CEFBABELD
            logfile = "/var/log/cefbabeld.log";
#endif //----- REPLACE -----
    }

    rc = reopen_logfile();
    if(rc < 0) {
        perror("reopen_logfile()");
        exit(1);
    }

    fd = open("/dev/null", O_RDONLY);
    if(fd < 0) {
        perror("open(null)");
        exit(1);
    }

    rc = dup2(fd, 0);
    if(rc < 0) {
        perror("dup2(null, 0)");
        exit(1);
    }

    close(fd);

    if(do_daemonise) {
        rc = daemonise();
        if(rc < 0) {
            perror("daemonise");
            exit(1);
        }
    }

    if(pidfile && pidfile[0] != '\0') {
        int pfd, len;
        char buf[100];

        len = snprintf(buf, 100, "%lu", (unsigned long)getpid());
        if(len < 0 || len >= 100) {
            perror("snprintf(getpid)");
            exit(1);
        }

        pfd = open(pidfile, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if(pfd < 0) {
            char buf[40];
            snprintf(buf, 40, "creat(%s)", pidfile);
            buf[39] = '\0';
            perror(buf);
            exit(1);
        }

        rc = write(pfd, buf, len);
        if(rc < len) {
            perror("write(pidfile)");
            goto fail_pid;
        }

        close(pfd);
    }

    rc = kernel_setup(1);
    if(rc < 0) {
        fprintf(stderr, "kernel_setup failed.\n");
        goto fail_pid;
    }

    rc = kernel_setup_socket(1);
    if(rc < 0) {
        fprintf(stderr, "kernel_setup_socket failed.\n");
        kernel_setup(0);
        goto fail_pid;
    }

    rc = finalise_config();
    if(rc < 0) {
        fprintf(stderr, "Couldn't finalise configuration.\n");
        goto fail;
    }

    for(i = optind; i < argc; i++) {
        vrc = add_interface(argv[i], NULL);
        if(vrc == NULL)
            goto fail;
    }

    if(interfaces == NULL) {
        fprintf(stderr, "Eek... asked to run on no interfaces!\n");
        goto fail;
    }

    if(!have_id && !random_id) {
        /* We use all available interfaces here, since this increases the
           chances of getting a stable router-id in case the set of Babel
           interfaces changes. */
#ifdef BABELD_CODE //+++++ REPLACE +++++
        for(i = 1; i < 256; i++) {
#else // CEFBAELD
        for(i = 1; i < 4096; i++) {
#endif //----- REPLACE -----
            char buf[IF_NAMESIZE], *ifname;
            unsigned char eui[8];
            ifname = if_indextoname(i, buf);
            if(ifname == NULL)
                continue;
            rc = if_eui64(ifname, i, eui);
            if(rc < 0)
                continue;
            memcpy(myid, eui, 8);
            have_id = 1;
            break;
        }
    }

    if(!have_id) {
        if(!random_id)
            fprintf(stderr,
                    "Warning: couldn't find router id -- "
                    "using random value.\n");
        rc = read_random_bytes(myid, 8);
        if(rc < 0) {
            perror("read(random)");
            goto fail;
        }
        /* Clear group and global bits */
        myid[0] &= ~3;
    }

    myseqno = (random() & 0xFFFF);

    fd = open(state_file, O_RDONLY);
    if(fd < 0 && errno != ENOENT)
#ifdef BABELD_CODE //+++++ REPLACE +++++
        perror("open(babel-state)");
#else // CEFBABELD
        perror("open(cefbabel-state)");
#endif //----- REPLACE -----
    rc = unlink(state_file);
    if(fd >= 0 && rc < 0) {
#ifdef BABELD_CODE //+++++ REPLACE +++++
        perror("unlink(babel-state)");
#else // CEFBABELD
        perror("unlink(cefbabel-state)");
#endif //----- REPLACE -----
        /* If we couldn't unlink it, it's probably stale. */
        close(fd);
        fd = -1;
    }
    if(fd >= 0) {
        char buf[100];
        int s;
        rc = read(fd, buf, 99);
        if(rc < 0) {
#ifdef BABELD_CODE //+++++ REPLACE +++++
            perror("read(babel-state)");
#else // CEFBABELD
            perror("read(cefbabel-state)");
#endif //----- REPLACE -----
        } else {
            buf[rc] = '\0';
            rc = sscanf(buf, "%d\n", &s);
            if(rc == 1 && s >= 0 && s <= 0xFFFF) {
                myseqno = seqno_plus(s, 1);
            } else {
#ifdef BABELD_CODE //+++++ REPLACE +++++
                fprintf(stderr, "Couldn't parse babel-state.\n");
#else // CEFBABELD
                fprintf(stderr, "Couldn't parse cefbabel-state.\n");
#endif //----- REPLACE -----
            }
        }
        close(fd);
        fd = -1;
    }

    protocol_socket = babel_socket(protocol_port);
    if(protocol_socket < 0) {
        perror("Couldn't create link local socket");
        goto fail;
    }

    if(local_server_port >= 0) {
        local_server_socket = tcp_server_socket(local_server_port, 1);
        if(local_server_socket < 0) {
            perror("local_server_socket");
            goto fail;
        }
    } else if(local_server_path) {
        local_server_socket = unix_server_socket(local_server_path);
        if(local_server_socket < 0) {
            perror("local_server_socket");
            goto fail;
        }
    }

    init_signals();
    rc = resize_receive_buffer(1500);
    if(rc < 0)
        goto fail;
    if(receive_buffer == NULL)
        goto fail;

    check_interfaces();

#ifdef BABELD_CODE //+++++ REPLACE +++++
    rc = check_xroutes(0);
    if(rc < 0)
        fprintf(stderr, "Warning: couldn't check exported routes.\n");
    rc = check_rules();
    if(rc < 0)
        fprintf(stderr, "Warning: couldn't check rules.\n");
#else // CEFBABELD
    rc = cefore_init (port_num, file_path);
    if (rc < 0) {
        fprintf(stderr, "cefore config error.\n");
        goto fail;
    }
    debugf("cefore_init () ... OK\n");
    
    cefore_socket = cefore_socket_create ();
    if (cefore_socket < 0) {
        fprintf(stderr, "Failed to connect to cefnetd.\n");
    } else {
        debugf("cefore_socket_create (FD=#%d) ... OK\n", cefore_socket);
        rc = cefore_xroute_init ();
        if (rc < 0) {
            fprintf(stderr, "Failed to get xroute from cefnetd.\n");
            fprintf(stderr, "cefbabeld is waiting for cefnetd to start up.\n");
        }
    }
#endif //----- REPLACE -----

    kernel_routes_changed = 0;
    kernel_rules_changed = 0;
    kernel_link_changed = 0;
    kernel_addr_changed = 0;
    kernel_dump_time = now.tv_sec + roughly(30);
    schedule_neighbours_check(5000, 1);
    schedule_interfaces_check(30000, 1);
    expiry_time = now.tv_sec + roughly(30);
    source_expiry_time = now.tv_sec + roughly(300);

    /* Make some noise so that others notice us, and send retractions in
       case we were restarted recently */
    FOR_ALL_INTERFACES(ifp) {
        if(!if_up(ifp))
            continue;
        /* Apply jitter before we send the first message. */
        usleep(roughly(10000));
        gettime(&now);
        send_hello(ifp);
        send_wildcard_retraction(ifp);
        flushupdates(ifp);
        flushbuf(&ifp->buf, ifp);
    }

    FOR_ALL_INTERFACES(ifp) {
        if(!if_up(ifp))
            continue;
        usleep(roughly(10000));
        gettime(&now);
        send_hello(ifp);
        send_wildcard_retraction(ifp);
        send_self_update(ifp);
        send_multicast_request(ifp, NULL, 0, NULL, 0);
        flushupdates(ifp);
        flushbuf(&ifp->buf, ifp);
    }

#ifndef BABELD_CODE //+++++ ADD +++++
    do_debugf(1, "route_ctrl_type=%s\n"
            , route_ctrl_type==ROUTE_CTRL_TYPE_S ? "S": 
              route_ctrl_type==ROUTE_CTRL_TYPE_MS ? "MS" : "MM");
    do_debugf(1, "wired_hello_interval=%d(msec) wireless_hello_interval=%d(msec)\n"
            , default_wired_hello_interval, default_wireless_hello_interval);
#endif //----- ADD -----
    debugf("Entering main loop.\n");

    while(1) {
        struct timeval tv;
        fd_set readfds;
        struct neighbour *neigh;
#ifndef BABELD_CODE //+++++ ADD +++++
        cefbabel_tcp_stat_prcess ();
#endif //----- ADD -----
        
        gettime(&now);

        tv = check_neighbours_timeout;
        timeval_min(&tv, &check_interfaces_timeout);
        timeval_min_sec(&tv, expiry_time);
        timeval_min_sec(&tv, source_expiry_time);
        timeval_min_sec(&tv, kernel_dump_time);
        timeval_min(&tv, &resend_time);
        FOR_ALL_INTERFACES(ifp) {
            if(!if_up(ifp))
                continue;
            timeval_min(&tv, &ifp->buf.timeout);
            timeval_min(&tv, &ifp->hello_timeout);
            timeval_min(&tv, &ifp->update_timeout);
            timeval_min(&tv, &ifp->update_flush_timeout);
        }
        FOR_ALL_NEIGHBOURS(neigh) {
            timeval_min(&tv, &neigh->buf.timeout);
        }
        FD_ZERO(&readfds);
        if(timeval_compare(&tv, &now) > 0) {
            int maxfd = 0;
            timeval_minus(&tv, &tv, &now);
            FD_SET(protocol_socket, &readfds);
            maxfd = MAX(maxfd, protocol_socket);
            if(kernel_socket < 0) kernel_setup_socket(1);
            if(kernel_socket >= 0) {
                FD_SET(kernel_socket, &readfds);
                maxfd = MAX(maxfd, kernel_socket);
            }
            if(local_server_socket >= 0 &&
               num_local_sockets < MAX_LOCAL_SOCKETS) {
                FD_SET(local_server_socket, &readfds);
                maxfd = MAX(maxfd, local_server_socket);
            }
            for(i = 0; i < num_local_sockets; i++) {
                FD_SET(local_sockets[i].fd, &readfds);
                maxfd = MAX(maxfd, local_sockets[i].fd);
            }
#ifndef BABELD_CODE //+++++ ADD +++++
            if (cefore_socket < 0) {
                cefore_socket = cefore_socket_create ();
                if (cefore_socket >= 0) {
                    fprintf (stderr, "cefbabeld detectes that cefnetd is running.\n");
                    cefore_xroute_init ();
                }
            }
            if(cefore_socket >= 0) {
                FD_SET(cefore_socket, &readfds);
                maxfd = MAX(maxfd, cefore_socket);
            }
#endif //----- ADD -----
            rc = select(maxfd + 1, &readfds, NULL, NULL, &tv);
            if(rc < 0) {
                if(errno != EINTR) {
                    perror("select");
                    sleep(1);
                }
                rc = 0;
                FD_ZERO(&readfds);
            }
        }

        gettime(&now);

        if(exiting)
            break;

        if(kernel_socket >= 0 && FD_ISSET(kernel_socket, &readfds)) {
            struct kernel_filter filter = {0};
            filter.route = kernel_route_notify;
            filter.addr = kernel_addr_notify;
            filter.link = kernel_link_notify;
            filter.rule = kernel_rule_notify;
            kernel_callback(&filter);
        }

        if(FD_ISSET(protocol_socket, &readfds)) {
            rc = babel_recv(protocol_socket,
                            receive_buffer, receive_buffer_size,
                            (struct sockaddr*)&sin6, sizeof(sin6));
            if(rc < 0) {
                if(errno != EAGAIN && errno != EINTR) {
                    perror("recv");
                    sleep(1);
                }
            } else {
                FOR_ALL_INTERFACES(ifp) {
                    if(!if_up(ifp))
                        continue;
                    if(ifp->ifindex == sin6.sin6_scope_id) {
                        parse_packet((unsigned char*)&sin6.sin6_addr, ifp,
                                     receive_buffer, rc);
                        VALGRIND_MAKE_MEM_UNDEFINED(receive_buffer,
                                                    receive_buffer_size);
                        break;
                    }
                }
            }
        }
#ifndef BABELD_CODE //+++++ ADD +++++
        if(cefore_socket >= 0 && FD_ISSET(cefore_socket, &readfds)) {
            rc = recv (cefore_socket, cefbuff, 65535, 0);
            if(rc <= 0) {
		        struct pollfd fds[1];
            	int kerrno;
		        kerrno = errno;
		        fds[0].fd = cefore_socket;
		        fds[0].events = POLLIN | POLLERR;
		        poll (fds, 1, 0);
		        if (fds[0].revents & (POLLIN | POLLHUP)) {
			        if((fds[0].revents == POLLIN) && (kerrno == EAGAIN || kerrno == EWOULDBLOCK)) {
				        ; // NOP
			        } else {			
//                        perror("recv");
                        sleep(1);
                        fprintf (stderr, "cefbabeld detectes that cefnetd is not running.\n");
                        fprintf (stderr, "Please check whether cefnetd is running or not?\n");
                        fprintf (stderr, "Exit\n");
                        cefire_socket_close ();
                        break;
			        }
		        }
            } else {
                cefore_xroute_update (cefbuff, rc);
            }
        }
#endif //----- ADD -----
        if(local_server_socket >= 0 && FD_ISSET(local_server_socket, &readfds))
           accept_local_connections();

        i = 0;
        while(i < num_local_sockets) {
            if(FD_ISSET(local_sockets[i].fd, &readfds)) {
                rc = local_read(&local_sockets[i]);
                if(rc <= 0) {
                    if(rc < 0) {
                        if(errno == EINTR || errno == EAGAIN)
                            continue;
                        perror("read(local_socket)");
                    }
                    local_socket_destroy(i);
                }
            }
            i++;
        }

        if(reopening) {
            kernel_dump_time = now.tv_sec;
            check_neighbours_timeout = now;
            expiry_time = now.tv_sec;
            rc = reopen_logfile();
            if(rc < 0) {
                perror("reopen_logfile");
                break;
            }
            reopening = 0;
        }

        if(kernel_link_changed || kernel_addr_changed) {
            check_interfaces();
            kernel_link_changed = 0;
        }

        if(kernel_routes_changed || kernel_addr_changed ||
           kernel_rules_changed || now.tv_sec >= kernel_dump_time) {
#ifdef BABELD_CODE //+++++ DEL +++++
            rc = check_xroutes(1);
            if(rc < 0)
                fprintf(stderr, "Warning: couldn't check exported routes.\n");
            rc = check_rules();
            if(rc < 0)
                fprintf(stderr, "Warning: couldn't check rules.\n");
#endif //----- DEL -----
            kernel_routes_changed = kernel_rules_changed =
                kernel_addr_changed = 0;
            if(kernel_socket >= 0)
                kernel_dump_time = now.tv_sec + roughly(300);
            else
                kernel_dump_time = now.tv_sec + roughly(30);
        }

        if(timeval_compare(&check_neighbours_timeout, &now) < 0) {
            int msecs;
            msecs = check_neighbours();
            /* Multiply by 3/2 to allow neighbours to expire. */
            msecs = MAX(3 * msecs / 2, 10);
            schedule_neighbours_check(msecs, 1);
        }

        if(timeval_compare(&check_interfaces_timeout, &now) < 0) {
            check_interfaces();
            schedule_interfaces_check(30000, 1);
        }

        if(now.tv_sec >= expiry_time) {
            expire_routes();
            expire_resend();
            expiry_time = now.tv_sec + roughly(30);
        }

        if(now.tv_sec >= source_expiry_time) {
            expire_sources();
            source_expiry_time = now.tv_sec + roughly(300);
        }

        FOR_ALL_INTERFACES(ifp) {
            if(!if_up(ifp))
                continue;
            if(timeval_compare(&now, &ifp->hello_timeout) >= 0)
                send_hello(ifp);
            if(timeval_compare(&now, &ifp->update_timeout) >= 0)
                send_update(ifp, 0, NULL, 0, NULL, 0);
            if(timeval_compare(&now, &ifp->update_flush_timeout) >= 0)
                flushupdates(ifp);
        }

        if(resend_time.tv_sec != 0) {
            if(timeval_compare(&now, &resend_time) >= 0)
                do_resend();
        }

        FOR_ALL_INTERFACES(ifp) {
            if(!if_up(ifp))
                continue;
            if(ifp->buf.timeout.tv_sec != 0) {
                if(timeval_compare(&now, &ifp->buf.timeout) >= 0) {
                    flushupdates(ifp);
                    flushbuf(&ifp->buf, ifp);
                }
            }
        }

        FOR_ALL_NEIGHBOURS(neigh) {
            if(neigh->buf.timeout.tv_sec != 0) {
                if(timeval_compare(&now, &neigh->buf.timeout) >= 0) {
                    flushbuf(&neigh->buf, neigh->ifp);
                }
            }
        }

        if(UNLIKELY(debug || dumping)) {
            dump_tables(stdout);
            dumping = 0;
        }
    }

    debugf("Exiting...\n");
    usleep(roughly(10000));
    gettime(&now);

    /* We need to flush so interface_updown won't try to reinstall. */
    flush_all_routes();

    FOR_ALL_INTERFACES(ifp) {
        if(!if_up(ifp))
            continue;
        send_wildcard_retraction(ifp);
        /* Make sure that we expire quickly from our neighbours'
           association caches. */
        send_multicast_hello(ifp, 10, 1);
        flushbuf(&ifp->buf, ifp);
        usleep(roughly(1000));
        gettime(&now);
    }
    FOR_ALL_INTERFACES(ifp) {
        if(!if_up(ifp))
            continue;
        /* Make sure they got it. */
        send_wildcard_retraction(ifp);
        send_multicast_hello(ifp, 1, 1);
        flushbuf(&ifp->buf, ifp);
        usleep(roughly(10000));
        gettime(&now);
        interface_updown(ifp, 0);
    }
    release_tables();
    kernel_setup_socket(0);
    kernel_setup(0);

#ifndef BABELD_CODE //+++++ ADD +++++
    if (cefore_socket > 0) {
        cefire_socket_close ();
    }
#endif //----- ADD -----
    fd = open(state_file, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if(fd < 0) {
#ifdef BABELD_CODE //+++++ REPLACE +++++
        perror("creat(babel-state)");
#else // CEFBABELD
        perror("creat(cefbabel-state)");
#endif //----- REPLACE -----
        unlink(state_file);
    } else {
        char buf[10];
        rc = snprintf(buf, 10, "%d\n", (int)myseqno);
        if(rc < 0 || rc >= 10) {
#ifdef BABELD_CODE //+++++ REPLACE +++++
            fprintf(stderr, "write(babel-state): overflow.\n");
#else // CEFBABELD
            fprintf(stderr, "write(cefbabel-state): overflow.\n");
#endif //----- REPLACE -----
            unlink(state_file);
        } else {
            rc = write(fd, buf, rc);
            if(rc < 0) {
#ifdef BABELD_CODE //+++++ REPLACE +++++
                perror("write(babel-state)");
#else // CEFBABELD
                perror("write(cefbabel-state)");
#endif //----- REPLACE -----
                unlink(state_file);
            }
            fsync(fd);
        }
        close(fd);
    }
    if(local_server_socket >= 0 && local_server_path) {
        unlink(local_server_path);
        free(local_server_path);
    }
    if(pidfile)
        unlink(pidfile);
    debugf("Done.\n");
    return 0;

 usage:
    fprintf(stderr,
            "%s\n"
#ifdef BABELD_CODE //+++++ REPLACE +++++
            "Syntax: babeld "
            "[-V] [-m multicast_address] [-p port] [-S state-file]\n"
            "               "
            "[-h hello] [-H wired_hello] [-z kind[,factor]]\n"
            "               "
            "[-g port] [-G port] [-k metric] [-A metric] [-s] [-l] [-w] [-r]\n"
            "               "
            "[-u] [-t table] [-T table] [-c file] [-C statement]\n"
            "               "
            "[-d level] [-D] [-L logfile] [-I pidfile]\n"
            "               "
            "interface...\n",
            BABELD_VERSION);
#else // CEFBABELD
            "Syntax: cefbabeld "
            "[-x cefnetd.conf-path] [-X cefnetd.conf-port]\n"
            "                  "
            "[-V] [-P route_control_type]\n"
            "                  "
            "[-m multicast_address] [-p port] [-S state-file]\n"
            "                  "
            "[-h hello] [-H wired_hello]  [-M half-time]\n"
            "                  "
            "[-c file] [-C statement]\n"
            "                  "
            "[-d level] [-D] [-L logfile] [-I pidfile]\n"
            "                  "
            "[IP-address%%]interface...\n",
            CEFBABELD_VERSION);
#endif  //----- REPLACE -----

    exit(1);

 fail:
    FOR_ALL_INTERFACES(ifp) {
        if(!if_up(ifp))
            continue;
        interface_updown(ifp, 0);
    }
    kernel_setup_socket(0);
    kernel_setup(0);
 fail_pid:
    if(pidfile)
        unlink(pidfile);
    exit(1);
}

static int
accept_local_connections()
{
    int rc, s;
    struct local_socket *ls;

    if(local_server_socket < 0)
        return 0;

    s = accept(local_server_socket, NULL, NULL);

    if(s < 0) {
        if(errno != EINTR && errno != EAGAIN) {
            perror("accept(local_server_socket)");
            return -1;
        }
        return 0;
    }

    if(num_local_sockets >= MAX_LOCAL_SOCKETS) {
        /* This should never happen, since we don't select for
           the server socket in this case.  But I'm paranoid. */
        fprintf(stderr, "Internal error: too many local sockets.\n");
        close(s);
        return -1;
    }

    rc = fcntl(s, F_GETFL, 0);
    if(rc < 0) {
        fprintf(stderr, "Unable to get flags of local socket.\n");
        close(s);
        return -1;
    }

    rc = fcntl(s, F_SETFL, (rc | O_NONBLOCK));
    if(rc < 0) {
        fprintf(stderr, "Unable to set flags of local socket.\n");
        close(s);
        return -1;
    }

    ls = local_socket_create(s);
    if(ls == NULL) {
        fprintf(stderr, "Unable create local socket.\n");
        close(s);
        return -1;
    }
    local_header(ls);
    return 1;
}

void
schedule_neighbours_check(int msecs, int override)
{
    struct timeval timeout;

    timeval_add_msec(&timeout, &now, roughly(msecs));
    if(override)
        check_neighbours_timeout = timeout;
    else
        timeval_min(&check_neighbours_timeout, &timeout);
}

void
schedule_interfaces_check(int msecs, int override)
{
    struct timeval timeout;

    timeval_add_msec(&timeout, &now, roughly(msecs));
    if(override)
        check_interfaces_timeout = timeout;
    else
        timeval_min(&check_interfaces_timeout, &timeout);
}

int
resize_receive_buffer(int size)
{
    unsigned char *new;

    if(size <= receive_buffer_size)
        return 0;

    new = realloc(receive_buffer, size);
    if(new == NULL) {
        perror("realloc(receive_buffer)");
        return -1;
    }
    receive_buffer = new;
    receive_buffer_size = size;

    return 1;
}

static void
sigexit(int signo)
{
    exiting = 1;
}

static void
sigdump(int signo)
{
    dumping = 1;
}

static void
sigreopening(int signo)
{
    reopening = 1;
}

static void
init_signals(void)
{
    struct sigaction sa;
    sigset_t ss;

    sigemptyset(&ss);
    sa.sa_handler = sigexit;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);

    sigemptyset(&ss);
    sa.sa_handler = sigexit;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGHUP, &sa, NULL);

    sigemptyset(&ss);
    sa.sa_handler = sigexit;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    sigemptyset(&ss);
    sa.sa_handler = SIG_IGN;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    sigemptyset(&ss);
    sa.sa_handler = sigdump;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    sigemptyset(&ss);
    sa.sa_handler = sigreopening;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, NULL);

#ifdef SIGINFO
    sigemptyset(&ss);
    sa.sa_handler = sigdump;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    sigaction(SIGINFO, &sa, NULL);
#endif
}

static void
dump_route(FILE *out, struct babel_route *route)
{
    const unsigned char *nexthop =
        memcmp(route->nexthop, route->neigh->address, 16) == 0 ?
        NULL : route->nexthop;
    char channels[100];

    if(route->channels_len == 0) {
        channels[0] = '\0';
    } else {
        int k, j = 0;
        snprintf(channels, 100, " chan (");
        j = strlen(channels);
        for(k = 0; k < route->channels_len; k++) {
            if(k > 0)
                channels[j++] = ',';
            snprintf(channels + j, 100 - j, "%u", (unsigned)route->channels[k]);
            j = strlen(channels);
        }
        snprintf(channels + j, 100 - j, ")");
    }

    fprintf(out, "%s%s%s metric %d (%d) refmetric %d id %s "
            "seqno %d%s age %d via %s neigh %s%s%s%s\n",
#ifdef BABELD_CODE //+++++ REPLACE +++++
            format_prefix(route->src->prefix, route->src->plen),
#else // CEFBABELD
            format_cefore_prefix(route->src->prefix, route->src->plen),
#endif //----- REPLACE -----
            route->src->src_plen > 0 ? " from " : "",
            route->src->src_plen > 0 ?
            format_prefix(route->src->src_prefix, route->src->src_plen) : "",
            route_metric(route), route_smoothed_metric(route), route->refmetric,
            format_eui64(route->src->id),
            (int)route->seqno,
            channels,
            (int)(now.tv_sec - route->time),
            route->neigh->ifp->name,
            format_address(route->neigh->address),
            nexthop ? " nexthop " : "",
            nexthop ? format_address(nexthop) : "",
            route->installed ? " (installed)" :
            route_feasible(route) ? " (feasible)" : "");
}

static void
dump_xroute(FILE *out, struct xroute *xroute)
{
    fprintf(out, "%s%s%s metric %d (exported)\n",
#ifdef BABELD_CODE //+++++ REPLACE +++++
            format_prefix(xroute->prefix, xroute->plen),
#else // CEFBABELD
            format_cefore_prefix(xroute->prefix, xroute->plen),
#endif //----- REPLACE -----
            xroute->src_plen > 0 ? " from " : "",
            xroute->src_plen > 0 ?
            format_prefix(xroute->src_prefix, xroute->src_plen) : "",
            xroute->metric);
}

static void
dump_tables(FILE *out)
{
    struct neighbour *neigh;
    struct xroute_stream *xroutes;
    struct route_stream *routes;

    fprintf(out, "\n");

    fprintf(out, "My id %s seqno %d\n", format_eui64(myid), myseqno);

#ifndef BABELD_CODE //+++++ ADD +++++   
    fprintf(out, "----- neighbours -----\n");
#endif //----- ADD -----
    FOR_ALL_NEIGHBOURS(neigh) {
        fprintf(out, "Neighbour %s dev %s reach %04x ureach %04x "
                "rxcost %u txcost %d rtt %s rttcost %u chan %d%s.\n",
                format_address(neigh->address),
                neigh->ifp->name,
                neigh->hello.reach,
                neigh->uhello.reach,
                neighbour_rxcost(neigh),
                neigh->txcost,
                format_thousands(neigh->rtt),
                neighbour_rttcost(neigh),
                neigh->ifp->channel,
                if_up(neigh->ifp) ? "" : " (down)");
    }

#ifndef BABELD_CODE //+++++ ADD +++++   
    fprintf(out, "----- xroutes -----\n");
#endif //----- ADD -----
    xroutes = xroute_stream();
    if(xroutes) {
        while(1) {
            struct xroute *xroute = xroute_stream_next(xroutes);
            if(xroute == NULL) break;
            dump_xroute(out, xroute);
        }
        xroute_stream_done(xroutes);
    }

#ifndef BABELD_CODE //+++++ ADD +++++   
    fprintf(out, "----- routes -----\n");
#endif //----- ADD -----
    routes = route_stream(ROUTE_ALL); 
    if(routes) {
        while(1) {
            struct babel_route *route = route_stream_next(routes);
            if(route == NULL) break;
            dump_route(out, route);
        }
        route_stream_done(routes);
    }
#ifndef BABELD_CODE //+++++ ADD for MP +++++
    if (route_ctrl_type == ROUTE_CTRL_TYPE_MS) {
        dump_source(out);
    } else if (route_ctrl_type == ROUTE_CTRL_TYPE_MM) {
        dump_best_route(out);
        dump_source(out);
    }
#endif //----- ADD for MP -----
    
    fflush(out);
}

int
reopen_logfile()
{
    int lfd, rc;

    if(logfile == NULL)
        return 0;

    lfd = open(logfile, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if(lfd < 0)
        return -1;

    fflush(stdout);
    fflush(stderr);

    rc = dup2(lfd, 1);
    if(rc < 0)
        return -1;

    rc = dup2(lfd, 2);
    if(rc < 0)
        return -1;

    if(lfd > 2)
        close(lfd);

    return 1;
}

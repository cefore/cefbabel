#
# Copyright (c) 2016-2025, National Institute of Information and Communications
# Technology (NICT). All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the NICT nor the names of its contributors may be
#    used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE NICT AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE NICT OR CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

PREFIX = /usr/local

CDEBUGFLAGS = -Os -g -Wall

DEFINES = $(PLATFORM_DEFINES)

CFLAGS = $(CDEBUGFLAGS) $(DEFINES) $(EXTRA_DEFINES)

LDLIBS = -lrt

SRCS = babeld.c net.c kernel.c util.c interface.c source.c neighbour.c \
       route.c xroute.c message.c resend.c configuration.c local.c \
       disambiguation.c rule.c cefore.c cefversion.h

OBJS = babeld.o net.o kernel.o util.o interface.o source.o neighbour.o \
       route.o xroute.o message.o resend.o configuration.o local.o \
       disambiguation.o rule.o cefore.o 

all: cefbabeld cefbabelstatus

cefbabeld: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o cefbabeld $(OBJS) $(LDLIBS)

cefbabelstatus: cefbabelstatus.o 
	$(CC) $(CFLAGS) $(LDFLAGS) -o cefbabelstatus cefbabelstatus.o $(LDLIBS)

babeld.o: babeld.c cefversion.h

cefbabelstatus.o: cefbabelstatus.c cefversion.h

local.o: local.c cefversion.h

kernel.o: kernel_netlink.c kernel_socket.c

.PHONY: all install uninstall clean


install: cefbabeld cefbabelstatus all
	-rm -f $(TARGET)$(PREFIX)/bin/cefbabeld
	-rm -f $(TARGET)$(PREFIX)/bin/cefbabelstatus
	mkdir -p $(TARGET)$(PREFIX)/bin
	cp -f cefbabeld $(TARGET)$(PREFIX)/bin
	cp -f cefbabelstatus $(TARGET)$(PREFIX)/bin


uninstall:
	-rm -f $(TARGET)$(PREFIX)/bin/cefbabeld
	-rm -f $(TARGET)$(PREFIX)/bin/cefbabelstatus

clean:
	-rm -f cefbabeld  cefbabelstatus *.o *~ core TAGS gmon.out

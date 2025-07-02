
# Cefbabel

## 1. Overview
Cefbabel is a routing daemon for Cefore, which is compliant with a CCNx version 1.0 protocol specified by IETF/IRTF RFCs 8569 and 8609. Cefbabel is based on a Babeld[^1] protocol which is a distance-vector routing algorithm over IP. Cefbabel builds the shortest-path in a Cefore network by inserting FIB entries into cefnetd, a forwarding daemon of Cefore, running on the path.

[^1]: Babeld reference https://www.irif.fr/~jch/software/babel/  

## 2. Installation

### Get the source file
```console
$ git clone https://github.com/cefore/cefbalel.git
```
### Build and install
```console
$ cd cefbabel
$ make
$ sudo make install
```

## 3. Getting started

### Start Cefbabel

Start Cefbabel on each node running Cefore(cefnetd) by specifying the set of interfaces used by Cefore. Before starting ```cefbabeld```, please confirm that ```cefnetd``` is already up.
> [NOTE] If you want to know how to run Cefore daemon (cefnetd), see Cefore's [README](https://github.com/cefore).  

```console
$ cefbabeld interface... [options]
```
> [NOTE] Option list is [here](#run-options)

If a node runnnig Cefore has multiple interfaces that you want to connect to the CCNx network, just list up all of them. For example, 
```console
$ cefbabeld enp0s3 enp0s8 ...
```

### Route advertisement
#### At source (publisher) node
Please insert static FIB to trigger advertisement of route information by Cefbabel.

For example, add route information (FIB entry) to Cefore for a content whose name prefix is “ccnx:/sample/content”.
```console
 $ cefroute add ccnx:/sample/content udp 192.168.1.10
```
#### Router nodes
Check whether the advertised route information is successfully created in FIB of another node, which is connected to the publisher node.
```console
$ cefstatus
CCNx Version     : 1
Port             : 9896
Rx Interest      : 0 (RGL[0], SYM[0], SEL[0])
Tx Interest      : 0 (RGL[0], SYM[0], SEL[0])
Rx ContentObject : 0
Tx ContentObject : 0
Cache Mode       : None
FWD Strategy     : None
Interest Return  : Disabled
Faces : 7
  faceid =   4 : IPv4 Listen face (udp)
  faceid =   0 : Local face
  faceid =   5 : IPv6 Listen face (udp)
  faceid =   6 : IPv4 Listen face (tcp)
  faceid =   7 : IPv6 Listen face (tcp)
  faceid =  35 : address = 192.168.1.10:9896 (udp)
  faceid =  36 : Local face
  faceid =   8 : Local face (for cefbabeld)
FIB(App) :
  Entry is empty
FIB : 1
  ccnx:/sample/content
    Faces : 35 (--d) RtCost=1
PIT(App) :
  Entry is empty
PIT :
  Entry is empty
```
Please confirm that the registered FIB "ccnx:/sample/content" is displayed.

### Run Options

| option | description                                         |
| ------ | --------------------------------------------------- |
| -D     | deamonaize                                          |
| -H NUM | specify hello interval (default: 4s)                |
| -V     | show the Cefbabel version                           |
| -X NUM | specify port number to be used for connecting Cefore(cefnetd) (default: 9896)   |
| -d NUM | specify debug level for output (1:info 2:detail)    |
| -p NUM | specify port number of Cefbabel (default: 9897)     |

> [NOTE]  The above options are commonly used options.

## 4. Licence
Copyright (c) 2016-2025, National Institute of Information and Communications  
Technology (NICT). All rights reserved.

Redistribution and use in source and binary forms, with or without  
modification, are permitted provided that the following conditions are  
met:  
1. Redistributions of source code must retain the above copyright notice,  
   this list of conditions and the following disclaimer.  
2. Redistributions in binary form must reproduce the above copyright  
   notice this list of conditions and the following disclaimer in the  
   documentation and/or other materials provided with the distribution.  
3. Neither the name of the NICT nor the names of its contributors may be  
   used to endorse or promote products derived from this software  
   without specific prior written permission.  

THIS SOFTWARE IS PROVIDED BY THE NICT AND CONTRIBUTORS "AS IS" AND ANY  
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED  
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE  
DISCLAIMED. IN NO EVENT SHALL THE NICT OR CONTRIBUTORS BE LIABLE FOR ANY  
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL  
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS  
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)  
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT  
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY  
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF  
SUCH DAMAGE.  


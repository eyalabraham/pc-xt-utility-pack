# Utility software pack for PC/XT
This is a collection of DOS programs written for DOS 3.31 running on [my PC-XT](https://sites.google.com/site/eyalabraham/pc-xt).
The programs compile with [Open Watcom C16 Optimizing Compiler](http://www.openwatcom.org/).
Programs were tested on a DOS 3.31 VM on VMware player ver 15.
All IP network utilities use my light weight [IP stack](https://github.com/eyalabraham/8bit-TCPIP) and a SLIP connection through the extra serial channel of the Z80-SIO/2.

## Fixed Disk test utility
Simple utility that tests the existence of fixed disks on the system and outputs various system data points about the device. It checks the fixed disk count, attempts to read partition table and boot sector of each partition.
I used this utility to check my BIOS compatibility with DOS.

## XODEM upload and download utility
XMODEM upload and download that uses the COM1 serial port. The program was written to use the INT 14 serial communication BIOS calls and should be portable to other PC machines.
Due to BIOS polling mechanism this works well with low BAUD rates of 1200 or below.

## Graphics demo
Simple Mandelbrot fractal drawing program. The program was written to use INT 10 graphics BIOS calls and should be portable to PC machines.

## PING client
Network PING utility with similar feature to Linux PING.
```
ping  [-c count] [-i interval] destination_ip_address
```

## NTP client
An NTP client for displaying, and optional update of system clock.
NTP server IP address is defined with the DOS environment variable NTP in the autoexec.bat file.
Time zone information is defined with the DOS environment variable TZ in the autoexec.bat file. The default time zone if TZ in not defined will be US eastern standard time.
```
ntp [-u]
```

## TELNET client
A simple telnet client written in C. The client will remain in NVT mode unless the server attempts to negotiate options. In that case the client will respond with WONT to any DO coming from the server except for window size and echo options, and will encourage the server to DO echo and suppress go ahead. When negotiating window (screen) size setting, the client advertises 24 rows x 80 columns.
Some TELNET servers use VT100 codes that are not processed on this client. Possible enhancement would be to add VT100 code processing.
```
telnet ipv4_address [port]
```

# TED a Tiny EDitor
This is a small full-screen editor for PC-XT type systems. There are many such editors available for download but all of them use some form of direct video-memory access for performance. [My PC-XT project](https://sites.google.com/site/eyalabraham/pc-xt) does not use a conventional video card, and direct memory access is not an option.  
This editor implementation only uses the INT 10 BIOS calls, and provides the functionality of a simple full-screen plain-text editor. The editor source code can be found in [this separate repository]().

## FTP client
tbd

## HTTP server
tbd

## Embedded Python interpreter and REPL
tbd

## SLIP interface setup

Check that the SLIP kernel module is loaded: 
```
$ lsmod | grep slip
```
    
If not loaded add 'slip' to /etc/modules or manually load with:
```
$ sudo modprobe slip
```
    
Create a SLIP connection between serial port and sl0 with 'slattach':
```
$ sudo slattach -d -L -p slip -s 9600 /dev/ttyS4
$ sudo ifconfig sl0 mtu 1500 up
```

Sometimes needs:
```
$ tput reset > /dev/ttyS4
```
    
Check sl0 with: 
```
$ ifconfig sl0
```
    
Enable packet forwarding:
```
$ sudo sysctl -w net.ipv4.ip_forward=1
```
    
Setup routing to sl0 for 10.0.0.19 (this is the device's IP):
```
$ sudo ip route add to 10.0.0.19 dev sl0 scope host proto static
```
    
Setup proxy ARP for 10.0.0.19 on enp4s0:
```
$ sudo arp -v -i enp4s0 -H ether -Ds 10.0.0.19 enp4s0 pub
```

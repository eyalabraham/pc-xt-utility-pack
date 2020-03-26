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
NTP server IP address is defined with the DOS environment variable NTP in the AUTOEXEC.BAT file.
Time zone information is defined with the DOS environment variable TZ in the AUTOEXEC.BAT file. The default time zone if TZ in not defined will be US eastern standard time.

```
ntp [-u]
```

## TELNET client
A simple telnet client written in C. The client will remain in NVT mode unless the server attempts to negotiate options. In that case the client will respond with WONT to any DO coming from the server except for window size and echo options, and will encourage the server to DO echo and suppress go ahead. When negotiating window (screen) size setting, the client advertises 24 rows x 80 columns.
Some TELNET servers use VT100 codes that are not processed on this client. Possible enhancement would be to add VT100 code processing. However, a much simpler solution was to install ANSI.SYS driver in CONFIG.SYS, which I needed anyway for the VDE text editor.

```
telnet ipv4_address [port]
```

## VDE full screen text editor
This is a small full-screen editor for PC-XT type systems. There are many such editors available for download but all of them use some form of direct video-memory access for performance. [My PC-XT project](https://sites.google.com/site/eyalabraham/pc-xt) does not use a conventional video card, and direct memory access is not an option.  
This editor relies on the ANSI.SYS driver to send commands to the screen as ANSI escape sequences. This implementation only uses the INT 10 BIOS calls, and provides the functionality of a full-screen plain-text editor. The VDE editor can be downloaded from the [VDE editor website](https://sites.google.com/site/vdeeditor/Home/vde-files). The version to get is:

```
VDE 1.65C     vde165c.zip   150 KB  February 1993
```
In order to use the editor, add ANSI.SYS as a DEVICE in the CONFIG.SYS DOS setup file. Theb run the ```vinst.com``` executable and configure the editor as a generic terminal: 'I'nstallation -> 'E'dit -> '0' Generic terminal ANSI.SYS -> 'W'rite -> 'S'ave).

## HOST, name server lookup
This is a simple utility for performing DNS lookups. Similar to the Linux/UNIX command it is  used to convert names to IP addresses and vice versa. __name__ is the domain name that is to be looked up. It can also be a dotted-decimal IPv4 address, in which case **host** will perform a reverse lookup for that address. __server__ is an optional argument which is either the name or IP address of the name server that **host** should query instead of the server provided with environment variable DNS. By default, **host** uses UDP when making queries.

```
host [-V | -h] [-R <retry>] [-s <name-server>] [-t <type>] {name}
```

## TFTP client
**tftp** is a client for the Trivial file Transfer Protocol, which can be used to transfer files to and from remote machines, including some very minimalistic, usually embedded, systems. This tftp client has no interactive mode, it is limited to command line only.

> tftpd set with --retransmit timeout of 2sec

```
tftp [-V | -h ] [-m <mode>] -g | -p  <file> <host>

-V          version info
-h          help
-g          "get" command
-p          "put" command
<file>      file name to send or receive
<host>      remote host IPv4 address
```

## Sudoku solver

Based on Professor Thorsten Altenkirch's video on a [recursive Sudoku solver](https://www.youtube.com/watch?v=G_UYXzGuqvM) found in the Computerphile channel. This is a brute force recursive algorithm with a 'back tracking' method. The program relies on the functionality of ANSI.SYS for screen presentation.

## HTTP server
tbd

## Embedded Python interpreter and REPL
tbd

## Environment variable

All network utilities require the setting of DOS environment variables: NETMASK, GATEWAY, and LOCALHOST. See DOS 3.31 manual on how to use SET command in autoexec.bat 

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

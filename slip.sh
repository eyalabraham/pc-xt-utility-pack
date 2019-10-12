#!/bin/bash

#sudo slattach -d -L -p slip -s 4800 /dev/ttyS4 &

sudo ifconfig sl0 mtu 1500 up
ifconfig sl0
sudo sysctl -w net.ipv4.ip_forward=1
sudo ip route add to 10.0.0.19 dev sl0 scope host proto static
sudo arp -v -i enp4s0 -H ether -Ds 10.0.0.19 enp4s0 pub

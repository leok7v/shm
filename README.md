# mps
simple client server tool wich uses memory socket and send() recv() 1 byte 
and 4KiB shared memory buffer written by server and read by client per roundtrip
measuring number of messages (roundtrips) per second.

to start:
shm server &
shm client

OSX i7 MacBookPro9,1 Intel Core i7 2.3 GHz measures at ~
roundtrips per second=16,000+
adding 2ms delay to client drops messages to 300+ with ~7% CPU utilization

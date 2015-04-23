#CSc 425 -- Project 3 Guide

[TOC]

##General Idea
Our goal is to be able to resume the telnet session after the address change, and do so without making changes to the `telnet` program or the TCP implementation in the OS kernel.
 
The idea is to write two proxy programs: `cproxy` running on the Client, and `sproxy` running on the Server. The two customized proxy programs detect and handle address changes, while the `telnet` program only communicates with local proxy program without being affected by any address change.

###Basic outline from spec
1. The `sproxy` should listen to TCP port 6200 on the Server, and takes no command-line argument. The `cproxy` should listen to TCP port 5200 on the Client, and take one argument that is the eth1 IP address of the Server, i.e., `cproxy w.x.y.z`

2. Instead of telnet into the Server directly, on the Client you telnet into cproxy:
`telnet localhost 5200`

  After accepting a connection from local telnet, `cproxy` should make a TCP connection to Server port 6200, i.e., connect to `sproxy`. Upon accepting this connection, `sproxy` should make a TCP connection to localhost (127.0.0.1) port 23, which is what the telnet daemon is listening on.

3. Now the `cproxy` has two connections and so does the `sproxy`. Their job is to relay packets between the two sockets. For example, `cproxy` calls `select( )` on both of its sockets, and whenever here is data to read from one socket, it will read the data and write it to the other socket. This way any user input to the telnet terminal will be passed by `cproxy` to `sproxy`, which then passes it to the telnet daemon on Server. Similarly data sent by the telnet daemon will be passed by `sproxy` to `cproxy,` which then passes it to the telnet program on the Client.

 This replaces the single direct TCP connection between telnet and telnet daemon by 3 separate TCP connections stitched together by the proxy programs. Run the ping program or any command in the telnet session and it should work just as fine as the single TCP connection. In this way the telnet and telnet daemon connect to programs on the localhost, whose address (127.0.0.1) never changes. Thus we mask the issue of address changes from the applications, and only need to develop a solution between `cproxy` and `sproxy`.

###Extra Credit (+10%)
From the spec:

>There is 10% extra credit for supporting multiple concurrent telnet sessions. You can achieve this by different means. One approach is to use one worker thread per session, and let the master thread accept new connections, spawning new threads for brand new sessions or passing the connected socket to an existing thread to resume communication. Another approach is to stick to a single thread, but use `select()` to multiplex both accepting new connections and reading data from sockets. This may need to set non-blocking mode for the `accept()` call

###Proxy Protocol
 We need a queue to remember all of the messages that the other process hasn't `ACK`'ed yet. This is just in case the session was ended for some reason, we will be able to re-send them on the re-connect.

 We will also need to have a packet header that will contain fields that signal some info to the process that receives it. It would be best if all packets had a sequence number, so that they could be sorted correctly in the queue (and resent in their correct orders). The packet should differentiate data packets, a new connection, a re-connection, a heartbeat, or an `ACK` for a previously received packet based on it's sequence number (or, like in TCP, the `ACK` could represent having received all packets up to that sequence #). 

On a failed connection, it is the client that should attempt to reconnect (from point 2 on page 4 of the spec).

On a brand new telnet session, `sproxy` should close the existing connection with the telnet deamon, and start a new connection with it for this new telnet session. On a reconnect, it should keep using the existing connection to resume the hanging session.

###Heartbeat
From the spec:
> `cproxy` exchanges periodic heartbeat messages with `sproxy` to detect the loss of connection. At every 1s `cproxy` and `sproxy` should send to each other a heartbeat message. Both proxy programs treat the missing of three consecutive heartbeats as the loss of connection. They will then close the failed socket. You can use `select()` to implement the 1s timeout. 

Since we need a custom header for our protocol, we should have a field that specifies if it is a heartbeat or not. If a packet is sent out as a heartbeat, it should have no payload (this just makes things simpler). 

####Select()
 `select()` watches opened sockets to see if they are ready to be read from. It has a timeout argument that can be used to make it return if nothing happens in that time period, *on either socket*. I spoke to the professor during office hours and he explained the possibilities to me as such:

 - `select()` returns after something was written into the socket from the local telnet service. This has nothing to do with the link between the two proxies though, so simply add the message to the queue and attempt to send it to the other proxy. 
 - `select()` returns after something was written into the socket from the other proxy. When we read this message (in order to pass it on to the telnet) we can treat it as if it were a heartbeat message. Because data came in on the link, we know that the link is up.
 - `select()` returns after the timeout value, meaning that nothing was read from either of the other ends. This could simply be a fluke, or that there was no data to receive, but either way, we don't know if the link between the proxies is still up. So then we need to send a heartbeat message to the other proxy.

The professor wanted to be very clear about the fact that any data received over the proxy link means the link is still up (or that it *was* still up anyway). This implies that we don't necessarily need to send out a heartbeat exactly every 1 second, but only when 1s has gone by with no data being received from the other proxy.

Using `select()` in this way, we can simply keep track of the last time that we received a message (or a heartbeat packet) from the other proxy by storing `gettimeofday()` in a `long`. If the value is ever greater than 3 seconds, we can assume the link is down. 

##Implementation
###Sever
 The server should consist essentially of a `while(true)` loop that continually accepts new connections (via TCP) from any IP address at port 6200 (from point 2). This socket should be reuseable, passive (i.e. call `listen()` function) and it's connections should be reusable, as well as defaulted to use `SO_KEEPALIVE`? 
 
This socket should be shut when
  1. `select()` fails after 3 seconds (i.e. the socket isn't ready). ***NOTE:*** this assumes that it is acceptable for only the client to initiate heartbeats. I *think* this *should* be fine because as long as the link is still up, the server will receive the heartbeat from the client. It would then reply with a heartbeat message, and the client would know that the link is still up. It also would reduce some of the complexity on the server side. 
  2. read returned 0 (reached the end-of-file... i.e. the socket is invalid)
  3. we receive a `proxy-close` packet from the client.


####Reading from the client
Use `select()` to determine if there was a 3 second timeout. If so, close the socket.
Use `read()` or `recv()` to get a new packet from the client. Close the sockets if we read length 0 , exit if we got -1.

Interpret the flags of the `proxy header`.
	on connect, start a telnet session
	on close, close all sockets & detatch threads & return
	on heartbeat, reply with a similar heartbeat.
	on ACK, clear all ack'ed messages in the cache until (and including) the current sequence number
	and finally, on a simple data packet, just go ahead and forward to the localhost telnet.

Once we've read, we should go ahead and let the client know that we've ACK'd it.
	
###Client
Client should connect to local telnet and read from it.

 - If the read was empty, the session was ended.
 - Otherwise, add the packet from the telnet session into a queue
  - Send the telnet session's packet to the server 
 - Connect to the server, with a `proxy-connect` packet.
  - Then start reading from the server
     - On an ACK, go ahead and remove all of our ACK'd packets
     - On data send to the telnet server
     - On heartbeat, keep track that we received it
     - 
###Threading
Note that this is not necessarily the exact ordering things should go in. While the client should definitely attempt to connect to telnet before it connects to the server (as telnet starts at the client), we can't send info from the telnet session to the server without having connected to the `sproxy` first.

I think that perhaps these should be run as multiple threads. For example, everything that involves relaying from telnet to `sproxy` should be done in one thread, while everything that involves relaying the opposite way should be done in a second thread.
		
###Telnet
This stuff is the underlining of both programs. It would be practically identical in each program. 

####Connecting to local telnet server
Use tcp to connect to localhost:23 (which is the port number for the telnet server) using a new socket
####Reading from local telnet server
select() to timeout after 1 second
read from the telnet socket and exit if failure. 
Add the packet from the telnet server to our queue. 
Then send the packet out over the other socket (socket to client or to server)

####Seding to the local telnet server
This is the simpler case. Since we've received from the other process, and we are only sending to the localhost (i.e. the same machine) there is no need to do anything more than send it out over the telnet socket. (the packet should have been ack'd already).

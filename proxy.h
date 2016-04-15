#ifndef PROXY_H
#define PROXY_H

#define CLIENT_PORT 5200
#define SERVER_PORT 6200

#define LOCALHOST "127.0.0.1"

// 2^16
#define MAX_CHARS 65536 

enum PROXY_Flags{
	//If the server receives this it should first send and ACK back to 
	// the client. Then, it should forward *only the payload* to the 
	// telnet daemon.
    //If the client receives this, same thing.
    //This flag should be added onto every packet that is originating from
    // either of the proxy's telnet sockets.
    PROXY_DATA = 0,
    
    //If the server receives this, it should close any existing connection
    // to the telnet daemon
    //The Client shouldn't receive this.
    PROXY_NEW_CONNECT,
    
    //If the server receives this, it should simply resend all of the 
    //  data that is in it's queue
    //The client shouldn't receive this
    PROXY_RECONNECT,
    
    //If the server receives this, it should close both of it's sockets.
    //  this will make it go back into the stage of waiting for a new
    //  client to connect
    //The Client shoudln't receive this. The client should send this one
    //  whenever the telnet socket closes or there is nothing left to read
    PROXY_CLOSE,
    
    //If the server receives this, it is simply a notification that the
    //  link is still working, but the client didn't have anything to send
    //If the client receives this, it should immediately respond with it's
    //  own packet that has this as it's flag.
    PROXY_HEARTBEAT,
    
    //If the server or client receives this, they should clean out all of
    //the packets that are being stored in the queue and have a number less
    // than or equal to the seq_num declared in the same header.
    //      These sockets are using TCP, so we know they will arrive in the
    //      correct order, so we don't need to worry about sliding windows
    //      or any of that complicated bullshit
    //Whoever is sending this, should have their process go like so:
    //
    //  receive a packet with a sequence# n
    //  forward the payload to the telnet socket
    //  reply to the other proxy with a PROXY_ACK flag and a sequence# n
    PROXY_ACK
};

struct Proxy_Header{
    uint32_t seq_num;   //4 bytes
    uint32_t ip;        //4 bytes   I mean for this to be utilized in the
                                // identification of which link packets
                                // are coming on on, etc. But I am not
                                // so sure it will be useful...
    uint32_t len;       //4 bytes (32 bits)
    char flag;          //1 byte (8 bits)
} __attribute__((packed));  //The size of this struct should be 13 bytes

/* Similar to the router project, we need to be able to store
 * the payload that are being sent to the other proxy.
 * Because we are going to be using a queue, I want to have
 * each of the messages basically point to the others
 */

#endif

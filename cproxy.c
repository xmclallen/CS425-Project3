#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>

#include "proxy.h"
#include "queue.h"

char* serverIP;

int telnet_socket = 0;
int server_socket = 0;

pthread_t server_thr;
pthread_t telnet_thr;


void sproxy_sendflags(enum PROXY_Flags flag, int seqNum);
void reconnect();

void send_to_telnet(char* buffer, int length){
    printf("\tSending %d bytes to TELNET\n", length);
    fflush(stdout);
    int write_len =  write(telnet_socket, buffer, length);
    printf("\tFinished send...\n");
    if(write_len <= 0){
        perror("\tFailed to write");
        exit(0);
    }
    printf("\tExiting method.\n");
    fflush(stdout);
}

void* sproxy_read(){
    char *buffer;

    /* Read into buffer, send to telnet */
    while(1){
	
	
	/* This entire block of code within this indentation
	 * was taken from sproxy.c
	 * 
	 * I am not sure if I want to use it though.. 
	 * 
	 * If the proxy doesn't receive any data in x seconds, it should
	 * immediately know that the link is down/hanging/etc. 
	 * 
	 * Then it would be able to try to sleep for a few (10-20) seconds 
	 * before attempting to re-connect.
	 * 
		// Use select() to multiplex reads 
*/

        //create a file descriptor set
        fd_set set;
        FD_ZERO(&set);
        FD_SET(server_socket, &set);

        //create a timeout struct (this must be done everytime, as 
        // select *will* modify the values)
        struct timeval timer;
        timer.tv_sec = 3;
        timer.tv_usec = 0;
        
        printf("Selecting on the server socket");        
        int select_err = select(FD_SETSIZE, &set, NULL, NULL, &timer);
        if(select_err <0){
            perror("Failed on select()");
        } //See if it hit the 1s timout.
        else if (select_err == 0){
			fprintf(stderr, "Connection timeout\n");
			reconnect();
		}
	
	    	
		printf("Now reading from the server socket\n");
        //Allocate a buffer, set all bytes to 0
        buffer = calloc( MAX_CHARS, sizeof(char));

        int read_len = read(server_socket, buffer, MAX_CHARS);
        
        if(read_len == 0){
            printf("\tConnection closed by SProxy.\n");
            free(buffer);
            
            //Instead of return NULL, I want this thread to exit, 
            // rather than attempting to to go on and do other things
			pthread_exit(0);
			return NULL; // just in case
        }
        else if(read_len<0){
            perror("Failed to read from SProxy");
            exit(1);
        }else{

            printf("Read %d bytes from SProxy: %.*s", read_len, read_len, buffer);
            /* Would need to differentiate from ACK and data */
            
            //get a header, to do ^
            struct Proxy_Header *head = (struct Proxy_Header*) buffer;
            //get the payload
            char* payload = (char*)(buffer + sizeof(struct Proxy_Header));
            
            int flag = head->flag;
        
        //I know this looks horrible, but it's better than having every
        // single thing take up like a million lines. 
//while(read_len > 0){
            if(flag == PROXY_NEW_CONNECT){
				//do nothing. We shouldn't be receiving this
			}
			else if(flag == PROXY_HEARTBEAT){
				//Send out a heartbeat in response
				sproxy_sendflags(PROXY_HEARTBEAT, 0);
			}
			else if(flag == PROXY_ACK){
				//clear out the queue of anything with a smaller seq_num
				
			}
			//not even sure if there is a time to send this one...
			else if(flag == PROXY_CLOSE){
				//Do nothing, I shouldn't be getting this flag
			}
			else if(flag == PROXY_DATA){
				//for now, simply forward it off to telnet
				send_to_telnet(payload, head->len);
				
				//later, send back an ACK after forwarding to the telnet			
			}
			else{
				printf("\tReceived an invalid flag from cproxy...dropping\n");
			}
		}//end else
	}
	
	
	return NULL; // probably dead code, but i am paranoid
}


/* TODO:
 * 
 *  This function needs to add in the correct header for a data message.
 *  It should be called with a seqNum arg. That arg should be the value
 *   that was returned when this packet was added to the queue.
 */ 

void send_to_sproxy(char* buffer, int len , int seqNum){
    
    
    size_t size = sizeof(struct Proxy_Header) + len;

    //create a new buffer of bytes to send
    char *to_send = malloc(size);

    /* create the header */
    struct Proxy_Header *head = (struct Proxy_Header*)to_send;
    head->flag = PROXY_DATA;
    head->seq_num = seqNum;
    head->ip = 0;
    head->len = len;

	//copy the buffer into the payload section

	for (int i = 0; i < len * sizeof(char); i++){
        to_send[sizeof(struct Proxy_Header) + i] = buffer[i];
	}

    printf("\tSending packet %d to Server Proxy\n", seqNum);

    int write_len = write(server_socket, to_send, size);
    if(write_len <= 0){
        printf("Failed to send data to the server\n");
    }
    free(to_send); //complains
    printf("\tSend to Server Proxy successful\n");
}

/* This function is to be called whenever we need to JUST send a flag
 * to the server proxy  */
void sproxy_sendflags(enum PROXY_Flags flag, int seqNum) {
    ssize_t size = sizeof(struct Proxy_Header);

    char *tosend = malloc(size);

    struct Proxy_Header *connect = (struct Proxy_Header *)tosend;
    connect->flag = flag;
    connect->seq_num = seqNum;
    connect->ip = 0;  //get_ip();
    connect->len = 0;

    printf("~> Sending a flag to SPROXY\n");
    ssize_t write_len = write(server_socket, tosend, size);
    if(write_len <= 0){
        perror("Failed to send a flag to the server\n");
    }
    printf("~> Sent flag %d (size: %ld) to SPROXY\n", flag, write_len);
    free(tosend);
}

void *telnet_read(){
	char *buffer;
    /* Read data into the buffer */
    while (1) {
        buffer = calloc(MAX_CHARS, sizeof(char));

        printf("-> Waiting for TELNET\n");
        int read_len = read(telnet_socket, buffer, MAX_CHARS);
        printf("~> Read from TELNET\n");

        if(read_len == 0) {
            printf("Telnet session closed\n");
            sproxy_sendflags(PROXY_CLOSE, 0);
            close(telnet_socket);
            close(server_socket);
            telnet_socket = 0;
            server_socket = 0;
            free(buffer);
            return NULL;
        } else if(read_len < 0) {
            fprintf(stderr, "Failed to read from socket\n");
            exit(1);
        }

		//Here we should add the buffer to the queue. The queue should
		// return an int, which represents it's sequence number.
		// look something like this:
		int seqNum = queue_add(buffer, read_len);
		//We should then pass in the seqNum as one of the args to the
		//  function that will send it to the server
        send_to_sproxy(buffer, read_len, seqNum);
        
        //Adding it to the queue and receiving a seqNum back is important
        // because it will allow us to resend all of the un-ACKed messages
        // in the event that we have our link go down, and have to 
        // reconnect. This is hoping to satisfy the requirement that
        // "there be no gaps in the data". 
    }

    free(buffer);
    pthread_exit(0);
}

bool connect_server(char* ip, int port){
    bool result = true;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    inet_aton(ip, &address.sin_addr); //store IP in address struct
    printf("\t\tSProxy address converted to %s\n", inet_ntoa(address.sin_addr));
    address.sin_port = htons(port);

    printf("\tConnecting to SProxy at %s\n", ip);
    int addrlen = sizeof(address);

    if( connect(server_socket, (struct sockaddr*)&address, addrlen)){
        perror("Failed to connect to server");
        result = false;
    }

    printf("SPROXY Established: %x\n",server_socket);
    return result;
}
  
void* new_connection(){
	connect_server(serverIP, SERVER_PORT);
	sproxy_sendflags(PROXY_NEW_CONNECT, 0);
    sproxy_read();
    
    return NULL;
    
}

void reconnect(){
    bool connected = false;
    while( connected == false){
        sleep(10);
        connected = connect_server(serverIP, SERVER_PORT);
    }
    sproxy_sendflags(PROXY_RECONNECT, 0);
    sproxy_read();
}

void start(){
//basically copy the stuff from sproxy's start
//  but make it connect to telnet first
//  and THEN to the server
    /* Setup socket to accept connections from clients */
        /* Listen to the server port, and accept from any interface */
    struct sockaddr_in s;
    memset(&s, 0, sizeof(struct sockaddr_in));
    s.sin_family = AF_INET;
    s.sin_addr.s_addr = INADDR_ANY; //inet_addr(LOCALHOST);
    s.sin_port = htons(CLIENT_PORT);

        /* Open the socket */
    int socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    if(socket_fd < 0 ){
        perror("Socket failure");
        exit(1); //we haven't actually opened anything, so we don't 
                //need to close or free stuff
    }

    /* Set the socket to allow port reuse */
    int yes = 1;
    int sockopt_err = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    if(sockopt_err != 0) {
        perror("Listener Setup");
        exit(1);
    }
        /* Bind the socket to the port */
    int bind_err = bind(socket_fd, (struct sockaddr*)&s, sizeof(struct sockaddr_in));
    if(bind_err != 0){
        perror("Bind failure");
        exit(1);
    }

        /* 'Listen' for a connection, let up to 5 connections queue up */
    int listen_err = listen(socket_fd, 5);
    if(listen_err != 0){
        perror("Listen failure");
        exit(1);
    }

    //As long as there are connections, (1 at a time) accept them.
    while(1){
        printf("Waiting for a new telnet connection\n");

            /* Accept an incoming connection */
        unsigned int length = sizeof(struct sockaddr_in);
        int new_fd = accept( socket_fd, (struct sockaddr*)&s, &length);
        if( new_fd < 0 ){
            perror("Accept failure");
            exit(1);
        }

        printf("Telnet connection accepted from %s\n", inet_ntoa(s.sin_addr));

        telnet_socket = new_fd;
        
        //connect to server
        if(pthread_create(&server_thr, NULL, new_connection, NULL)){
            perror("Server Pthread");
        }
        
        //connect read from telnet
        if (pthread_create(&telnet_thr, NULL, telnet_read, NULL)) {
			perror("Telnet Pthread");
			exit(-1);
		}
        
        //stay in this state until the telnet session ends/fails/etc
        while(telnet_socket);
    }

}

int main( int argc, char** argv){
    if(argc != 2){
        printf("Usage: ./cproxy <server ip>\n");
        return -1;
    }

    // strlen() returns length excluding \0
    // but strncpy will copy until the \0
    // typically this works out ok, but I am paranoid
    serverIP = malloc( sizeof(char) * (strlen(argv[1])+1) );
    strcpy(serverIP, argv[1]);

    start();

    return 0;
}

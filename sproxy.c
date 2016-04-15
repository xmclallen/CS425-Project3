#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

#include "proxy.h"

int client_socket = 0;
int telnet_socket = 0;

pthread_t cproxy_thr;
pthread_t telnet_thr;


void cproxy_sendflags(enum PROXY_Flags flag, int seqNum);
void telnet_start();

void cleanup(){
    printf("\n\n\nCleaning shit up");
    close(client_socket);
    close(telnet_socket);
    return;
}

void send_to_telnet(char* buffer, int length){
    printf("\tSending to TELNET\n");
    fflush(stdout);
    int write_len =  write(telnet_socket, buffer, length);
    printf("\tFinished send\n");
    if(write_len <= 0){
        perror("Failed to write");
        exit(0);
    }
    printf("\tExiting method\n");
    fflush(stdout);
}


//These variables will be used for the heartbeat timing.

struct timeval tv0;
struct timeval tv1;
time_t curr_time;
time_t prev_msg_rcvd;

struct timeval timeout;

void* cproxy_read(){
    while(1){
        printf("\t Waiting for CProxy\n");

        /* Use select() to multiplex reads */

        //create a file descriptor set
        fd_set set;
        FD_ZERO(&set);
        FD_SET(client_socket, &set);

        //create a timeout struct (this must be done everytime, as 
        // select *will* modify the values)
        struct timeval timer;
        timer.tv_sec = 1;
        timer.tv_usec = 0;
        
        timeout.tv_sec = 3;
        
        int select_err = select(FD_SETSIZE, &set, NULL, NULL, &timer);
        if(select_err <0){
            perror("Failed on select()");
        } //See if it hit the 1s timout.
        else if (select_err == 0){
		printf("\tTimeout\n");		
	//get the current time of day
			gettimeofday(&tv1, NULL);
			curr_time = tv1.tv_sec;
			
			//Now, if the time difference is less than 3 seconds,
            if(curr_time - prev_msg_rcvd < timeout.tv_sec){
				//we will initiate a heartbeat message.
				printf("No data received for %ld second, sending heartbeat\n",
						curr_time - prev_msg_rcvd );
				cproxy_sendflags(PROXY_HEARTBEAT, 0);
				continue;
			}
            else{
			
				printf("No data received for %ld seconds, ",
						curr_time - prev_msg_rcvd);
				fprintf(stderr, "Connection timeout\n");
				close(client_socket);
				client_socket=0;
				return NULL;
			}
        }

        //It read before 1s was up, so we are all good
        
        printf("Now reading from the client socket\n");
        //Allocate a buffer, set all bytes to 0
        char *buffer = calloc( MAX_CHARS, sizeof(char));

        int read_len = read(client_socket, buffer, MAX_CHARS);
        
        if(read_len == 0){
            printf("\tConnection closed by CProxy.\n");
            free(buffer);
            
            //Instead of return NULL, I want this thread to exit, 
            // rather than attempting to to go on and do other things
			pthread_exit(0);
			return NULL; // just in case
        }
        else if(read_len<0){
            perror("Failed to read from CProxy");
            exit(1);
        }else{

			//Read succeded, update the last time that we received a packet
            gettimeofday(&tv0, NULL);
            prev_msg_rcvd = tv0.tv_sec;

            printf("Read %d bytes from CProxy: %.*s", read_len, read_len, buffer);
            /* Would need to differentiate from ACK and data */
            
            //get a header, to do ^
            struct Proxy_Header *head = (struct Proxy_Header*) buffer;
            //get the payload
            char* payload = (char*)(buffer + sizeof(struct Proxy_Header));
            
            int flag = head->flag;
        
        //I know this looks horrible, but it's better than having every
        // single thing take up like a million lines. 
        while(read_len > 0){
            if(flag == PROXY_NEW_CONNECT){
				telnet_start();
			}
			else if(flag == PROXY_HEARTBEAT){
				//do nothing.
				// we should have already updated the prev_msg_rcvd variable
			}
			else if(flag == PROXY_ACK){
				//clear out the queue of anything with a smaller seq_num
				
			}
			//not even sure if there is a time to send this one...
			else if(flag == PROXY_CLOSE){
				printf("\tSession closed via flag\n");
				
				free(buffer);
				//detatch threads
				//close sockets and set to 0 (so the while loops stop)
				return NULL;
			}
			else if(flag == PROXY_DATA){
				//for now, simply forward it off to telnet
				printf("\tReceived packet %d\n", head->seq_num);
				send_to_telnet(payload, head->len);
				
				//later, send back an ACK after forwarding to the telnet			
			}
			else{
				printf("\tReceived an invalid flag from cproxy...dropping\n");
			}
	
			size_t packet_size = (sizeof(struct Proxy_Header)+head->len);
			read_len -= packet_size;
			
			char* next_packet = (char*)(buffer+packet_size);
			free(buffer);
			buffer = next_packet;
			printf("Handled a packet from the buffer, there are %d bytes left.\n",
					read_len);
		}//end while
			//free(buffer); don't double free
		}//end else
	}
	
	return NULL; // probably dead code, but i am paranoid
}

void telnet_connect(){

    telnet_socket = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( 23 );

    int addrlen = sizeof(address);

    printf("\tAttepmting to connect to telnet daemon\n");
    if( connect( telnet_socket, (struct sockaddr*)&address, addrlen)){
        perror("Failed to connect to telnet daemon");
        exit(-1);
    }

    printf("\tTelnet daemon connection established.\n");


    /* Here we would need to send the server a packet telling it that
     * this is a fresh connection, not a re-connect.
     */
}

int send_to_cproxy(char* buffer, int len /*, int seq_num*/){
    size_t size = sizeof(struct Proxy_Header) + len*sizeof(char);

    //create a new buffer of bytes to send
    char *to_send = malloc(size);

    /* create the header */
    
    struct Proxy_Header *head = (struct Proxy_Header*)to_send;
    head->seq_num = 0;
    head->ip = 0;
    head->len = len;
    head->flag = PROXY_DATA;
    

    //copy the buffer into the payload section
    char *message = (char*)(to_send + sizeof(struct Proxy_Header));
    for(int i=0; i<len; i++){
        message[i] = buffer[i];
    }

    printf("\tSending to CProxy\n");

    int write_len = write(client_socket, to_send, size);
    free(to_send);
   // this *could* be useful at some point
    return write_len;
}


/* This function is to be called whenever we need to JUST send a flag
 * to the server proxy  */
void cproxy_sendflags(enum PROXY_Flags flag, int seqNum) {
    ssize_t size = sizeof(struct Proxy_Header);

    char *tosend = malloc(size);

    struct Proxy_Header *connect = (struct Proxy_Header *)tosend;
    connect->flag = flag;
    connect->seq_num = seqNum;
    connect->ip = 0;  //get_ip();
    connect->len = 0;

	printf("~> Sending a flag to CPROXY\n");
    ssize_t write_len = write(client_socket, tosend, size);
    if(write_len <= 0){
        perror("Failed to send a flag to the server\n");
    }
    printf("~> Sent flag %d (size: %ld) to CPROXY\n", flag, write_len);
    free(tosend);
}




void* telnet_read(){
    char *buffer;

    /* Read into buffer, send to sproxy */
    while(1){
        //Allocate a buffer, set all bits to 0
        buffer = calloc( MAX_CHARS, sizeof(char));

        printf("\tWaiting for TELNET\n");
        int read_len = read(telnet_socket, buffer, MAX_CHARS);
        if(read_len == 0){
            printf("\tTelnet session closed.\n");
            // send a packet to the sproxy, letting it know we closed
            close(client_socket);
            close(telnet_socket);
            client_socket = 0;
            telnet_socket = 0;
            free(buffer);
            return NULL;
        }
        else{
            //Add the buffer to the outgoing queue
                //int seq_number = queue_add(buffer, read_len);

            //Then send it out
                //create the header, including seqnumber, etc
                //attempt to write() to the server
             //These should be 2 separate function calls so that messages
              //can be re-sent in the event of a link failure, simply by
              //repeatedly calling the sending function until the queue is empty 
            printf("Read %d bytes from Telnet: %.*s", read_len, read_len, buffer);
            send_to_cproxy(buffer, read_len /*, seq_number*/);

        }
    }

    free(buffer);
    pthread_exit(0);
}

void telnet_start(){
	if (telnet_socket == 0)
        telnet_connect();

    if (pthread_create(&telnet_thr, NULL, telnet_read, NULL) != 0) {
        perror("Telnet Pthread");
        exit(-1);
    }   
}

void start(){
    /* Setup socket to accept connections from clients */
        /* Listen to the server port, and accept from any interface */
    struct sockaddr_in s;
    memset(&s, 0, sizeof(s));
    s.sin_family = AF_INET;
    s.sin_addr.s_addr = INADDR_ANY;
    s.sin_port = htons(SERVER_PORT);

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
        perror("Socket option - SO_REUSEADDR");
        exit(1);
    }

    /* Set the socket for keepalive */
    sockopt_err = setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int));
    if(sockopt_err != 0) {
        perror("Socket option - SO_KEEPALIVE");
        exit(1);
    }
    
    /* Bind the socket to the server port */
    int bind_err = bind(socket_fd, (struct sockaddr*)&s, sizeof(s));
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
        printf("Waiting for a new connection\n");

            /* Accept an incoming connection */
    	unsigned int length = sizeof(struct sockaddr_in);
    	int new_fd = accept(socket_fd, (struct sockaddr *)&s, &length);
    	if(new_fd < 0) {
    		perror("Accept failure");
    		exit(1);
    	}

        printf("Connection accepted from %s\n", inet_ntoa(s.sin_addr));

        client_socket = new_fd;

        //create a thread to run cproxy_read
        if (pthread_create(&cproxy_thr, NULL, cproxy_read, NULL)) {
			perror("Client Pthread");
			exit(-1);
		}
		
		//telnet_start(); //commented out b/c it should start reading only
						// when it receives a new connect packet
       
        while(client_socket);
    }

}

int main(int argc, char** argv){
    start();
    cleanup();
    
    exit(0);
}

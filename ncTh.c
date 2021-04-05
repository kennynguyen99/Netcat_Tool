#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include "commonProto.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/poll.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include "Thread.h"


#define MYPORT "3543" // the port users will be connecting to
#define BACKLOG 5     // how many pending connections
#define TRUE 1
#define FALSE 0
#define MAXDATASIZE 100


struct int server_fds[100];
int nConnections = 0;

int server(struct commandOptions *commandOptions);
int createServerSocket();
void serverInThread();
void *connectionThread(void *arg);

int main(int argc, char **argv) {
  
  // This is some sample code feel free to delete it
  // This is the main program for the thread version of nc
  
  struct commandOptions cmdOps;
  int retVal = parseOptions(argc, argv, &cmdOps);
  printf("Command parse outcome %d\n", retVal);

  printf("-k = %d\n", cmdOps.option_k);
  printf("-l = %d\n", cmdOps.option_l);
  printf("-v = %d\n", cmdOps.option_v);
  printf("-r = %d\n", cmdOps.option_r);
  printf("-p = %d\n", cmdOps.option_p);
  printf("-p port = %u\n", cmdOps.source_port);
  printf("-w  = %d\n", cmdOps.option_w);
  printf("Timeout value = %u\n", cmdOps.timeout);
  printf("Host to connect to = %s\n", cmdOps.hostname);
  printf("Port to connect to = %u\n", cmdOps.port);  
}

// creates a socket to listen for incoming connections
int createServerSocket(){
    // listen on sock_fd
    int sockfd;

    // hints, result of get address info, and a pointer to iterate over the linked list
    struct addrinfo hints, *servinfo, *p;
    socklen_t sin_size;

    int rv;
    int bytesReceived;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       // use IPv4
    hints.ai_socktype = SOCK_STREAM; // use TCP
    hints.ai_flags = AI_PASSIVE;     // use my IP address

    if ((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if (p->ai_family == AF_INET)
        {

            // get a socket descriptor
            if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            {
                perror("server: socket");
            }

            int yes = 1;

            if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
            {
                perror("sockopt = -1");
                exit(1);
            }

            if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
            {
                close(sockfd);
                perror("server: bind");
                continue;
            }

            // successfully got a socket and binded
            break;
        }
        else
        {
            continue;
        }
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    return sockfd;
}

int server(struct commandOptions *commandOptions){
    int listen_fd = createServerSocket();
    int new_sd;

    createThread(&serverInThread);

    while(TRUE) {

        new_sd = accept(listen_fd, (struct sockaddr *) &their_addr, &sin_size);

        server_fds[nConnections] = new_sd;

        // need to pass the index of the fd in the table that was just accepted
        createThread(&connectionThread);
        nConnections++;
    }
}

// thread to monitor server's stdin to write to every connection
void serverInThread(){
    char send_buff[1000];
    int bytesToSend, bytesSent;

    while(fgets(send_buff, sizeof(send_buff), stdin)) {
        bytesToSend = strlen(send_buff);

        for (int i = 0; i < nConnections; i++) {
            bytesSent = send(server_fds[i], send_buff, bytesToSend, 0);
        }
    }
}


// thread to monitor every connection to write to stdout
void connectionThread(void *arg){
    args a = *(args *)arg;
    int this_fd_index;
    int closeConnection = FALSE;
    int VERBOSE = commandOptions->option_v;

    if (VERBOSE){
        printf("Descriptor %d is readable\n", fds[i].fd);
    }


    // receive all incoming data on this socket before waiting for more data
    int bytesReceived;
    do {
        // receive data on this connection until the recv fails with EWOULDBLOCK
        // If any other failure occurs, we will close the connection
        bytesReceived = recv(fds[i].fd, buffer, sizeof(buffer), 0);

        if (bytesReceived < 0) {
            if (VERBOSE) {
                printf("Bytes received <0\n");
            }
            if (errno != EWOULDBLOCK) {
                perror("recv() failed");
                closeConnection = TRUE;
            }
            break;
        }

        // check to see if the connection has been closed by client
        if (bytesReceived == 0) {
            if (VERBOSE) {
                printf("Connection closed\n");
            }
            closeConnection = TRUE;
            break;
        }

        if (VERBOSE) {
            printf("%d bytes received\n", bytesReceived);
            // write to standard out
            printf("Client with fd %d says:\n", server_fds[this_fd_index].fd);
        }

        fwrite(buffer, sizeof(char), bytesReceived, stdout);
    }while (TRUE);

    return NULL;
}




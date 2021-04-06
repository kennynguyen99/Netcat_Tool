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
#include <semaphore.h>

#define MYPORT "3543" // the port users will be connecting to
#define BACKLOG 5     // how many pending connections
#define TRUE 1
#define FALSE 0
#define MAXCONNECTIONS 5
#define MAXDATASIZE 100

 int server_fds[100];
int nConnections = 0;
int currMaxConnections = 1;
sem_t mutex;

int server(struct commandOptions *commandOptions);
int createServerSocket(char *address, unsigned int port);
void serverInThread();
void *connectionThread(void *arg);
int createClientSocket(char *address, unsigned int port, unsigned int sourcePort);

int main(int argc, char **argv)
{

    // This is some sample code feel free to delete it
    // This is the main program for the thread version of nc

    struct commandOptions cmdOps;
    int retVal = parseOptions(argc, argv, &cmdOps);

    //initialize the semaphore
    if(cmdOps.r) {
        currMaxConnections = MAXCONNECTION;
        sem_init(&mutex, 0, MAXCONNECTIONS);
    }else{
        sem_init(&mutex, 0, 1);
    }

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
int createServerSocket(char *address, unsigned int port)
{
    // listen on sock_fd
    int sockfd;

    // hints, result of get address info, and a pointer to iterate over the linked list
    struct addrinfo hints, *servinfo, *p;
    socklen_t sin_size;

    int rv;
    int bytesReceived;
    char *serverHost = NULL;
    char *portNumber = (char *)malloc(sizeof(port));
    if (address != NULL)
    {
        serverHost = address;
    }
    if (port > 0)
    {
        sprintf(portNumber, "%u", port);
    }
    else
    {
        strcpy(portNumber, MYPORT);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       // use IPv4
    hints.ai_socktype = SOCK_STREAM; // use TCP
    hints.ai_flags = AI_PASSIVE;     // use my IP address

    if ((rv = getaddrinfo(serverHost, portNumber, &hints, &servinfo)) != 0)
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
    int listen_fd = createServerSocket(commandOptions->hostname, commandOptions->port);
    int new_sd;
    struct Thread newConnectionThread;
    int *sem_value;
    int fd_index;

    detachThread(createThread(&serverInThread, &listen_fd));

    while(TRUE){
        new_sd = accept(listen_fd, (struct sockaddr *)&their_addr, &sin_size);

        sem_trywait(mutex);

        // if the semaphore is at 0 ie. we cannot have more connections
        if(errorno == EAGAIN){
            close(new_sd);
            continue;
        }

        sem_getvalue(mutex, sem_value);
        nConnections = currMaxConnections - *sem_value;

        if(nConnections == 0 && !commandOptions->option_k){
            break;
        }

        fd_index = nConnections-1;
        server_fds[fd_index] = new_sd;

        // create a new thread to monitor this connection
        newConnectionThread = createThread(&connectionThread, &fd_index);
        detachThread(&newConnectionThread);
    }

    close(listen_fd);
    // end the process
    sem_destroy(&mutex);
    exit(0);
}

// thread to monitor server's stdin to write to every connection
void serverInThread(void *listen_fd){

    int listen_fd_socket =  *(int *) listen_fd;
    char send_buff[1000];
    int bytesToSend, bytesSent;

    while (fgets(send_buff, sizeof(send_buff), stdin))
    {
        bytesToSend = strlen(send_buff);

        for (int i = 0; i < nConnections; i++)
        {
            bytesSent = send(server_fds[i], send_buff, bytesToSend, 0);
        }
    }

    close(listen_fd_socket);
    // end the process
    sem_destroy(&mutex);
    exit(0);
}

// thread to monitor every connection to write to stdout
void connectionThread(void *fd_index)
{
    // args a = *(args *)arg;
    int VERBOSE = commandOptions->option_v;
    int this_fd_index = *(int *)fd_index; // This would be pased in args.

    if (VERBOSE)
    {
        printf("Descriptor %d is readable\n", server_fds[this_fd_index]);
    }

    // receive all incoming data on this socket before waiting for more data
    int bytesReceived;
    do
    {
        // receive data on this connection until the recv fails with EWOULDBLOCK
        // If any other failure occurs, we will close the connection
        bytesReceived = recv(server_fds[this_fd_index], buffer, sizeof(buffer), 0);

        if (bytesReceived < 0)
        {
            if (VERBOSE)
            {
                printf("Bytes received <0\n");
            }
            if (errno != EWOULDBLOCK)
            {
                perror("recv() failed");
                closeConnection = TRUE;
            }
            break;
        }

        // check to see if the connection has been closed by client
        if (bytesReceived == 0)
        {
            if (VERBOSE)
            {
                printf("Connection closed\n");
            }
            break;
        }

        if (VERBOSE)
        {
            printf("%d bytes received\n", bytesReceived);
            // write to standard out
            printf("Client with fd %d says:\n", server_fds[this_fd_index]);
        }

        fwrite(buffer, sizeof(char), bytesReceived, stdout);
        for (int i = 0; i < nConnections; i++)
        {
            if (i != this_fd_index)
            {
                bytesSent = send(server_fds[i], send_buff, bytesToSend, 0);
            }
        }
    } while (TRUE);

    close(server_fds[this_fd_index]);

    server_fds[this_fd_index] = -1;

    // compress array
    for (int i = 0; i < nConnections; i++){
        if (server_fds[i] == -1){
            for(int j = i; j < nConnections; j++){
                server_fds[j] = server_fds[j+1];
            }
            i--;
            break;
        }
    }

    sem_post(mutex);
    return NULL;
}

int createClientSocket(char *address, unsigned int port, unsigned int sourcePort)
{
    int sockfd, numbytes;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_in client_addr;
    int rv;
    char buf[MAXDATASIZE];
    char *serverHost = NULL;
    char *portNumber = (char *)malloc(sizeof(port));
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // use IPv4
    hints.ai_socktype = SOCK_STREAM; // use TCP
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = htons(sourcePort);

    if (address != NULL)
    {
        serverHost = address;
    }
    if (port > 0)
    {
        sprintf(portNumber, "%u", port);
    }
    else
    {
        strcpy(portNumber, MYPORT);
    }

    if ((rv = getaddrinfo(address, portNumber, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "netcat: error in getaddrinfo() with source: %s\n", gai_strerror(rv));
        exit(0);
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if (p->ai_family == AF_INET)
        {

            sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

            if (sockfd == -1)
            {
                perror("netcat: error in socket()");
                continue;
            }

            if (sourcePort > 0)
            {
                int yes = 1;
                if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
                {
                    perror("sockopt = -1");
                    exit(1);
                }
                if (bind(sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) == -1)
                {
                    close(sockfd);
                    perror("client: bind");
                    continue;
                }
            }
            if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
            {
                close(sockfd);
                perror("netcat: error in connect()");
                continue;
            }

            break;
        }
        else
        {
            continue;
        }
    }

    if (p == NULL)
    {
        printf("netcat: error could not connect\n");
        exit(0);
    }
    freeaddrinfo(servinfo);
    return sockfd;
}

int client(struct commandOptions *commandOptions)
{
    // we don't need a semaphore for the client
    sem_destroy(&mutex);
    int client_fd = createClientSocket(commandOptions->hostname, commandOptions->port, commandOptions->source_port);
    struct Thread *recvThread = createThread(&clientRecvThread, &client_fd);
    struct Thread *sendThread = createThread(&clientSendThread, &client_fd);

    joinThread(recvThread, NULL);
    joinThread(sendThread, NULL);
}

// client thread that monitors the connection and writes to stdout
void clientRecvThread(void *connection_fd)
{
    char recv_buff[1000];
    int bytesReceived;

    int this_fd = *(int *)connection_fd; // passed as the arg
    int VERBOSE = commandOptions->option_v; // the commandOpts are passed in args

    // receive all incoming data on this socket before waiting for more data
    do
    {
        // receive data on this connection until the recv fails with EWOULDBLOCK
        // If any other failure occurs, we will close the connection
        bytesReceived = recv(this_fd, recv_buff, sizeof(recv_buff), 0);

        if (bytesReceived < 0)
        {
            if (VERBOSE)
            {
                printf("Bytes received <0\n");
            }

            if (errno != EWOULDBLOCK)
            {
                perror("recv() failed");
                closeConnection = TRUE;
            }
            break;
        }

        // check to see if the connection has been closed by client
        if (bytesReceived == 0)
        {
            if (VERBOSE)
            {
                printf("Connection closed\n");
            }
            break;
        }

        if (VERBOSE)
        {
            printf("%d bytes received\n", bytesReceived);
        }
        // write to standard out
        fwrite(recv_buff, sizeof(char), bytesReceived, stdout);
    } while (TRUE);

    close(this_fd);
    exit(0);
    return NULL;
}

// thread to monitor client's stdin to write to server
void clientSendThread(void *connection_fd)
{
    char send_buff[1000];
    int bytesToSend, bytesSent;

    int this_fd = *(int *)connection_fd; // passed as the arg
    int closeConnection = FALSE;
    int VERBOSE = commandOptions->option_v; // the commandOpts are passed in args

    // send data
    do
    {
        // standard in is ready to read
        if (VERBOSE)
        {
            printf("client's stdin ready to read\n");
        }
        char *line = fgets(send_buff, sizeof(send_buff), stdin);

        if (line == NULL){
            break;
        }

        bytesToSend = strlen(send_buff);
        bytesSent = send(sockfd, send_buff, bytesToSend, 0);
    } while (TRUE);


    close(this_fd);
    exit(0);
    return NULL;
}
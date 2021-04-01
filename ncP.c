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

#define MYPORT "3490" // the port users will be connecting to
#define BACKLOG 5     // how many pending connections
#define TRUE 1
#define FALSE 0
#define MAXDATASIZE 100

int main(int argc, char **argv)
{

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
    if (cmdOps.option_l)
    {
        int server_fd = server(&cmdOps);
    }
    else
    {
        int client_fd = client(&cmdOps);
    }
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

    if ((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next){
        if (p->ai_family == AF_INET){

            // get a socket descriptor
            if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
                perror("server: socket");
            }

            int yes = 1;

            if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1){
                perror("sockopt = -1");
                exit(1);
            }

            if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
                close(sockfd);
                perror("server: bind");
                continue;
            }

            // successfully got a socket and binded
            break;
        }
        else{
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

    /*
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    */

    return sockfd;
}

int server(struct commandOptions *commandOptions)
{
    if (commandOptions->option_p)
    {
        printf("It is an error to use this option with the -p option.\n");
        return -1;
    }

    /*
    struct sockaddr_storage their_addr; // connector's address
    socklen_t sin_size;
    int bytesReceived;

    int sockfd = createServerSocket();
    new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);

    char buffer[5000];
    // read from connection
    bytesReceived = recv(new_fd, buffer, sizeof buffer, 0);

    // write to stdout
    fwrite(buffer, sizeof char, 1, bytesReceived, stdout);
    */

    int listenfd = createServerSocket();
    serverPolling(listenfd, commandOptions);
}

// Server performs polling and reads from a connection and writes to stdout and every other connection
int serverPolling(int sockfd, struct commandOptions *commandOptions)
{
    struct pollfd fds[100];
    int timeout;
    int nfds = 2, currentSize = 0;
    int pollin_happened;
    struct sockaddr_storage their_addr; // connector's address
    socklen_t sin_size;
    int new_sd;
    int closeConnection;
    int endServer = FALSE;
    int rv;
    char buffer[1000];
    char send_buff[1000];
    int bytesToSend, bytesSent;

    //initialize the poll fd structure
    memset(fds, 0, sizeof(fds));

    // set up stdin
    fds[0].fd = 0;
    fds[0].events = POLLIN;

    // set up the initial listening socket
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;


    // Initialize the timeout to 3 minutes. If no activity after 3 minutes, this program will ends.
    // time out value is based in miliseconds
    timeout = (3 * 60 * 1000);

    // Loop waiting for incoming connects or for incoming data on any of the connected sockets
    do
    {
        // call poll and wait for 3 minutes for it to expire
        printf("Waiting on poll()...\n");
        rv = poll(fds, nfds, timeout);
        printf("Awake from poll\n");

        // check to see if the poll call failed.
        if (rv < 0)
        {
            perror("poll() failed");
            break;
        }

        // No events happened
        if (rv == 0)
        {
            printf("poll() timed out. End Program. \n");
            break;
        }

        // one or more descriptors are readable. Need to determine which ones they are
        currentSize = nfds;

        for (int i = 0; i < currentSize; i++)
        {
            // Loop throught to find the descriptors that returned
            // POLLIN and determine whether it's the listening or the active connection

            pollin_happened = fds[i].revents & POLLIN;

            if (fds[i].revents == 0)
            {
                continue;
            }

            if (fds[i].revents != POLLIN)
            {
                printf("Error! revents = %d. End Program.\n", fds[i].revents);
                endServer = TRUE;
                break;
            }

            if (pollin_happened)
            {
                // file descriptor is ready to read
                printf("file descriptor %d is ready to read\n", fds[i].fd);
            }

            if (fds[i].fd == sockfd)
            {
                // Listening descriptor is readable
                printf("Listening socket is readable\n");

                do
                {
                    if(!commandOptions->option_r && nfds == 3){
                        break;
                    }

                    printf("start of loop \n");


                    // Accept all incoming connection that are queued up on the listening socket
                    // before we loop back and call poll again
                    new_sd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
                    if (new_sd < 0)
                    {
                        if (errno != EWOULDBLOCK)
                        {
                            perror("accept failed");
                            //end program
                            endServer = TRUE;
                        }
                        break;
                    }
                    // add the new incoming connection to the pollfd structure
                    printf("New incoming connection - %d\n", new_sd);
                    fds[nfds].fd = new_sd;

                    // we're interested in whenever this socket is ready to be read
                    fds[nfds].events = POLLIN;
                    nfds++;

                    // loop back up and accept another incoming connection
                } while (new_sd != -1 );
            }

            else if(fds[i].fd == 0)
            {
                // standard in is ready to read
                printf("server's stdin ready to read\n");
                fgets(send_buff, sizeof(send_buff), stdin);
                bytesToSend = strlen(send_buff);

                for (int j = 2; j < currentSize; j++){
                    bytesSent = send(fds[j].fd, send_buff, bytesToSend, 0);
                }
            }
            else
            {
                printf("Descriptor %d is readable\n", fds[i].fd);
                closeConnection = FALSE;

                // receive all incoming data on this socket before we loop back and call poll again
                int bytesReceived;
                do
                {
                    // receive data on this connection until the recv fails with EWOULDBLOCK
                    // If any other failure occurs, we will close the connection
                    printf("Ready to rcv\n");
                    bytesReceived = recv(fds[i].fd, buffer, sizeof(buffer), MSG_DONTWAIT);
                    printf("Receive value, %d", bytesReceived);

                    if (bytesReceived < 0)
                    {
                        printf("Bytes received <0\n");
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
                        printf("Connection closed\n");
                        closeConnection = TRUE;
                        break;
                    }

                    printf("%d bytes received\n", bytesReceived);

                    // write to standard out
                    fwrite(buffer, sizeof(char), bytesReceived, stdout);

                    // write to every other connection except the one currently read
                    int bytesSent;
                    int totalByteSent;

                    for (int j = 2; j < currentSize; j++)
                    {
                        totalByteSent = 0;
                        if (i != j)
                        {

                            while (totalByteSent < bytesReceived)
                            {
                                bytesSent = send(fds[i].fd, buffer, bytesReceived, 0);
                                totalByteSent += bytesSent;

                                if (bytesSent < 0)
                                {
                                    perror("send() failed");
                                    closeConnection = TRUE;
                                    break;
                                }
                            }
                        }
                    }

                } while (TRUE);

                // if the closeConnection flag was turned on, we need to clean up this active connection
                // This clean up process includes removing the descriptor
                if (closeConnection)
                {
                    close(fds[i].fd);
                    fds[i].fd = -1;

                    // compress array
                    // for (int i = 0; i < nfds; i++)
                    // {
                    //     if (fds[i].fd == -1)
                    //     {
                    //         for (int j = i; j < nfds; j++)
                    //         {
                    //             fds[j].fd = fds[j + 1].fd;
                    //         }
                    //         i--;
                    //         nfds--;
                    //     }
                    // }

                    // check if there are any connections left and if we should end the server
                    if (!commandOptions->option_k && nfds == 2)
                    {
                        endServer = TRUE;
                    }
                }
            }
        }
    } while (endServer == FALSE);
}

int createClientSocket(char *address, unsigned int port, unsigned int sourcePort)
{
    int sockfd, numbytes;
    struct addrinfo hints, *servinfo, *p; //, sourceHints, *sourceServInfo;
    int rv;
    char buf[MAXDATASIZE];
    char *serverPort;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // use IPv4
    hints.ai_socktype = SOCK_STREAM; // use TCP

    if (port != 0)
    {
        sprintf(serverPort, "%d", &port);
    }
    else
    {
        serverPort = MYPORT;
    }

    if ((rv = getaddrinfo(address, serverPort, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "netcat: error in getaddrinfo() with source: %s\n", gai_strerror(rv));
        exit(0);
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if (p->ai_family == AF_INET) {
            sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

            if (sockfd == -1) {
                perror("netcat: error in socket()");
                continue;
            }

            /*
            if (sourcePort > 0) {
                struct sockaddr_in my_addr;
                my_addr.sin_family = AF_INET;
                my_addr.sin_port = htons(sourcePort);


                // add a condition if there is a specified source port
                if (bind(sockfd, (struct sockaddr*) &my_addr, sizeof(my_addr)) == -1) {
                    close(sockfd);
                    perror("client: error in source port bind()");
                    break;
                }
            }*/



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
    int bytesReceived;
    char recv_buff[1000];
    char send_buff[1000];
    int bytesSent;
    int bytesToSend;

    struct pollfd client_fds[100];
    int timeout;
    int nfds = 2, currentSize = 0;
    int pollin_happened;

    int clientfd = createClientSocket(commandOptions->hostname, commandOptions->port, commandOptions->source_port);
    printf("We returned and the client fd is %d \n", clientfd);


    /*
    //initialize the poll fd structure
    memset(fds, 0, sizeof(fds));

    // set up stdin
    fds[0].fd = 0;
    fds[0].events = POLLIN;

    // set up the initial listening socket
    fds[1].fd = clientfd;
    fds[1].events = POLLIN;


    // Initialize the timeout to 3 minutes. If no activity after 3 minutes, this program will ends.
    // time out value is based in miliseconds
    timeout = (3 * 60 * 1000);

    do {
        // call poll and wait for 3 minutes for it to expire
        printf("Waiting on poll()...\n");
        rv = poll(client_fds, nfds, timeout);
        printf("Awake from poll\n");


    }while(TRUE);
     */


    while(TRUE) {

        // scanf("%s", send_buff);

        fgets(send_buff, sizeof(send_buff), stdin);
        bytesToSend = strlen(send_buff);
        bytesSent = send(clientfd, send_buff, bytesToSend, 0);
        if (bytesSent == -1) {
            printf(errno);
        }
        printf("bytes sent , %d \n", bytesSent);
    }


    /*
    if(feof(stdin)){
        printf("Client: Finished reading from stdin, ending process \n");

    }else{
        printf("Client: error occured when reading from stdin\n");
    }
    */



}
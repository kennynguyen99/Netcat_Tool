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


#define MYPORT "3490"  // the port users will be connecting to
#define BACKLOG 5 // how many pending connections
#define TRUE 1
#define FALSE 0

int main(int argc, char **argv) {
  
  // This is some sample code feel free to delete it
  // This is the main program for the polling version of nc
  
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
    hints.ai_family = AF_INET; // use IPv4
    hints.ai_socktype = SOCK_STREAM; // use TCP
    hints.ai_flags = AI_PASSIVE; // use my IP address

    if( (rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0 ){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next){
        if(p->ai_family == AF_INET){

            // get a socket descriptor
            if( (sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1 ){
                perror("server: socket");
            }


            if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1){
                perror("sockopt = -1");
            }


            if( bind(sockfd, p->ai_addr, p->ai_addrlen) == -1 ){
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

    if(p == NULL){
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if( listen(sockfd, BACKLOG) == -1 ){
        perror("listen");
        exit(1);
    }
    return sockfd;
}

// Server performs polling and reads from a connection and writes to stdout and every other connection
int serverPolling(int sockfd, struct commandOptions *commandOptions){
  struct pollfd fds[100];
  int timeout;
  int nfds = 1, currentSize = 0;
  int pollin_happened;
  struct sockaddr_storage their_addr; // connector's address
  socklen_t sin_size;
  int new_sd;
  int closeConnection;
  int endServer = FALSE;
  int rv;
  char buffer[1000];

  //initialize the poll fd structure
  memset(fds, 0, sizeof(fds));

  // set up the initial listening socket
  fds[0].fd = sockfd;
  fds[0].events = POLLIN;

  // Initialize the timeout to 3 minutes. If no activity after 3 minutes, this program will ends.
  // time out value is based in miliseconds
  timeout = (3 * 60 * 1000);

  // Loop waiting for incoming connects or for incoming data on any of the connected sockets
  do{
    // call poll and wait for 3 minutes for it to expire
    printf("Waiting on poll()...\n");
    rv = poll(fds, nfds, timeout);

    // check to see if the poll call failed.
    if(rv < 0){
      perror("poll() failed");
      break;
    }

    // No events happened
    if(rv == 0){
      printf("poll() timed out. End Program. \n");
      break;
    }

    // one or more descriptors are readable. Need to determine which ones they are 
    currentSize = nfds;

    for(int i = 0; i< currentSize; i++){
      // Loop throught to find the descriptors that returned 
      // POLLIN and determine whether it's the listening or the active connection

      pollin_happened = fds[i].revents & POLLIN;

      if(fds[i].revents == 0){
        continue;
      }

      if(fds[i].revents != POLLIN){
        printf("Error! revents = %d. End Program.\n", fds[i].revents);
        endServer = TRUE;
        break;
      }

      if(pollin_happened){
          // file descriptor is ready to read
          printf("file descriptor %d is ready to read", fds[i].fd);
      }

      if(fds[i].fd == sockfd){
        // Listening descriptor is readable
        printf("Listening socket is readable\n");

        do{
            if(!commandOptions->option_r && nfds >= 2){
                // we cannot have more than 1 connection so do not accept any connection
                break;
            }

            // Accept all incoming connection that are queued up on the listening socket
            // before we loop back and call poll again
            new_sd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
            if(new_sd <0){
                if(errno!= EWOULDBLOCK){
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
        } while(new_sd != -1);

      }else{
          printf("Descriptor %d is readable\n", fds[i].fd);
          closeConnection = FALSE;

          // receive all incoming data on this socket before we loop back and call poll again
          int bytesReceived;
          do {
              // receive data on this connection until the recv fails with EWOULDBLOCK
              // If any other failure occurs, we will close the connection

              bytesReceived = recv(fds[i].fd, buffer, sizeof(buffer), 0);

              if(bytesReceived<0){
                  if(errno != EWOULDBLOCK){
                      perror("recv() failed");
                      closeConnection = TRUE;
                  }
                  break;
              }

              // check to see if the connection has been closed by client
              if(bytesReceived == 0){
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

              for(int j = 1; j < currentSize; j++){
                  totalByteSent = 0;
                  if( i!=j ) {

                      while (totalByteSent < bytesReceived) {
                          bytesSent = send(fds[i].fd, buffer, bytesReceived, 0);
                          totalByteSent += bytesSent;

                          if (bytesSent < 0) {
                              perror("send() failed");
                              closeConnection = TRUE;
                              break;
                          }
                      }
                  }
              }

          } while(TRUE);

          // if the closeConnection flag was turned on, we need to clean up this active connection
          // This clean up process includes removing the descriptor
          if(closeConnection){
              close(fds[i].fd);
              fds[i].fd = -1;

              // compress array
              for(int i=0; i< nfds; i++){
                  if(fds[i].fd == -1){
                      for(int j=i; j<nfds; j++){
                          fds[j].fd = fds[j+1].fd;
                      }
                      i--;
                      nfds--;
                  }
              }

              // check if there are any connections left and if we should end the server
              if(!commandOptions->option_k && nfds == 1){
                  endServer = TRUE;
              }
          }
      }
    }
  } while(endServer == FALSE);
}

int server(struct commandOptions *commandOptions){
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



int createClientSocket(char *address, char *port, char *sourcePort){
    int sockfd, gai_error;
    struct addrinfo hints, *servinfo, *p, sourceHints, *sourceServInfo;

    struct sockaddr_in local_addr;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // use IPv4
    hints.ai_socktype = SOCK_STREAM; // use TCP

    if ((gai_error = getaddrinfo(address, port, &hints, &servinfo)) != 0){
        fprintf(stderr, "netcat: error in getaddrinfo(): %s\n", gai_strerror(gai_error));
        exit(0);
    }

    memset(&hints, 0, sizeof(hints));
    sourceHints.ai_family = AF_INET; // use IPv4
    sourceHints.ai_socktype = SOCK_STREAM; // use TCP
    sourceHints.ai_flags = AI_PASSIVE; // use my IP address

    if ((gai_error = getaddrinfo("localhost", sourcePort, &sourceHints, &sourceServInfo)) != 0){
        fprintf(stderr, "netcat: error in getaddrinfo() with source: %s\n", gai_strerror(gai_error));
        exit(0);
    }



    for (p = servinfo; p != NULL; p = p->ai_next){
        if(p->ai_family == AF_INET) {
            sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

            if (sockfd == -1) {
                perror("netcat: error in socket()");
                continue;
            }

            /*
            // add a condition if there is a specified source port
            if(bind(sockfd, sourceServInfo->ai_addr, sourceServInfo->ai_addrlen) == -1)
                close(sockfd);
                perror("client: error in source port bind()");
                break;
            }
            */


            if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
                close(sockfd);
                perror("netcat: error in connect()");
                continue;
            }

            break;
        }else{
            continue;
        }
    }

    if (p == NULL){
        printf("netcat: error could not connect\n");
        exit(0);
    }

    freeaddrinfo(servinfo);
    freeaddrinfo(sourceServInfo);
    return sockfd;
}

int client(struct commandOptions *commandOptions){
    int sockfd = createClientSocket(commandOptions->hostname, commandOptions->port);

}


/*

int performPolling(){
    int numfds = 5;
    struct pollfd pfds[5];


    pfds[0].fd = 0; // standard input
    pfds[0].events = POLLIN; // tell me when ready to read
    int timeout = (3 *60 * 1000);
    int num_events = poll(pfds, 1, timeout);

    if(num_events == 0){
        printf("Poll timed out!\n");
    } else{

        for(int i = 0; i)
            int pollin_happened = pfds[0].revents & POLLIN;

        if(pollin_happened){
            printf("File descriptor %d is read to read\n", pfds[i].fd);
        }
    }
}
*/


/*
int example(){
    // listen on sock_fd, new connection on new_fd
    int sockfd, new_fd;

    // hints, result of get address info, and a pointer to iterate over the linked list
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address

    socklen_t sin_size;

    int rv;
    int bytesReceived;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // use IPv4
    hints.ai_socktype = SOCK_STREAM; // use TCP
    hints.ai_flags = AI_PASSIVE; // use my IP address

    if((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next){
        if(p->ai_family == AF_INET){

            // get a socket descriptor
            if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
                perror("server: socket");
            }


            if(bind(sockfd, p->ai_addr, p->ai_addrelen) == -1){
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

    if(p == NULL){
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if( listen(sockfd, BACKLOG) == -1 ){
        perror("listen");
        exit(1);
    }

    new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);

    char buffer[5000];
    // read from connection
    bytesReceived = recv(new_fd, buffer, sizeof buffer, 0);

    // Not sure what the size and counts should be
    fwrite(buffer, sizeof char, count, 5000, stdout);

}
*/




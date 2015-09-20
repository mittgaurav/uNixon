/*******************************************
** Test the un-fairness of linux socket connections
** even in presence of SO_REUSEPORT for linux
** version < 3.9. More on this issue at 
** https://lwn.net/Articles/542738/.
** 
** This issue manisfests itself in presence of more
** than 1 processes listening on same socket. How is
** that possible, create socket and fork the server.
**
** Haven't really tested on that version
** or later versions; Linus fixed this issue in
** version 3.9.
*******************************************/
#include <iostream>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <string.h>

// create socket, bind to it, and start listening on it.
void connectAndBind()
{
    // Create socket
    int sd = socket(PF_INET, SOCK_STREAM, 0);
    if(sd < 0) {
        perror("Socket");
        exit(1);
    }

    // define socket  
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(9999);
    if( inet_aton("127.0.0.1", &addr.sin_addr) == 0 ) {
        perror("127.0.0.1");
        exit(1);
    }

    // bind to address
    if( bind(sd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ) {
        perror("Connect");
        exit(1);
    }

    // socket should be non-blocking
    if( -1 == fcntl(sd, F_SETFL, fcntl(sd, F_GETFL,0) | O_NONBLOCK)) {
        perror("fcntl");
        exit(1);
    }

    // now, set reuseport and reuseaddr
    int option = 1;
    if(-1 == setsockopt(sd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
            (char*) &option, sizeof( option ))) {
        perror("setsockopt");
        exit(1);
    }

    // start listening
    if(listen(sd,200) != 0) {
        perror("listen");
        exit(1);
    }
}

// forks off into four servers and returns an instance id
int forkN() {
    int instance = 0;

    for(int i = 0; i < 4; ++i) {
        pid_t pid = fork();
        if(0 == pid) {
            instance = i;
            break;
        }
        else if( -1 == pid) {
            perror("fork");
            exit(1);
        }
    }

    std::cout << "started instance %d" << instance << "\n";
    return instance;
}

// Allow clients to connect and then go away
int main()
{
    int sd = createAndBind();

    int MAX = 409; // connections per server

    struct epoll_event event;
    struct epoll_event* events;

    // create epoll fd. Here, we shall listen to new events.
    int eventFD = epoll_create1( 0 );
    if( -1 == eventFD ) {
        perror( "epoll");
        exit(1);
    }

    event.data.fd = sd;
    event.events  = EPOLLIN | EPOLLET;

    // add this first event to listen on socket to eventloop
    if( -1 == epoll_ctl(eventFD, EPOLL_CTL_ADD, sd, &event)) {
        perror("epoll_ctl");
        exit(1);
    }

    // fork the server into multiple processes
    int instance = forkN();
    events = (epoll_event*) calloc(MAX, sizeof(event));

    // eventloop begins
    while(true) {
        // wait for events on eventFD. The first time, there is
        // only one event possible; incoming message on socket.
        int n = epoll_wait(eventFD, events, MAX, -1);
        for(int i = 0; i < n; ++i) {
            // The reason people like epoll is that it is linear
            // in terms of events actually received; unlike poll
            // that is linear in terms of events registered.

            if((events[i].events & EPOLLERR)
                    || (events[i].events & EPOLLHUP)
                    || !(events[i].events &EPOLLIN)) {
                // an error has occurred on this fd
                // or, socket is not ready for reading
                // either way, why are we notified, uh?
                fprintf(stderr, "epoll error");
                close(events[i].data.fd);
                continue;
            }
            else if(sd == events[i].data.fd) {
                // We got a message on original socket. Thus, we
                // have got incoming connection(s). Let's add it!
                while( true ) {
                    struct sockaddr in_addr;
                    socklen_t in_len = sizeof( in_addr );
                    
                    // accept the incoming connections, one by one.
                    int infd = accept(sd, &in_addr, &in_len);
                    if( -1 == infd ) {
                        // Error on accepting the connection.
                        if( errno != EWOULDBLOCK) perror("accept");
                        break;
                    }

                    // Just to print some diagnostic
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
                    int s = getnameinfo(&in_addr, in_len, hbuf,
                            NI_MAXHOST, sbuf, NI_MAXSERV,
                            NI_NUMERICHOST | NI_NUMERICSERV);
                    if(s == 0)
                        printf( "process %d got conn(%d):host=%s, port=%s",
                                instance, infd, hbuf, sbuf);
                    
                    // Make the incoming socket non-blocking
                    // and add it to the same eventfd to monitor
                    if(-1 == fcntl(infd, F_SETFL,
                            fcntl(sd, F_GETFL, 0) | O_NONBLOCK)) {
                        perror("fcntl");
                        exit(1);
                    }
                    
                    event.data.fd = infd;
                    event.evetns = EPOLLIN | EPOLLET;
                    if( -1 == epoll_ctl( eventFD, EPOLL_CTL_ADD, infd, &event)) {
                        perror( "epoll_ctl");
                        exit(1);
                    }
                }
                continue;
            }
            // remember, we are not handling the incoming messages
            // on individual sockets. This means that this can't be
            // verbatim used for implementing a client-server comm.
            else {
                // pretend to work
                poll(0,0,100);
            }
        }
    }
    free (events);
    close(sd);
}

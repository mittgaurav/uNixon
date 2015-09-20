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

// Connect and send a message to server.
void connectAndSend()
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

    // connect to server
    if( connect(sd, (struct sockaddr*)*addr, sizeof(addr)) != 0 ) {
        perror("Connect");
        exit(1);
    }

    // keep writing.
    while( true ) {
        if(send(sd,"This is not a drill", 20, 0) < 0) {
            perror("send");
            exit(1);
        }

        sleep(5);
    }
}

// Allow clients to connect and then disconnect
int main()
{
    for( int i = 0; i < 1600; ++i ) {
        // sleep for 100 ms
        poll(0,0,100);

        pid_t pid = fork();
        if( -1 == pid ) {
            perror( "fork" );
            exit( 1 );
        }

        // child: child doesn't fork. We create a total of
        // hundred children, all sending on same socket.
        if( 0 == pid ) {
            connectAndSend();
            break;
        }
        
    }
}

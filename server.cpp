#include <stdio.h> //standard i/O header
#include <sys/socket.h> 
#include <netinet/in.h>
#include <errno.h> //global variable the kernel use to store the last error
#include <stdlib.h>


static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

int main() {

            //socket(domain , conn-type , protocol)
    int fd = socket(AF_INET , SOCK_STREAM , 0);
    
    /*
        1.AF_INET is for IPv4. Use AF_INET6 for IPv6 or dual-stack sockets.
        2.SOCK_STREAM is for TCP. Use SOCK_DGRAM for UDP.
        3.The 3rd argument is 0 and useless for our purposes.
     */
    int val = 1;
    //int setsockopt(int sockfd, int level, int optname,const void optval[.optlen], socklen_t optlen);
    setsockopt(fd ,SOL_SOCKET, SO_REUSEADDR , &val, sizeof(val));

    struct sockaddr_in addr = {}; //initiazlise a sockadder_in sturcture
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234); //htons converts the little endian representation to big endian 
    addr.sin_addr.s_addr = htonl(0); // wildcard IP 0.0.0.0
    
    int rv = bind(fd , (const struct sockaddr *)&addr , sizeof(addr));

    if(rv) {
        die("bind()");
    }

    //listen
    rv = listen(fd , SOMAXCONN);
    if(rv) {
        die("listen()");
    }

    //accept the incomming request
    

}
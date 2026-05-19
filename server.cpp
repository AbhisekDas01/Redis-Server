#include <iostream> //standard i/O header
#include <sys/socket.h> 
#include <netinet/in.h>
#include <unistd.h> //used to handle file descriptors (read , write , close)
#include <cerrno> //global variable the kernel use to store the last error
#include <cstdlib>
#include <cstring>



static void msg(const char *msg) {
    std::cerr << msg << std::endl;
}

static void die(const char *msg) {
    int err = errno;
    std::cerr << "[" << err << "] " << msg << std::endl; /*File Print Format: Unlike standard printf (which always prints to the screen), fprintf allows you to specify exactly where the text should be sent (to a file, a stream, or the screen).*/
    abort();
}

static void doSomething(int connfd) {
    char rbuf[64] = {};

    /*ssize_t = signed 64bit int(0 -> INT_MAX) + -1 (maximum network function returns -1 as a error code)*/
    ssize_t n = read(connfd, rbuf , sizeof(rbuf)-1); //read return int value > 0 and -1 for errors
    if(n < 0) {
        msg("read() error");
        return;
    }

    std::cout << "Client says: " << rbuf << std::endl;

    const char* wbuf = "world\n";

    n = write(connfd , wbuf , strlen(wbuf));
    if (n < 0) {
        die("write() error"); 
    }
}

int main() {

            //socket(domain , conn-type , protocol)
    int fd = socket(AF_INET , SOCK_STREAM , 0);
     if (fd < 0) {
        die("socket()");
    }
    
    /*
        1.AF_INET is for IPv4. Use AF_INET6 for IPv6 or dual-stack sockets.
        2.SOCK_STREAM is for TCP. Use SOCK_DGRAM for UDP.
        3.The 3rd argument is 0 and useless for our purposes.
     */
    int val = 1;
    //int setsockopt(int sockfd, int level, int optname,const void optval[.optlen], socklen_t optlen);
    setsockopt(fd ,SOL_SOCKET, SO_REUSEADDR , &val, sizeof(val));

    //bind
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

    std::cout << "Server is listening on port 1234..." << std::endl;

    //accept the incomming request
    while(true) {

        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(fd , (struct sockaddr *)& client_addr , &addrlen);

        if(connfd < 0) {
            continue; //error 
        }

        doSomething(connfd);

        close(connfd);
    }
    return 0;

}
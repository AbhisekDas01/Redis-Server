#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>


static void die(const char *msg) {
    int err = errno;
    std::cerr << "[" << err << "] " << msg << std::endl; /*File Print Format: Unlike standard printf (which always prints to the screen), fprintf allows you to specify exactly where the text should be sent (to a file, a stream, or the screen).*/
    abort();
}


int main(){
    
    int fd = socket(AF_INET , SOCK_STREAM , 0);
    if(fd < 0) {
        die("Socket()");
    }

    //binding
    struct sockaddr_in addr = {}; //initiazlise a sockadder_in sturcture
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234); //htons converts the big endian  representation to little endian
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);

    int rv = connect(fd , (const struct sockaddr *) & addr , sizeof(addr));
    if(rv) {
        die("Connect()");
    }

    //send message
    const char *msg = "Hello";
    ssize_t n = write(fd, msg, strlen(msg));

    if (n < 0) {
        die("write() error"); 
    }

    char rbuf[64] = {};

    n = read(fd , rbuf , sizeof(rbuf) -1 );
    if(n < 0) {
        die("Read");
    }

    std::cout << "Server says : " << rbuf << std::endl;
    close(fd);

    return 0;
}
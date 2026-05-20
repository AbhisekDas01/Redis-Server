#include <iostream> //standard i/O header
#include <sys/socket.h> 
#include <netinet/in.h>
#include <unistd.h> //used to handle file descriptors (read , write , close)
#include <cerrno> //global variable the kernel use to store the last error
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string_view>

const size_t k_max_msg = 4096; //specify maximum message length



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

/*When you call read(fd, buffer, 1000), you are asking the operating system for 1,000 bytes.
Most beginners assume the OS will either give them exactly 1,000 bytes or return an error.

This is entirely false. TCP does not preserve message boundaries.
If the internet is being slow, the kernel might only have 400 bytes sitting in its network card buffer right now. It will hand you those 400 bytes and return the number 400.

If you write your code like the "Bad example!", your server expects 1,000 bytes, gets 400, but immediately tries to read the message as if it were complete. The remaining 600 bytes will arrive a millisecond later, but your server has already moved on, causing the data stream to become completely corrupted.*/

static int32_t readFull(int fd , char *buf , size_t n) {

    while (n > 0){
        ssize_t rv = read(fd , buf , n);

        if(rv <= 0) {
            return -1; // -1 error, or  0 unexpected EOF
        }

        assert((size_t)rv <= n); /*If the condition is TRUE: The program assumes everything is fine and quietly moves on to the next line of code. There is zero disruption.

        If the condition is FALSE: The program instantly halts, prints the exact file name and line number to the screen, and crashes right there on the spot.*/
        n -= (size_t)rv;
        buf += rv; //move pointer to read from new slots
        
    }
    return 0; 
}

static int32_t writeFull(int fd ,const char *buf , size_t n) {
    while(n > 0) {

        ssize_t rv = write(fd , buf , n);

        if(rv <= 0) {
            return -1;
        }

        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv; //move pointer forward to write new places;

    }
    return 0;
}


static int32_t one_request(int connfd) {

    //4 byte header reserved for length of the messae
    char rbuf[4 + k_max_msg];
    errno = 0; //set the universal error to zero

    //get the size of the recieved message from the first 4 bytes and store it to rbuf
    int32_t err = readFull(connfd , rbuf , 4);
    if(err) { //err = 0 || -1
        msg(errno == 0 ? "EOF" : "read() Error");
        return err;
    }

    uint32_t len = 0;
    memcpy(&len , rbuf , 4); //copy the stored len from 1st 4bytes from rbuf -> len;
    
    if(len > k_max_msg) {
        msg("Too Long Message!");
        return -1;
    }

    err = readFull(connfd , &rbuf[4] , len); //read the message of len ans insert it to the rbuf from 4th index
    if(err) {
        msg("read() Error");
        return err;
    }

    //do somthing
    std::cout << "Client Says: " << std::string_view(&rbuf[4] , len) << std::endl; //print the message from index 4

    //send replay using the same protocol
    const char reply[] = "World";
    char wbuf[4+sizeof(reply)];

    len = (uint32_t)strlen(reply);

    //form the message in the wbuf
    memcpy(wbuf , &len , 4);
    memcpy(&wbuf[4] , reply , len);
    return writeFull(connfd , wbuf , 4+len);   
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

        // doSomething(connfd);

        /*Serve All the messages from one client before closing the connection and switching to other*/
        while(true) {
            int32_t err = one_request(connfd);
            if(err) {
                break;
            }
        }

        close(connfd);
    }
    return 0;

}
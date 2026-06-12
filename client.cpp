#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <cerrno> //global variable the kernel use to store the last error
#include <vector>

const size_t k_max_msg = 4096; //specify maximum message length

static void die(const char *msg)
{
    int err = errno;
    std::cerr << "[" << err << "] " << msg << std::endl; /*File Print Format: Unlike standard printf (which always prints to the screen), fprintf allows you to specify exactly where the text should be sent (to a file, a stream, or the screen).*/
    abort();
}

static void msg(const char *msg)
{
    std::cerr << msg << std::endl;
}

static int32_t readFull(int fd, char *buf, size_t n)
{

    while (n > 0)
    {
        ssize_t rv = read(fd, buf, n);

        if (rv <= 0)
        {
            if (errno == EINTR)
            { //If we were interrupted by a signal, do NOT exit!
                continue;
            }
            return -1; // -1 error, or  0 unexpected EOF
        }

        assert((size_t)rv <= n); /*If the condition is TRUE: The program assumes everything is fine and quietly moves on to the next line of code. There is zero disruption.

        If the condition is FALSE: The program instantly halts, prints the exact file name and line number to the screen, and crashes right there on the spot.*/
        n -= (size_t)rv;
        buf += rv; //move pointer to read from new slots
    }
    return 0;
}

static int32_t writeFull(int fd, const char *buf, size_t n)
{
    while (n > 0)
    {

        ssize_t rv = write(fd, buf, n);

        if (rv <= 0)
        {
            return -1;
        }

        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv; //move pointer forward to write new places;
    }
    return 0;
}

//data format
//Tlen = total size of the message
//nstr-> number of packets
//len->size of each string
// +------+------+-----+------+-----+------+-----+-----+------+
// | Tlen | nstr | len | str1 | len | str2 | ... | len | strn |
// +------+------+-----+------+-----+------+-----+-----+------+
static int32_t sendReq(int fd, const std::vector<std::string> &cmd)
{

    //calculate the total length of the message
    uint32_t len = 4; //nstr(count)

    for (const std::string &s : cmd)
    {
        len += 4 + s.size(); //len + str (for each message)
    }

    if (len > k_max_msg)
    {
        return -1;
    }

    char wbuf[4 + k_max_msg]; //create a temporary buffer to store the data

    memcpy(&wbuf[0], &len, 4); //insert the total length of the message
    uint32_t n = (uint32_t)cmd.size();
    memcpy(&wbuf[4], &n, 4); //total parameters (nstr)

    size_t curr = 8; //the current position in the buffer is 8
    for (const std::string &s : cmd)
    {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[curr], &p, 4);                  //length of the current message
        memcpy(&wbuf[curr + 4], s.data(), s.size()); //store the message
        curr += 4 + s.size();
    }

    return writeFull(fd, wbuf, 4 + len);
}

enum
{
    TAG_NIL = 0, // nil
    TAG_ERR = 1, // error code + msg
    TAG_STR = 2, // string
    TAG_INT = 3, // int64
    TAG_DBL = 4, // double
    TAG_ARR = 5, // array
};

static int32_t printResponse(uint8_t *data, size_t size)
{

    switch (data[0])
    {
    case TAG_NIL:
        printf("(NIL)\n");
        return 1;
    case TAG_ERR:
        if (size < 1 + 8)
        {
            msg("bad Response");
            return -1;
        }
        {
            int32_t code = 0;
            uint32_t len = 0;
            memcpy(&code, &data[1], 4); //extract the error code
            memcpy(&len, &data[5], 4);  //extract the length of the error message
            if (size < 1 + 8 + len)
            {
                msg("bad response");
                return -1;
            }

            /*  [%.*s] = our response data are not terminated by the null character (\0) so out standard %s
                will not know where to stop printing so it will print the garbage value , but the [%.*s] takes twe values [1.length , 2.str] tells that print exactly len characters from str*/
            printf("(err) %d %.*s\n", code, len, &data[1 + 8]);
            return 1 + 8 + len;
        }
    case TAG_STR:
        if (size < 1 + 4)
        {
            msg("bad Response");
            return -1;
        }
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);
            if (size < 1 + 4 + len)
            {
                msg("bad Response");
                return -1;
            }
            printf("(str) %.*s\n", len, &data[1 + 4]);
            return 1 + 4 + len;
        }
    case TAG_INT:
        if (size < 1 + 8)
        {
            msg("bad Response");
            return -1;
        }
        {
            int64_t val = 0;
            memcpy(&val, &data[1], 8);
            printf("(int) %ld\n", val);
            return 1 + 8;
        }
    case TAG_DBL:
        if (size < 1 + 8)
        {
            msg("bad Response");
            return -1;
        }
        {
            double val = 0;
            memcpy(&val, &data[1], 8);
            printf("(dbl) %g\n", val);
            return 1 + 8;
        }
    case TAG_ARR:
        if (size < 1 + 4)
        {
            msg("bad response");
            return -1;
        }
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);
            printf("(arr) len=%d\n", len);
            size_t arrayBytes = 1 + 4;
            for (uint32_t i = 0; i < len; i++)
            {
                ssize_t rv = printResponse(&data[arrayBytes], size - arrayBytes);
                if (rv < 0)
                {
                    return rv;
                }
                arrayBytes += (size_t)rv;
            }
            printf("(arr) end\n");
            return (int32_t)arrayBytes;
        }
    default:
        msg("bad response");
        return -1;
    }
}

//response format
//        array   2    int         str    3
// ┌─────┌─────┬─────┬─────┬─────┬─────┬─────┬─────┐
// │ n   │ tag │ len │ tag │ 123 │ tag │ len │ foo │
// └─────└─────┴─────┴─────┴─────┴─────┴─────┴─────┘
// n = total length of the message

static int32_t readRes(int fd)
{

    char rbuf[4 + k_max_msg]; //temporary buffer to recieve the response
    errno = 0;
    int32_t err = readFull(fd, rbuf, 4); //get the message length

    if (err)
    {
        if (errno == 0)
        {
            msg("EOF");
        }
        else
        {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;

    memcpy(&len, rbuf, 4);
    if (len > k_max_msg)
    {
        msg("Too long");
        return -1;
    }

    //read the total message
    err = readFull(fd, &rbuf[4], len);

    if (err)
    {
        msg("read() error");
        return err;
    }

    //print the response
    int32_t rv = printResponse((uint8_t *)&rbuf[4], len);
    if (rv > 0 && (uint32_t)rv != len)
    {
        msg("Bad Response");
        rv = -1;
    }
    return rv;
}

int main(int argc, char **argv)
{

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        die("Socket()");
    }

    //binding
    struct sockaddr_in addr = {}; //initiazlise a sockadder_in sturcture
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);                   //htons converts the big endian  representation to little endian
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); //INADDR_LOOPBACK = 127.0.0.1(localhost)

    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv)
    {
        die("Connect()");
    }

    std::vector<std::string> cmd;
    for (int i = 1; i < argc; i++)
    { //read the input from the cmd
        cmd.push_back(argv[i]);
    }

    int32_t err = sendReq(fd, cmd);
    if (err)
    {
        goto L_DONE;
    }

    err = readRes(fd);
    if (err)
    {
        goto L_DONE;
    }

L_DONE:
    close(fd);
    return 0;
}
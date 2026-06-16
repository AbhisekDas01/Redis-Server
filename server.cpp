//stdlib
#include <iostream> //standard i/O header
#include <cerrno> //global variable the kernel use to store the last error
#include <cstdlib>
#include <cstring>

//system
#include <sys/socket.h> 
#include <netinet/in.h>
#include <unistd.h> //used to handle file descriptors (read , write , close)
#include <cassert>
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

//STL
#include <vector>

#include "common.h"
#include "helpers/helper.h"
#include "list/list.h" //used to implement the doubly LL for the standard timout for the connections
#include "heap/heap.h"

//hashmap
#include "hashtable/hashtable.h"
#include "zset/zset.h"

//to use the multithreading
#include "threadPool/threadPool.h"


static void msg(const char *msg) {
    std::cerr << msg << std::endl;
}

static void msg_errno(const char *msg) {
   std::cerr << "[Error No: " << errno << "] : " << msg << std::endl;
}

static void die(const char *msg) {
    int err = errno;
    std::cerr << "[" << err << "] " << msg << std::endl; /*File Print Format: Unlike standard printf (which always prints to the screen), fprintf allows you to specify exactly where the text should be sent (to a file, a stream, or the screen).*/
    abort();
}

// struct timespec {
//     time_t     tv_sec;   /* Seconds */
//     long       tv_nsec;  /* Nanoseconds [0, 999'999'999] */
// };
//function to get the time in miliseconds 
static uint64_t getMonotonicMsec() {
    struct timespec tv = {0 , 0};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return uint64_t(tv.tv_sec) * 1000 + tv.tv_nsec/ 1000000; //convert to ms
}

static volatile sig_atomic_t g_stop = 0;
static void handle_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

//set the file descriptors in non blocking mode
static void fdSetNonBlock(int fd) {
    errno = 0;
    int flags = fcntl(fd , F_GETFL , 0);

    if(errno) {
        die("Fcntl Error");
        return;
    }

    flags = flags | O_NONBLOCK; //add the nonblock option using the bit masking

    errno = 0;
    (void)fcntl(fd , F_SETFL , flags);
        if(errno) {
        die("Fcntl Error");
    }
}




//Data structure to store the state information of each connection to use in the future
struct Conn {
    int fd = -1; 

    //to Store the application's intention for this event loop
    bool wantRead = false;
    bool wantWrite = false;
    bool wantClose = false; 

    //buffered input and output
    Buffer incoming; 
    Buffer outgoing; 

    //timer
    uint64_t lastActiveMs = 0;
    uint64_t ioStartMs = 0;

    DList idleNode; //to track the idle time 
    DList ioNode; //to track the the time out during transmission

};



const size_t k_max_args = 200 * 1000;

//data format
//nstr-> number of packets
//len->size of each string
// +------+-----+------+-----+------+-----+-----+------+
// | nstr | len | str1 | len | str2 | ... | len | strn |
// +------+-----+------+-----+------+-----+-----+------+
static int32_t parseRequest(const uint8_t *data , size_t size , std::vector<std::string> &out) {
    const uint8_t *end = data + size;
    uint32_t nstr = 0;
    if(!readU32(data , end , nstr)) { //get the number of messages
        return -1;
    }

    if(nstr > k_max_args) {
        return -1; // Limit exceed
    }

    //now decode every message packet
    while(out.size() < nstr) {

        //1.find the len of each message
        uint32_t len = 0;
        if(!readU32(data , end , len)) {
            return -1;
        }
        //2.parse the message of that length
        out.push_back(std::string()); //insert an empty string to the back 
        if(!readStr(data , end , len ,  out.back())) {
            return -1;
        }
    }

    if(data != end) {
        return -1; //data contains garbage data
    }
    return 0;
}


// global states hashmap
static struct {
    HMap db; //top-level hashtable

     //1.Store all the connection fds in the conn container using an vector with fd as the index number
    std::vector<Conn *> fd2conn;

    // timers for idle connections , read and write
    DList idleList;    // list head
    DList ioList;

    //for TTL
    std::vector<HeapItem> heap;

    //the thread pool
    ThreadPool threadPool;
} gData;

enum {
    T_INIT  = 0,
    T_STR   = 1,    // string
    T_ZSET  = 2,    // sorted set
};


// KV pair for the top-level hashtable
struct Entry {
    struct HNode node;  // hashtable node
    std::string key;

     // for TTL
    size_t heap_idx = -1;   // array index to the heap item
    // value
    uint32_t type = 0;
    // one of the following
    std::string str;
    ZSet zset;
};



//dummy node to check find the the key in the hashmap
struct LookupKey {
    struct HNode node;
    std::string key;
};

static void entrySetTTL(Entry *ent , int64_t ttl_ms);
static bool hnode_same(HNode *node , HNode *key);

static Entry *entryNew(uint32_t type) {
    Entry *ent = new Entry();
    ent->type = type;
    return ent;
}

//function to delete the zset sunchronously
static void entryDelSync(Entry *ent) {
    if(ent->type == T_ZSET) {
        zsetClear(&ent->zset);
    }
    delete ent;
}

static void entryDelFun(void *arg) { //exaction function signature for the thread pool

    entryDelSync((Entry *) arg);
}


static void entryDel(Entry *ent) {

    //unlink from any data structure 
    entrySetTTL(ent , -1); // remove from the heap data structure
    size_t size = (ent->type == T_ZSET) ? hmSize(&ent->zset.hmap) : 0;
    const size_t k_large_container_size = 1000;

    if(size > k_large_container_size) { //use background thread to do the job to avoid blockage
        threadPoolQueue(&gData.threadPool , &entryDelFun , ent);
    } else {
        entryDelSync(ent); // small; avoid context switches
    }
}

//function to check the match for the keys
static bool entryEq(HNode *lhs , HNode *rhs) {
    struct Entry *le = container_of(lhs , struct Entry , node);
    struct LookupKey *re = container_of(rhs , struct LookupKey , node);
    return le->key == re->key;
}



/**
 * 
 * REDIS FUNCTIONS (GET , SET , DEL)
 */
// GET key
static void doGet(std::vector<std::string> &cmd , Buffer &out) {

    //a dummy node for the lookup in the global hashmap
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hcode = strHash((uint8_t*)key.key.data() , key.key.size());
    
    // hashtable lookup
    HNode *hnode = hmLookup(&gData.db , &key.node , &entryEq);
    if(!hnode) { //if not found the entry
        return outNil(out);
    }

    //extract the data
    Entry *ent = container_of(hnode , Entry , node);
    if(ent->type != T_STR) { //if the data is not a normal string
        return outErr(out , ERR_BAD_TYP , "Not a String type");
    }
    return outStr(out , ent->str.data() , ent->str.size());
}

//SET function
static void doSet(std::vector<std::string> &cmd , Buffer &out) {
    // SET key value
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hcode = strHash((uint8_t*)key.key.data() , key.key.size());

    // hashtable lookup
    HNode *hnode = hmLookup(&gData.db , &key.node , &entryEq);
    Entry *ent = NULL;
    if(hnode) { //if data already exist the update the value 
        
        ent = container_of(hnode , Entry , node);
        if(ent->type != T_STR) {
            return outErr(out , ERR_BAD_TYP , "a non-string value exists");
        }
    } else {
        //create a new entry and insert into the global hashtable
        ent = entryNew(T_STR);
        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        hmInsert(&gData.db , &ent->node);
    }

    ent->str.swap(cmd[2]);
    return outNil(out);
}

static void doDel(std::vector<std::string> &cmd , Buffer &out) {

    //DEL key
    // SET key value
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hcode = strHash((uint8_t*)key.key.data() , key.key.size());

    // hashtable lookup
    HNode *hnode = hmDelete(&gData.db , &key.node , &entryEq);

    if(hnode) {
        entryDel(container_of(hnode , Entry , node));
    }
    return outInt(out , hnode? 1 : 0);
}

static bool cbKeys(HNode *node , void *arg) {

    Buffer &out = *(Buffer *)arg;
    const std::string &key = container_of(node , Entry , node)->key;
    outStr(out , key.data() , key.size());
    return true;
}

static void doKeys(std::vector<std::string> & , Buffer &out) {
    outArr(out , (uint32_t)hmSize(&gData.db));
    hmForeach(&gData.db , &cbKeys , (void *)&out);
}

static const ZSet k_empty_zset;

//function to extract the zset from the name
static ZSet *exceptZset(std::string &s) {

    LookupKey key;
    key.key.swap(s);
    key.node.hcode = strHash((uint8_t *)key.key.data() , key.key.size());
    //find the actual node using the dummy node
    HNode *hnode = hmLookup(&gData.db , &key.node , &entryEq);
    if(!hnode) {
        return (ZSet *)&k_empty_zset; //if the node not found means the zset is not created then create a empty zset and return
    }
    Entry *ent = container_of(hnode , Entry , node);
    return ent->type == T_ZSET ? &ent->zset : NULL;
}

//zadd key score name
static void doZadd(std::vector<std::string> &cmd , Buffer &out) {

    std::string &name = cmd[3];
    double score = 0;
    if(!str2dbl(cmd[2] , score)) {
        return outErr(out , ERR_BAD_ARG , "Expect a number");
    }

    //get the zset
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hcode = strHash((uint8_t *)key.key.data() , key.key.size());
    HNode *hnode = hmLookup(&gData.db , &key.node, &entryEq);
    
    Entry *ent = NULL;
    if(!hnode) { //if the key do not exists then create one 
        ent = entryNew(T_ZSET);
        ent->key.swap(key.key);
        ent->node.hcode = strHash((uint8_t *)ent->key.data() , ent->key.size());
        hmInsert(&gData.db , &ent->node); //insert the zset collection reference to the global hashmap
    } else { //if node found then extract its Entry

        ent = container_of(hnode , Entry , node);
        if(ent->type != T_ZSET) {
            return outErr(out , ERR_BAD_TYP , "Expect a ZSET");
        }

    }

    bool added = zsetInsert(&ent->zset , name.data() , name.size() , score );
    return outInt(out , (uint64_t)added);
}

static void doZquery(std::vector<std::string> &cmd , Buffer &out) {
    // parse the arguments and lookup the KV pair
    //ZQUERY key score name offset limit
    double score = 0;
    if(!str2dbl(cmd[2] , score)) {
        return outErr(out, ERR_BAD_ARG, "Expect FP number");
    }

    std::string &name = cmd[3];

    int64_t offset = 0 , limit = 0;
    if(!str2int(cmd[4] , offset) || !str2int(cmd[5] , limit)) {
        return outErr(out , ERR_BAD_ARG , "Expect INT");
    }

    //get ZSet 
    ZSet *zset = exceptZset(cmd[1]); //find the proper zset from the key
    if(!zset) {
        return outErr(out , ERR_BAD_TYP , "Expect zset");
    }

    //seek to key
    if(limit < 0) {
        return outArr(out , 0);
    } 
    ZNode *znode = zsetSeekge(zset , score , name.data() , name.size());

    //2. find the offset
    znode = znodeOffset(znode , offset);

    //3. iterate and output
    size_t ctx = outBeginArr(out);
    int64_t n = 0;
    int64_t count = 0;

    while(znode && count < limit) {
        outStr(out , znode->name , znode->len);
        outDbl(out , znode->score);
        znode = znodeOffset(znode , +1);
        n+=2; //2 items inserted
        count++;
    }
    outEndArr(out , ctx , n);
}

//Function to delete a node from the zset
//zrem key name
static void doZrem(std::vector<std::string> &cmd , Buffer &out) {

    ZSet *zset = exceptZset(cmd[1]);
    if(!zset) {
        return outErr(out , ERR_BAD_TYP , "Expect zset");
    }

    std::string &name = cmd[2];

    ZNode *znode = zsetLookup(zset , name.data() , name.size());

    if(znode) {
        zsetDelete(zset , znode);
    }
    return outInt(out , znode ? 1 : 0);
}

// zscore key name
static void doZscore(std::vector<std::string> &cmd , Buffer &out) {

    ZSet *zset = exceptZset(cmd[1]);
    if(!zset) {
        return outErr(out , ERR_BAD_TYP , "Expect zset");
    }   

    std::string &name = cmd[2];
    ZNode *znode = zsetLookup(zset , name.data() , name.size());

    return znode ? outDbl(out , znode->score) : outNil(out);
}

static void doZrank(std::vector<std::string> &cmd , Buffer &out) {

    ZSet *zset = exceptZset(cmd[1]);
    if(!zset) {
        return outErr(out , ERR_BAD_TYP , "Expect zset");
    } 
    std::string &name = cmd[2];

    int64_t rank = zsetRank(zset , name.data() , name.size());
    if(rank == -1) {
        return outNil(out);
    }
    outInt(out , rank);
}

static void doSetTTL(std::vector<std::string> &cmd , Buffer &out) {

    //parse the args
    int64_t ttlMs = 0;
    if(!str2int(cmd[2] , ttlMs)) {
        return outErr(out , ERR_BAD_ARG , "expect a int64");
    }

    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hcode = strHash((uint8_t *)key.key.data() , key.key.size());
    HNode *hnode = hmLookup(&gData.db , &key.node , &entryEq);

    //set ttl
    if(hnode) {
        Entry *ent = container_of(hnode , Entry , node);
        if (ttlMs <= 0) { //if the time is -ve then remove the key
            hmDelete(&gData.db , &ent->node , &hnode_same);//unlink the node from the global hashmap
            entryDel(ent); //send the data to be deleted
        } else {
            entrySetTTL(ent , ttlMs);
        }
    }
    return outInt(out , hnode ? 1 : 0);
}

static void doSeeTTL(std::vector<std::string> &cmd , Buffer &out) {

    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hcode = strHash((uint8_t*)key.key.data() , key.key.size());
    HNode *hnode = hmLookup(&gData.db , &key.node , &entryEq);

    if(!hnode) {
        return outInt(out , -2);
    }

    Entry *ent = container_of(hnode , Entry , node);
    if(ent->heap_idx == (size_t)-1) {
        return outInt(out , -1);
    }

    uint64_t nowMs = getMonotonicMsec();
    uint64_t expireMs = gData.heap[ent->heap_idx].val;
    if(expireMs <= nowMs) {
        return outInt(out , 0);
    }

    return outInt(out , (int64_t)(expireMs - nowMs));
}

// function to remove the ttl key to normal key
//PERSIST key
static void doSetPersist(std::vector<std::string> &cmd , Buffer &out) {

    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hcode = strHash((uint8_t*)key.key.data() , key.key.size());
    HNode *hnode = hmLookup(&gData.db , &key.node , &entryEq);

    int success = 0;
    if(hnode) {
        Entry *ent = container_of(hnode , Entry , node);

        if(ent->heap_idx != (size_t)-1) {
            heapDelete(gData.heap , ent->heap_idx);
            ent->heap_idx = -1;
            success = 1;
        }
    }

    return outInt(out , success);
}

static void doRequest(std::vector<std::string> &cmd , Buffer &out) {
    if (cmd.empty()) {
        return outErr(out, ERR_UNKNOWN, "Empty command!");
    }

    const std::string &action = cmd[0];

    // Standard Key-Value Commands
    
    // GET key: Retrieve the string value associated with a key
    if (action == "get") {
        if (cmd.size() != 2) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'get'");
        }
        doGet(cmd, out);
    // SET key value: Insert or update a string value for a key
    } else if (action == "set") {
        if (cmd.size() != 3) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'set'");
        }
        doSet(cmd, out);
    // DEL key: Delete a key-value entry from the database
    } else if (action == "del") {
        if (cmd.size() != 2) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'del'");
        }
        doDel(cmd, out);
    // KEYS: Retrieve and list all keys currently stored in the database
    } else if (action == "keys") {
        if (cmd.size() != 1) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'keys'");
        }
        doKeys(cmd, out);

    // Sorted Set (ZSET) Commands
    
    // ZADD key score name: Add or update a name with a double score in a sorted set
    } else if (action == "zadd") {
        if (cmd.size() != 4) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'zadd'");
        }
        doZadd(cmd, out);
    // ZREM key name: Remove a name from a sorted set
    } else if (action == "zrem") {
        if (cmd.size() != 3) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'zrem'");
        }
        doZrem(cmd, out);
    // ZSCORE key name: Get the score associated with a name in a sorted set
    } else if (action == "zscore") {
        if (cmd.size() != 3) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'zscore'");
        }
        doZscore(cmd, out);
    // ZQUERY key score name offset limit: Query sorted set entries starting >= (score, name) with offset and limit
    } else if (action == "zquery") {
        if (cmd.size() != 6) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'zquery'");
        }
        doZquery(cmd, out);
    } else if (action == "zrank") {
        if (cmd.size() != 3) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'zrank'");
        }
        doZrank(cmd , out);
    } else if (action == "pexpire") { 
        if (cmd.size() != 3) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'pexpire'");
        }
        doSetTTL(cmd , out);
    } else if (action == "pttl") {
        if (cmd.size() != 2) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'pttl'");
        }
        doSeeTTL(cmd , out);
    } else if (action == "persist") {
        if (cmd.size() != 2) {
            return outErr(out, ERR_UNKNOWN, "Wrong number of arguments for 'persist'");
        }
        doSetPersist(cmd , out);
    } else {
        outErr(out, ERR_UNKNOWN, "Unknown command!");
    }
}

static bool tryOneRequest(Conn *conn) {
    //parse the protocol message header
    if(conn->incoming.size() < 4) { //message size not recived so return false that there is more data ro read
        return false;
    }

    uint32_t len = 0;
    memcpy(&len , conn->incoming.data() , 4);

    if(len > k_max_msg) {
        msg("Too long message");
        conn->wantClose = true;
        return false; //want to close
    }

    //check for the recived message
    if(conn->incoming.size() < len + 4) { // the data is incomplete there to more data to be recieved 
        return false;
    }

    const uint8_t *request = &conn->incoming[4];

    std::vector<std::string> cmd;
    if(parseRequest(request , len , cmd) < 0) {
        conn->wantClose = true;
        return false;
    }

    size_t headerPos = 0;

    responseBegin(conn->outgoing , &headerPos);
    doRequest(cmd , conn->outgoing);
    responseEnd(conn->outgoing , headerPos);

    

     // application logic done! remove the request message.
     bufConsume(conn->incoming , (size_t)(4+len));

     return true;
}

static Conn * handleAccept(int fd) {

    //accpt the incomming request
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd , (struct sockaddr*)&client_addr , &addrlen);
    if(connfd < 0) {
        msg_errno("accept() error");
        return NULL;
    }

    //print the clier ip address
    uint32_t ip = client_addr.sin_addr.s_addr; //this is stored in the little endian form
    std::cerr << "New Client IP : " 
          << (ip & 255) << "." 
          << ((ip >> 8) & 255) << "." 
          << ((ip >> 16) & 255) << "." 
          << (ip >> 24) << ":" 
          << ntohs(client_addr.sin_port) << std::endl;

    //set the new connection to be non blocking
    fdSetNonBlock(connfd);

    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->wantRead = true; //at first the kernel should seek the data from the client 
    //add the time stamp 


    conn->lastActiveMs = getMonotonicMsec();
    dlistInit(&conn->ioNode); //to do a efficient read write (circular manner)
    dlistInit(&conn->idleNode);
    dlistInsertBefore(&gData.idleList , &conn->idleNode); //insert the node in back of the linked list
    return conn;
    
}

static void handleWrite(Conn *conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd , conn->outgoing.data() , (size_t)conn->outgoing.size());

    if(rv < 0 && errno == EAGAIN) {
        return; //NOT AN Error
    }
    if (rv < 0) {
        msg_errno("write() error");
        conn->wantClose = true;    // error handling
    }  
    
    //consume the buffer after read
    bufConsume(conn->outgoing , (size_t)rv);

    if(conn->outgoing.size() == 0) { //all data is consumed
        conn->wantRead = true;
        conn->wantWrite = false;
        return;
    }
}

static void handleRead(Conn *conn) {

    uint8_t buf[64*1024]; //Create a 64KB slot on stack to store the recived data temporarily
    ssize_t rv = read(conn->fd , buf , (size_t)sizeof(buf));

    //if the read syscall returned an error
    if(rv < 0 && errno == EAGAIN) {
        return; //Resource Temporarily Unavailable (Try Again).
    }

    if(rv < 0) {
        msg_errno("read() Error");
        conn->wantClose = true;
        return;
    }
    if(rv == 0) {
        if(conn->incoming.size() == 0) {
            msg("Client Closed");
        } else {
            msg("Unexpected EOF");
        }

        conn->wantClose = true;
        return;
    }

    //Store the recieved data in a persistent storage (conn->incoming) 
    bufAppend(conn->incoming , buf , (size_t)rv);
    
    

   
    // TCP Stream Parser: A fast client might bundle multiple requests into a single read().
    // This loop aggressively extracts and handles every complete message frame back-to-back 
    // until the buffer is empty or holds an incomplete packet, preventing pipeline deadlocks.
    while(tryOneRequest(conn)) {}

    //update readiness information
    if(conn->outgoing.size() > 0) { //has a respose with it
        conn->wantRead = false;
        conn->wantWrite = true;
        
        //if the app has its response ready then directly send it to the client without waiting for the next round to come
        return handleWrite(conn);
    } //else : wantread = true;


}

const uint64_t k_idle_timeout_ms = 5 * 1000; //maximu idle time 5sec
const uint64_t k_io_timeout_ms = 3 * 1000; //maximum 3sec


//function to find the remaining time of the 1st node (idle times are stored in the fifo order)
static int32_t nextTimerMs() {

    
    uint64_t nowMs = getMonotonicMsec(); // get current timestamp
    uint64_t maxTimeout = (uint64_t)-1; //to get the maximum value

    
    if(!dlistEmpty(&gData.idleList)) {
        Conn *connIdle = container_of(gData.idleList.next , Conn , idleNode);
        maxTimeout = connIdle->lastActiveMs + k_idle_timeout_ms;
    }
    if(!dlistEmpty(&gData.ioList)) {
        Conn *connIo = container_of(gData.ioList.next , Conn , ioNode);
        uint64_t nextIo = connIo->ioStartMs + k_io_timeout_ms;

        if(maxTimeout == 0 || nextIo < maxTimeout) {
            maxTimeout = nextIo; //means the io is expiring soon
        }
    }
    //check for teh expire timer
    if(!gData.heap.empty() && gData.heap[0].val < maxTimeout) {
        maxTimeout = gData.heap[0].val;
    }

    if(maxTimeout == (uint64_t)-1) { //if the value is unchanged then no timers
        return -1;
    }

    if(maxTimeout <= nowMs) { //If the earliest deadline has already passed, wake up immediately
        return 0;
    }
    
    return (int32_t)(maxTimeout - nowMs);
}

static void connDestroy(Conn *conn) {
    (void)close(conn->fd); //close the connection
    dlistDetach(&conn->idleNode); //detach the node from the linked list
    dlistDetach(&conn->ioNode);
    gData.fd2conn[conn->fd] = NULL;
    delete conn;
}

static bool hnode_same(HNode *node , HNode *key) {
    return node == key;
}



//function to remove the timed out connections
static void processTimers() {

    uint64_t nowMs = getMonotonicMsec(); //get current time stamp

    //1.check the idle list for removal
    while(!dlistEmpty(&gData.idleList)) {
        
        Conn *conn = container_of(gData.idleList.next , Conn , idleNode);
        
        uint64_t maxTimeout = conn->lastActiveMs + k_idle_timeout_ms; //5 sec idle time

        if(maxTimeout <= nowMs) { //if the idle time passed remove the connection
            fprintf(stderr, "removing idle connection: %d\n", conn->fd);
            connDestroy(conn);
        } else {
            break;
        }

    }
    //check the io list for removal
    while(!dlistEmpty(&gData.ioList)) {
        
        Conn *conn = container_of(gData.ioList.next , Conn , ioNode);
        
        uint64_t maxTimeout = conn->ioStartMs + k_io_timeout_ms; //5 sec idle time

        if(maxTimeout <= nowMs) { //if the idle time passed remove the connection
            fprintf(stderr, "removing io interrupt connection: %d\n", conn->fd);
            connDestroy(conn);
        } else {
            break;
        }

    }


    //remove the TTL
    const size_t k_max_works = 2000; //these are the max number of ttl expire work at a single iteration
    size_t nworks = 0; 

    std::vector<HeapItem> &heap = gData.heap;
    while (!heap.empty() && heap[0].val < nowMs && nworks++ < k_max_works) {
        Entry *ent = container_of(heap[0].ref , Entry , heap_idx);
        //delete the node for the storage
        hmDelete(&gData.db , &ent->node , &hnode_same);
        entryDel(ent);
    }
}

static void connUpdateTimer(Conn *conn) {

    bool hasBuffer = (conn->incoming.size() > 0) || ( conn->outgoing.size() > 0); //if the data is in the buffer means the client is in the io state so update the io timer
    if(hasBuffer) {

        dlistDetach(&conn->idleNode); //remove the node from the idle state
        dlistInit(&conn->idleNode); //reset the old pointers

        if(dlistEmpty(&conn->ioNode)) { //if the io operation just started the update the iovalue

            conn->ioStartMs = getMonotonicMsec();
            dlistInsertBefore(&gData.ioList , &conn->ioNode);

        }
    } else { //if the buffers are empty then the client should be in the idle condition 
        
        dlistDetach(&conn->ioNode);
        dlistInit(&conn->ioNode);

        conn->lastActiveMs = getMonotonicMsec();
        dlistDetach(&conn->idleNode); //remove from old pos
        dlistInsertBefore(&gData.idleList , &conn->idleNode);
    }
}

static void entrySetTTL(Entry *ent , int64_t ttl_ms) {

    if(ttl_ms < 0) {
        if (ent->heap_idx != (size_t)-1) {
            //seting a -ve timer means deleting the ttl
            heapDelete(gData.heap , ent->heap_idx);
            ent->heap_idx = -1;
        }
    } else { //if the ttlms is >= 0 then addnew or update exiting the data

        uint64_t expires_at = getMonotonicMsec() + (uint64_t)ttl_ms;
        HeapItem t = {(uint64_t)expires_at , &ent->heap_idx};
        heapUpsert(gData.heap , ent->heap_idx , t);
    }
}

int main() {
    // Register shutdown signals
    signal(SIGINT, handle_signal); //signal interrupt (Ctrl + C)
    signal(SIGTERM, handle_signal); //interrupt by the kill <PID>

    //initialise the threadpool
    threadPoolInit(&gData.threadPool , 4);

    //initialization of the list
    dlistInit(&gData.idleList);
    dlistInit(&gData.ioList);

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

     //set the listening socket (fd) as non block mode 
     fdSetNonBlock(fd);

    //listen
    rv = listen(fd , SOMAXCONN);
    if(rv) {
        die("listen()");
    }

    std::cout << "Server is listening on port 1234..." << std::endl;

    //start the event loop

   

    std::vector<struct pollfd> pollArgs; 
    /*struct pollfd {
        int fd;	-> reference to the file descriptor 		
        short int events; -> POLLIN , POLLOUT , POLLERR	
        short int revents; -> filled by the kernel
    }*/

    while(!g_stop) {
        pollArgs.clear(); //first clear the old connection in case any connection is destroyed or closed that should also be removed

        //insert the listening socket to the zero index 
        struct pollfd pfd = {fd , POLLIN , 0}; //(basically it tells the poll function to wake it when a new connection request comes)
        pollArgs.push_back(pfd);

        //insert all the client connection to the poll
        for(Conn *conn : gData.fd2conn) {

            if(!conn) {
                continue; 
            }

            struct pollfd pfd = {conn->fd , POLLERR , 0}; //POLLERR ->  Monitor the client socket for connection errors (broken pipes, crashes) by default 
            if(conn->wantRead) {
                pfd.events = pfd.events | POLLIN;
            }
            if(conn->wantWrite) {
                pfd.events |= POLLOUT;
            }
            pollArgs.push_back(pfd);
        }

        int32_t timeoutMs = nextTimerMs();
        //now wait for the readiness of the sockets 
        int rv = poll(pollArgs.data() , (nfds_t)pollArgs.size() , timeoutMs); // Tell the kernel to watch our sockets;  block indefinitely until an event occurs (Your thread goes to sleep here.)
        if(rv < 0 && errno == EINTR) { // EINTR = Interrupted System Call
            continue;
        }
        if(rv < 0) {
            die("POLL()");
        }

        /**if we reached this line means one or more sockets have given the signal that they want to perform something these may be 
         * 1. The main listening socket placed in index 0 which indicate that new connection came 
         * 2. The Client sockets want to do some (read /write ) on our application
         */
        
         //1. Check if new connection request came
         if(pollArgs[0].revents) {

            //accept the new connection and generate a Conn object
            Conn *conn = handleAccept(fd);

            if(conn != NULL) {

                //put the connection opj to the map
                if(gData.fd2conn.size() <= (size_t)conn->fd){
                    gData.fd2conn.resize((size_t)conn->fd+1);
                }
                assert(!gData.fd2conn[conn->fd]); //check that the slot is empty (means the slot is not allocated to any other active socket)
                gData.fd2conn[conn->fd] = conn;

            }
         }

         //2. Now handle the event from the client sockets
         for(size_t i = 1 ; i < pollArgs.size() ; i++) {
            uint32_t ready = pollArgs[i].revents;
            if(ready == 0) {
                continue;
            }

            Conn *conn = gData.fd2conn[pollArgs[i].fd];
            

            if(ready & POLLIN) {
                assert(conn->wantRead);
                handleRead(conn);
            }
            if(ready & POLLOUT) {
                assert(conn->wantWrite);
                handleWrite(conn);
            }

            //update the timer of idle or the io based on the current state of the conn
            if(!conn->wantClose) {
                connUpdateTimer(conn); //update the timers if the connections are still alive 
            }

            // close the socket from socket error or application logic
            if((ready & POLLERR) || conn->wantClose) {
                connDestroy(conn);
            }
        }

        //3. Closed all the timeouted connections 
        processTimers();

    }
    
    std::cout << "\nServer is shutting down..." << std::endl;
    threadPoolDestroy(&gData.threadPool);
    close(fd);

    return 0;
}

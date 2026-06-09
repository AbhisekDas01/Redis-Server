#pragma once

#include <cstdint> //used for uint64_t
#include <cstddef> //for NULL && size_t
#include <vector>
#include <string>

typedef std::vector<uint8_t> Buffer;

const size_t k_max_msg = 32 << 20;  // likely larger than the kernel buffer

// error code for TAG_ERR
enum {
    ERR_UNKNOWN = 1,    // unknown command
    ERR_TOO_BIG = 2,    // response too big
};

// data types of serialized data
enum {
    TAG_NIL = 0,    // nil
    TAG_ERR = 1,    // error code + msg
    TAG_STR = 2,    // string
    TAG_INT = 3,    // int64
    TAG_DBL = 4,    // double
    TAG_ARR = 5,    // array
};

//Helper function to help serialization


//helper functions to append the data
void bufAppend(Buffer &buf , const uint8_t *data , size_t size);
void bufAppendU8(Buffer &buf , uint8_t data);
void bufAppendU32(Buffer &buf , uint32_t data);
void bufAppendI64(Buffer &buf , int64_t data);
void bufAppendDbl(Buffer &buf , double data); 

//output functions
void outNil(Buffer &out);
void outStr(Buffer &out , const char *s , size_t size);
void outInt(Buffer &out , uint64_t val);
void outArr(Buffer &out , uint32_t n);
void outDbl(Buffer &out , double val);
void outErr(Buffer &out , uint32_t code , const std::string &msg);

//buffer consume helper
void bufConsume(Buffer &buf , size_t n);

//input deserialization helpers
bool readU32(const uint8_t*&curr , const uint8_t *end , uint32_t &out);
bool readStr(const uint8_t *&curr , const uint8_t *end , size_t len , std::string &out);

//response framing helpers
void responseBegin(Buffer &out , size_t *header);
size_t responseSize(Buffer &out , size_t header);
void responseEnd(Buffer &out , size_t header);
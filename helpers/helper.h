#pragma once

#include <cstdint> //used for uint64_t
#include <cstddef> //for NULL && size_t
#include <vector>
#include <string>

typedef std::vector<uint8_t> Buffer;

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
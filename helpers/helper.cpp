#include "helper.h"
#include <cstring>
#include <fcntl.h>
#include <cerrno>
#include <cstdlib>
#include <math.h>
#include <assert.h>


void bufAppend(Buffer &buf , const uint8_t *data , size_t size){
    buf.insert(buf.end() , data , data+size); 
    /**
     *  buf.end() -> pointer to the position after the last available data in the vector
     *  data is the starting address of your raw data.

        data + size uses pointer arithmetic to calculate the ending boundary. In C++, range boundaries are always exclusive (meaning it copies everything up to, but not including, the exact address of data + size).
     */
}

//Helper functions to generate the response
void bufAppendU8(Buffer &buf , uint8_t data) {
    buf.push_back(data);
}

void bufAppendU32(Buffer &buf , uint32_t data) {
    bufAppend(buf , (const uint8_t *)&data , 4); //insert the length in 4bytes
}

void bufAppendI64(Buffer &buf , int64_t data) {
    bufAppend(buf , (const uint8_t *)&data , 8);
}

void bufAppendDbl(Buffer &buf , double data){
    bufAppend(buf , (const uint8_t *)&data , 8);
}

/**Generate the nil response */
void outNil(Buffer &out) {
    bufAppendU8(out , TAG_NIL);
}

void outStr(Buffer &out , const char *s , size_t size) {
    bufAppendU8(out , TAG_STR); //insert the tag as string (1 byte)
    bufAppendU32(out , (uint32_t)size);  //push the size of the message (4bytes)
    bufAppend(out , (const uint8_t *)s , size);

}

//generate the int response
void outInt(Buffer &out , uint64_t val) {
    bufAppendU8(out , TAG_INT);
    bufAppendI64(out , val);
}

void outArr(Buffer &out , uint32_t n) {
    bufAppendU8(out , TAG_ARR); //Tag as array
    bufAppendU32(out , n); //len of the array
}

void outDbl(Buffer &out , double val) {
    bufAppendU8(out , TAG_DBL);
    bufAppendDbl(out , val);
}

void outErr(Buffer &out , uint32_t code , const std::string &msg) {
    bufAppendU8(out , TAG_ERR); //tag err
    bufAppendU32(out , code); //error code
    bufAppendU32(out , (uint32_t)msg.size()); //msg length
    bufAppend(out , (const uint8_t*)msg.data() , msg.size());
}

size_t outBeginArr(Buffer &out) {
    out.push_back(TAG_ARR);
    bufAppendU32(out , 0); // length of the array to be full filled by the end
    return out.size() - 4; //index to enter teh length
}

void outEndArr(Buffer &out , size_t ctx , uint32_t len) {
    assert(out[ctx-1] == TAG_ARR);
    memcpy(&out[ctx] , &len , 4);
}

//buffer consume helper
void bufConsume(Buffer &buf , size_t n){
    buf.erase(buf.begin() , buf.begin()+n);
}


bool readU32(const uint8_t*&curr , const uint8_t *end , uint32_t &out) {

    if(curr + 4 > end) {
        return false; //not enough data is recived get more data to parse
    }

    memcpy(&out , curr , 4);
    curr += 4; //move pointr forward to parse new data;
    return true;
}

bool readStr(const uint8_t *&curr , const uint8_t *end , size_t len , std::string &out) {

    if(curr + len > end) { //not enough data is available
        return false;
    }

    out.assign(curr , curr+len);
    curr += len;
    return true;
}

//function to reserve the spaces for the data in out buffer in the response
void responseBegin(Buffer &out , size_t *header) {
    *header = out.size(); //message header postion
    bufAppendU32(out , 0); //reserve space

}

size_t responseSize(Buffer &out , size_t header) {
    return out.size() - header - 4; //it returns exact length of the message 
}

void responseEnd(Buffer &out , size_t header) {
    size_t msgSize = responseSize(out , header);
    if(msgSize > k_max_msg) {
        out.resize(header+4);
        outErr(out , ERR_TOO_BIG , "Response is too big.");
        msgSize = responseSize(out , header); //get the error message size
    }

    uint32_t len = (uint32_t)msgSize;
    memcpy(&out[header] , &len , 4);
}


//string conversion functions
bool str2dbl(const std::string &s , double &out) {
    char *endp = NULL;
    out = strtod(s.c_str() , &endp);
    return endp == s.c_str() + s.size() && !isnan(out);
}

bool str2int(const std::string &s , double &out) {
    char *endp = NULL;
    out = strtoll(s.c_str(), &endp, 10);
    return endp == s.c_str() + s.size();
}
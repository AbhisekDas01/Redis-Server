#include "helper.h"


static void bufAppend(Buffer &buf , const uint8_t *data , size_t size){
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

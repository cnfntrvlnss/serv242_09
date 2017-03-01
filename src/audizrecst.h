/*************************************************************************
	> File Name: audizrecst.h
	> Author: 
	> Mail: 
	> Created Time: Thu 23 Feb 2017 03:42:08 AM EST
 ************************************************************************/

#ifndef _AUDIZRECST_H
#define _AUDIZRECST_H

#include <cstdint>
#include <vector>

#include "audizcomm.h"
typedef unsigned uint32_t;
//#include "ProjectBuffer.h"
#define AZ_RECCENTER "recCenter"
//#define AZ_LINKMSGLEN 64
#define AZ_RECLINKNAME "rec link"
#define AZ_RECLINKACK "server ok"

#define AZ_MSGTYPE_START 64
#define AZ_PUSH_PROJDATA AZ_MSGTYPE_START
#define AZ_NOTIFY_GETDATA (AZ_MSGTYPE_START + 1)
#define AZ_REPORT_RESULT (AZ_MSGTYPE_START + 2)

#define  AZ_MAGIC_8CHARS "\x07\x06\x05\x04\x03\x02\x01\x00"
struct RecLinkMsg_Head{
    char strMark[8];
    uint32_t type;
    uint32_t val;

    void pack_w(std::vector<AZ_PckVec> &pcks){
        pcks.push_back(AZ_PckVec(AZ_MAGIC_8CHARS, 8));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&type), sizeof(uint32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&val), sizeof(uint32_t)));
    }
    void pack_r(std::vector<AZ_PckVec> &pcks){
        pcks.push_back(AZ_PckVec(strMark, 8));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&type), sizeof(uint32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&val), sizeof(uint32_t)));
    }

    bool serialize(int fd);
    bool deserialize(int fd);
};

struct RecLinkDataUnit{
    void set(uint32_t id, uint32_t st, uint32_t len){
        ftokId = id;
        start = st;
        length = len;
    }
    uint32_t ftokId;
    uint32_t start;
    uint32_t length;
    void pack(std::vector<AZ_PckVec> &pcks){
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&ftokId), sizeof(uint32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&start), sizeof(uint32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&length), sizeof(uint32_t)));
    }
};




#endif

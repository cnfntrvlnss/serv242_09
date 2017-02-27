/*************************************************************************
	> File Name: audizserv_c.cpp
	> Author: 
	> Mail: 
	> Created Time: Thu 23 Feb 2017 03:45:08 AM EST
 ************************************************************************/

#include<iostream>

#include "../audizrecst.h"
#include "../apueclient.h"
#include "ProjectBuffer.h"
#include "globalfunc.h"

using namespace std;
using namespace audiz;

#define OUTPUT_ERROR "; error: "<< strerror(errno)

class RecSession: public ProjectConsumer{
public:
    
    int link;
    long clientId;

    
    bool sendProject(Project *proj);
    //have data to read.
    bool recvResponse();

};

bool RecSession::recvResponse()
{
    RecLinkMsg_Head head;
    vector<AZ_PckVec> pcks;
    head.pack_r(pcks);
    int err;
    readn(link, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "RecSession::recvResponse failed to read data from link. client: "<< clientId<< OUTPUT_ERROR);
        return false;
    }
    if(memcmp(head.strMark, AZ_MAGIC_8CHARS, 8) != 0){
        LOG4CPLUS_ERROR(g_logger, "RecSession::recvResponse read incorrect msg_head. client: "<< clientId<< OUTPUT_ERROR);
        return false;
    }
    if(head.type == AZ_NOTIFY_GETDATA){
    }
    else if(head.type == AZ_PUSH_RECRESULT){
        Audiz_Result res;
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&res), sizeof(res)));
        for(int idx=0; idx < head.val; idx++){
            readn(link, reinterpret_cast<PckVec*>(&pcks[0]), 1, &err, 0);
            if(err < 0){
                LOG4CPLUS_ERROR(g_logger, "RecSession::recvResponse fail to read result form client "<< clientId<< OUTPUT_ERROR);
                return false;
            }
            confirm(res.m_iPCBID, &res);
        }
    }
    else{
        LOG4CPLUS_ERROR(g_logger, "RecSession::recvResponse read msg_head having unrecognized type. client: "<< clientId << OUTPUT_ERROR);
        return false;
    }
}

bool RecSession::sendProject(Project *proj)
{
    vector<ShmSegment> data;
    proj->getData(data);
    RecLinkMsg_Head head;
    vector<AZ_PckVec> pcks;
    head.type = AZ_PUSH_PROJDATA;
    head.val = data.size();
    int err;
    writen(link, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "fail to write data head to link. client: "<< clientId<< "; error: "<< strerror(errno));   
        return false;
    }
    for(unsigned idx=0; idx < data.size(); idx++){
        RecLinkDataUnit unit;
        unit.pack(pcks);
        unit.set(data[idx].blk->ftokId, data[idx].blk->offset, data[idx].len);
        writen(link, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
        if(err < 0){
            LOG4CPLUS_ERROR(g_logger, "fail to write data unit "<<  idx<<" to link. client: "<< clientId<< "; error: "<< strerror(errno));   
            return false;
        }
    }
    return true;
}

/*
void run(){
    RecSession rec1, rec2;
    while(true){
        if(rec1.fail()){
            rec1.connect();
        }
        if(rec2.fail()){
            rec2.connect();
        }

        pollfd fds[2];
        fds[0].fd = rec1.link;
        fds[0].events = POLLIN;
        fds[1].fd = rec2.link;
        fds[1].events = POLLIN;
        int retp = poll(fds, 1, -1);
        if(retp > 0){
            res = readResult();
            que.confirm(proj, res);
        }
    }
}
*/

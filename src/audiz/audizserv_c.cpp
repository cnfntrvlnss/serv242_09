/*************************************************************************
	> File Name: audizserv_c.cpp
	> Author: 
	> Mail: 
	> Created Time: Thu 23 Feb 2017 03:45:08 AM EST
 ************************************************************************/

#include <poll.h>
#include<iostream>

#include "../audizrecst.h"
#include "../apueclient.h"
#include "ProjectBuffer.h"
#include "samplelib.h"
#include "globalfunc.h"

using namespace std;
using namespace audiz;

#define OUTPUT_ERROR "; error: "<< strerror(errno)

class RecSession: public ProjectConsumer, SampleConsumer{
public:
    RecSession(int fd, long procId):
        link(fd), clientId(procId)
    {
        smpType = 0;
        addStream(this);
    }
    ~RecSession(){
        if(smpType > 0){
            rmSmpConsumer(this);
        }
        removeStream(this);
    }
    int link;
    long clientId;
    unsigned smpType;
    //for serializing msg send to rec link.
    pthread_mutex_t mylock;
    void setSmpType(unsigned val){
        smpType = val;
        addSmpConsumer(this);
    }

    bool sendProject(Project *proj);
    //have data to read.
    bool recvResponse();

    bool parseSmpHead(const char *smpHead, int &t2){
        int t1, t3;
        if(sscanf(smpHead, "%d_%x_%d.", &t1, &t2, &t3) != 3){
            return false;
        }
        return false;
    }
    bool addOne(const char *smpHead);
    bool rmOne(const char *smpHead);
    void feedAll();
};

bool RecSession::addOne(const char *smpHead)
{
    int t2;
    if(parseSmpHead(smpHead, t2)){
        LOG4CPLUS_ERROR(g_logger, "RecSession::addOne cannot parse irregular head of sample. head: "<< smpHead);
        return false;
    }
    if(t2 == smpType){
        //send msg to client.
        RecLinkMsg_Head head;
        head.type = AZ_RECADD_SAMPLE;
        head.val = 1;
        vector<AZ_PckVec> pcks;
        head.pack_w(pcks);
        pcks.push_back(AZ_PckVec(const_cast<char*>(smpHead), SPKMDL_HDLEN));
        int err;
        pthread_mutex_lock(&mylock);
        writen(link, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
        pthread_mutex_unlock(&mylock);
        if(err < 0){
            LOG4CPLUS_ERROR(g_logger, "RecSession::addOne failed to write msg to reclink."<< OUTPUT_ERROR);
            return false;
        }
    }
    else{
        
    }
}

bool RecSession::rmOne(const char *smpHead)
{
    int t2;
    if(parseSmpHead(smpHead, t2)){
        LOG4CPLUS_ERROR(g_logger, "RecSession::rmOne cannot parse irregular head of sample. head: "<< smpHead);
        return false;
    }
    if(t2 == smpType){
        //send msg to client.
        RecLinkMsg_Head head;
        head.type = AZ_RECRM_SAMPLE;
        head.val = 1;
        vector<AZ_PckVec> pcks;
        head.pack_w(pcks);
        pcks.push_back(AZ_PckVec(const_cast<char*>(smpHead), SPKMDL_HDLEN));
        int err;
        pthread_mutex_lock(&mylock);
        writen(link, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
        pthread_mutex_unlock(&mylock);
        if(err < 0){
            LOG4CPLUS_ERROR(g_logger, "RecSession::rmOne failed to write msg to reclink."<< OUTPUT_ERROR);
            return false;
        }
    }
    else{
    }
}

void RecSession::feedAll()
{
    //send msg to client.
    RecLinkMsg_Head head;
    head.type = AZ_RECFEED_SAMPLES;
    head.val = 0;
    vector<AZ_PckVec> pcks;
    head.pack_w(pcks);
    int err;
    pthread_mutex_lock(&mylock);
    writen(link, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    pthread_mutex_unlock(&mylock);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "RecSession::feedAll failed to write msg to reclink."<< OUTPUT_ERROR);
        return ;
    }
}

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
        uint64_t pid;
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&pid), sizeof(uint64_t)));
        for(int idx=0; idx < head.val; idx++){
            readn(link, reinterpret_cast<PckVec*>(&pcks[0]), 1, &err, 0);
            if(err < 0){
                LOG4CPLUS_ERROR(g_logger, "RecSession::recvResponse while process az_notify_getdata fail to read project id from client "<< clientId<< OUTPUT_ERROR);
                return false;
            }
            confirm(pid, NULL);
        }
    }
    else if(head.type == AZ_REPORT_RESULT){
        Audiz_Result res;
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&res), sizeof(res)));
        for(int idx=0; idx < head.val; idx++){
            readn(link, reinterpret_cast<PckVec*>(&pcks[0]), 1, &err, 0);
            if(err < 0){
                LOG4CPLUS_ERROR(g_logger, "RecSession::recvResponse fail to read result form client "<< clientId<< OUTPUT_ERROR);
                return false;
            }
            LOG4CPLUS_INFO(g_logger, "RecSession::recvResponse read result from client "<< clientId<< " PID="<< res.m_iPCBID<<" TargetID="<< res.m_iTargetID);
            confirm(res.m_iPCBID, &res);
        }
    }
    else if(head.type == AZ_RECREQ_SAMPLES){
        setSmpType(head.val);
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
    head.pack_w(pcks);
    //pid follows msg head.
    pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&(proj->PID)), sizeof(uint64_t)));
    int err;
    writen(link, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "fail to write data head to link. client: "<< clientId<< "; error: "<< strerror(errno));   
        return false;
    }
    
    for(unsigned idx=0; idx < data.size(); idx++){
        RecLinkDataUnit unit;
        pcks.clear();
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

map<int, RecSession*> g_mRecSesses;
static void RecSess_add(int fd, long procId)
{
    assert(g_mRecSesses.find(fd) == g_mRecSesses.end());
    g_mRecSesses[fd] = new RecSession(fd, procId);
}

static inline void closeRecLink(int fd)
{
    assert(g_mRecSesses.find(fd) != g_mRecSesses.end());
    LOG4CPLUS_INFO(g_logger, "end rec link. fd: "<< fd<< "; clientId: "<< g_mRecSesses[fd]->clientId);
    delete g_mRecSesses[fd];
    g_mRecSesses.erase(fd);
    close(fd);
}

static bool procImplAcceptLink(int servfd)
{
    uid_t uid;
    long procid;
    int tmpfd = serv_accept(servfd, &uid);
    if(tmpfd < 0){
        LOG4CPLUS_ERROR(g_logger, "procImplAcceptLink serv_accept error. "<< OUTPUT_ERROR);
        return false;
    }
    Audiz_LinkResponse res;
    vector<AZ_PckVec> pcks;
    res.req.pack_r(pcks);
    int err;
    readn(tmpfd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "procImplAcceptLink fail to read first package of new rec link."<< OUTPUT_ERROR);
        goto err_exit;
    }
    if(strcmp(res.req.head, AZ_RECLINKNAME) == 0){
        LOG4CPLUS_DEBUG(g_logger, "procImplAcceptLink begin a new rec link. fd: "<< tmpfd);
        strcpy(res.ack, AZ_RECLINKACK);
        pcks.clear();
        res.pack_w(pcks);
        writen(tmpfd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
        if(err < 0){
            LOG4CPLUS_ERROR(g_logger, "procImplAcceptLink failed to write response to rec link."<< OUTPUT_ERROR);
            goto err_exit;
        }
        procid = res.req.sid;
        LOG4CPLUS_INFO(g_logger, "procImplAcceptLink a new rec link. fd: "<< tmpfd<< "; clientId: "<< procid);
    }
    else{
        LOG4CPLUS_WARN(g_logger, "procImplAccptLink unrecognized client connecting not for reclink.");
        goto err_exit;
    }
    RecSess_add(tmpfd, procid);
    return true;
err_exit:
    close(tmpfd);
    return false;
}

void* servRec_loop(void *param)
{
    int servfd = serv_listen(AZ_RECCENTER);
    if(servfd < 0){
        LOG4CPLUS_ERROR(g_logger, "servRec_loop serv_listen error. path: "<< AZ_RECCENTER<< OUTPUT_ERROR);
        exit(1);
    }
    LOG4CPLUS_INFO(g_logger, "servRec_loop server listen at path: "<< AZ_RECCENTER);
    struct pollfd fdarr[10];
    fdarr[0].fd = servfd;
    fdarr[0].events = POLLIN;
    while(true){
        int fdidx= 1;
        for(map<int, RecSession*>::iterator it=g_mRecSesses.begin(); it !=g_mRecSesses.end(); it++){
            fdarr[fdidx].fd = it->first;
            fdarr[fdidx].events = POLLIN;
            fdidx ++;
        }
        int retp = poll(fdarr, fdidx, -1);
        if(retp == 0){
            continue;
        }
        else if(retp < 0){
            LOG4CPLUS_ERROR(g_logger, "servtask_loop pooling error. error: "<< strerror(errno));
            break;
        }
        if(fdarr[0].revents & POLLIN){
            if(!procImplAcceptLink(servfd)) break;
        }

        #define POLLERROR_EVS (POLLERR | POLLHUP | POLLNVAL)
        for(int idx=1; idx < fdidx; idx++){
            bool bclose = false;
            if(fdarr[idx].revents & POLLIN){
                assert(g_mRecSesses.find(fdarr[idx].fd) != g_mRecSesses.end());
                g_mRecSesses[fdarr[idx].fd]->recvResponse();
            }
            if(fdarr[idx].revents & POLLERROR_EVS){
                LOG4CPLUS_ERROR(g_logger, "servRec_loop reclink is broken whiling polling, fd: "<< fdarr[idx].fd<< OUTPUT_ERROR);
                bclose = true;
            }
            if(bclose){
                closeRecLink(fdarr[idx].fd);
            }
        }
    }
    exit(1);
    return NULL;
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

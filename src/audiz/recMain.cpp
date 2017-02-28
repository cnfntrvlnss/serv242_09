/*************************************************************************
	> File Name: recMain.cpp
	> Author: 
	> Mail: 
	> Created Time: Wed 22 Feb 2017 11:09:35 PM EST
 ************************************************************************/

#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <cstdint>
#include<iostream>
#include <string>
#include <thread>

#include "../audizrecst.h"
#include "../apueclient.h"
#include "globalfunc.h"
#include "ProjectBuffer.h"

using namespace audiz;
using namespace std;

string g_strSaveDir = "tmpData/";
class ProjectConsumerImpl: public ProjectConsumer{
public:
    bool sendProject(Project* prj);
};

bool ProjectConsumerImpl::sendProject(Project* prj)
{
   char filepath[512];
    sprintf(filepath, "%s%lu.wav", g_strSaveDir.c_str(), prj->PID);
    FILE *fp = fopen(filepath, "wb");
    if(fp == NULL){
        fprintf(stderr, "failed to open file %s.\n", filepath);
        return false;
    }
    vector<ShmSegment> segs;
    prj->getData(segs);
    for(size_t idx=0; idx < segs.size(); idx++){
        const ShmBlock *seg = segs[idx].blk;
        unsigned len = segs[idx].len;
        char *stptr = ShmSeg_get(seg, 0);
        fwrite(stptr, 1, len, fp);
    }
    fclose(fp);
    sprintf(filepath, "ProjectconsumerImpl::sendProject PID=%lu have write data to file %s\n", prj->PID, filepath);
    this->confirm(prj->PID, NULL);
    return true;
}

#define OUTPUT_ERRNO " error: "<< strerror(errno)
static char g_csWorkDir[] = "./";
static int getRecLinkFd(const char* servAddr)
{
    int retfd;
    char myPath[MAX_PATH];
    snprintf(myPath, MAX_PATH, "%s%s", g_csWorkDir, "testrec");
    retfd = cli_conn(myPath, servAddr); 
    if(retfd <= 0){
        LOG4CPLUS_ERROR(g_logger, "getRecLinkFd failed to connect from "<< myPath<< " to "<< servAddr<<", ret: "<< retfd<< OUTPUT_ERRNO);
       return retfd;
    }
    LOG4CPLUS_ERROR(g_logger, "getRecLinkFd begin interaction with new link, client: "<< myPath<< "; server: "<< servAddr);
    Audiz_LinkResponse res;
    strcpy(res.req.head, AZ_RECLINKNAME);
    res.req.sid =getpid();
    vector<AZ_PckVec> pcks;
    res.req.pack_w(pcks);
    int err;
    writen(retfd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "getRecLinkFd failed to write first packet to unpath "<< servAddr << OUTPUT_ERRNO);
        close(retfd);
        return 0;
    }
    res.pack_r(pcks);
    readn(retfd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "getRecLinkFd failed to read response from server."<< OUTPUT_ERRNO);
        close(retfd);
        return 0;
    }
    if(strcmp(res.req.head, AZ_RECLINKNAME) != 0 || res.req.sid != getpid() || strcmp(AZ_RECLINKACK, res.ack) != 0){
        LOG4CPLUS_ERROR(g_logger, "getRecLinkFd read unexpected response from server.");
        close(retfd);
        return 0;
    }
    return retfd;
}

static const char g_ShmPath[] = "recMain";
static const int g_iFtokId = 0;
static bool getSharedData(char* &stptr, int &shmId)
{
    key_t key = ftok(g_ShmPath, g_iFtokId);
    if(key == -1){
        LOG4CPLUS_ERROR(g_logger, "getSharedData ftok failed."<< OUTPUT_ERRNO);
        return false;
    }
    shmId = shmget(key, 0, 0);
    if(shmId == -1){
        LOG4CPLUS_ERROR(g_logger, "getSharedData shmget failed."<< OUTPUT_ERRNO);
        return false;
    }
    stptr = (char*)shmat(g_iFtokId, 0, SHM_RDONLY);
    if(stptr == (void*)-1){
        LOG4CPLUS_ERROR(g_logger, "getSharedData shmat failed."<< OUTPUT_ERRNO);
        return false;
    }
    LOG4CPLUS_INFO(g_logger, "getSharedData have get shared memory. key: "<< key<< "; id: "<< shmId<< "; Pointer: "<< stptr);
    return true;
}

static void delSharedData(char *stptr, int shmId)
{
    if(shmdt(stptr) == -1){
        LOG4CPLUS_ERROR(g_logger, "delshareddata shmdt failed."<< OUTPUT_ERRNO);
    }
    if(shmctl(shmId, IPC_RMID, NULL) == -1){
        LOG4CPLUS_ERROR(g_logger, "delshareddata shmctl with IPC_RMID failed."<< OUTPUT_ERRNO);
    }
}

extern void* servRec_loop(void *param);
using namespace log4cplus;
int main()
{
    PropertyConfigurator::doConfigure("log4cplus.ini");
    initProjPool();
    std::thread sertask(servRec_loop, reinterpret_cast<char*>(NULL));
    sertask.join();
#if 0
    ProjectConsumerImpl con;
    addStream(&con);
    sleep(INT32_MAX);
    removeStream(&con);
    rlseProjPool();
#endif
    return 0;
}

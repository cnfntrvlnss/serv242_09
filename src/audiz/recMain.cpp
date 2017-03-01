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

static string g_strSaveDir = "tmpData/";
static bool saveProject(uint64_t pid, const vector<AZ_PckVec>& segs)
{
    char filepath[512];
    sprintf(filepath, "%s%lu.wav", g_strSaveDir.c_str(), pid);
    FILE *fp = fopen(filepath, "wb");
    if(fp == NULL){
        fprintf(stderr, "failed to open file %s.\n", filepath);
        return false;
    }

    for(size_t idx=0; idx < segs.size(); idx++){
        fwrite(segs[idx].base, 1, segs[idx].len, fp);
    }
    fclose(fp);
    sprintf(filepath, "ProjectconsumerImpl::sendProject PID=%lu have write data to file %s\n", pid, filepath);
    return true;
}

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
    LOG4CPLUS_INFO(g_logger, "getRecLinkFd begin interaction with new link, client: "<< myPath<< "; server: "<< servAddr);
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

static int g_RecFd = -1;
static string g_ShmPath;
static const int g_iFtokId = 0;
static int g_ShmId = 0;
static char *g_ShmPtr = NULL;

static bool getSharedData(char* &stptr, int &shmId)
{
    key_t key = ftok(g_ShmPath.c_str(), g_iFtokId);
    if(key == -1){
        LOG4CPLUS_ERROR(g_logger, "getSharedData ftok failed."<< OUTPUT_ERRNO);
        return false;
    }
    shmId = shmget(key, 0, 0);
    if(shmId == -1){
        LOG4CPLUS_ERROR(g_logger, "getSharedData shmget failed."<< OUTPUT_ERRNO);
        return false;
    }
    stptr = (char*)shmat(shmId, 0, SHM_RDONLY);
    if(stptr == (void*)-1){
        LOG4CPLUS_ERROR(g_logger, "getSharedData shmat failed."<< OUTPUT_ERRNO);
        return false;
    }
    LOG4CPLUS_INFO(g_logger, "getSharedData have get shared memory. key: "<< key<< "; id: "<< shmId<< "; Pointer: "<< (unsigned long)stptr);
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

static bool fetchProject(uint64_t &pid, vector<AZ_PckVec>& data)
{
    data.clear();
    RecLinkMsg_Head head;
    vector<AZ_PckVec> pcks;
    head.pack_r(pcks);
    int err;
    readn(g_RecFd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "fetchProject failed to read msg head "<< OUTPUT_ERRNO);
        return false;
    }
    if(memcmp(head.strMark, AZ_MAGIC_8CHARS, 8) != 0){
        LOG4CPLUS_ERROR(g_logger, "fetchProject read invalid msg head.");
        return false;
    }
    if(head.type != AZ_PUSH_PROJDATA){
        LOG4CPLUS_ERROR(g_logger, "fetchProject read unrecognized msg head.");
        return false;
    }
    //pid follows msg head.
    PckVec pck;
    pck.base = reinterpret_cast<char*>(&pid);
    pck.len = sizeof(uint64_t);
    readn(g_RecFd, &pck, 1, &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "fetchProject failed to read pid."<< OUTPUT_ERRNO);
        return false;
    }
    vector<RecLinkDataUnit> src;
    src.resize(head.val);
    pcks.clear();
    for(int idx=0; idx < head.val; idx++){
        src[idx].pack(pcks);
    }
    readn(g_RecFd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "fetchProject failed to read dataunit."<< OUTPUT_ERRNO);
        return false;
    }
    for(int idx=0; idx < head.val; idx++){
        assert(src[idx].ftokId == 0);
        data.push_back(AZ_PckVec(g_ShmPtr + src[idx].start, src[idx].length));
    }
    return true;
}

static bool notifyFinished(uint64_t pid)
{
    RecLinkMsg_Head head;
    head.type = AZ_NOTIFY_GETDATA;
    head.val = 1;
    vector<AZ_PckVec> pcks;
    head.pack_w(pcks);
    pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&pid), sizeof(uint64_t)));
    int err;
    writen(g_RecFd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "notifyFinished read pid."<< OUTPUT_ERRNO);
        return false;
    }
    return true;
}

static bool reportProjectResult(const Audiz_Result &res)
{
    RecLinkMsg_Head head;
    head.type = AZ_REPORT_RESULT;
    head.val = 1;
    vector<AZ_PckVec> pcks;
    head.pack_w(pcks);
    pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(const_cast<Audiz_Result*>(&res)), sizeof(Audiz_Result)));
    int err;
    writen(g_RecFd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "reportProjectResult fail to write result."<< OUTPUT_ERRNO);
        return false;
    }
    return true;
}

string getMyselfExe()
{
    string ret;
    ret.resize(MAX_PATH);
    char *stptr = const_cast<char*>(ret.c_str());
    if(readlink("/proc/self/exe", stptr, MAX_PATH) == -1){
        LOG4CPLUS_ERROR(g_logger, "getMyselfExe readlink error."<< OUTPUT_ERRNO);
        exit(1);
    }
    return ret;
}

extern void* servRec_loop(void *param);
using namespace log4cplus;
int main()
{
    g_ShmPath = getMyselfExe();
    PropertyConfigurator::doConfigure("log4cplus.ini");
    initProjPool(g_ShmPath.c_str());
    std::thread sertask(servRec_loop, reinterpret_cast<char*>(NULL));
    sertask.detach();

    if(!getSharedData(g_ShmPtr, g_ShmId)){
        exit(1);
    }
    while(true){
        while(g_RecFd < 0){
            g_RecFd = getRecLinkFd(AZ_RECCENTER);
            sleep(3);
        }
        Audiz_Result res;
        uint64_t &pid = res.m_iPCBID;
        res.m_iTargetID = 0x24;
        res.m_iAlarmType = 0x97;
        vector<AZ_PckVec> prjdata;
        if(!fetchProject(pid, prjdata)){
            break;
        }

        saveProject(pid, prjdata);
        notifyFinished(pid);
        reportProjectResult(res);
    }
    delSharedData(g_ShmPtr, g_ShmId);

#if 0
    ProjectConsumerImpl con;
    addStream(&con);
    sleep(INT32_MAX);
    removeStream(&con);
    rlseProjPool();
#endif
    return 0;
}

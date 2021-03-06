/*************************************************************************
	> File Name: audizcli_c.cpp
	> Author: 
	> Mail: 
	> Created Time: Wed 01 Mar 2017 04:55:19 AM EST
 ************************************************************************/
#include "audizcli_c.h"

#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include<iostream>

#include "../apueclient.h"
#include "../utilities/utilites.h"

namespace audiz{

using namespace std;
#ifndef LOG4CPLUS_LOGGERHEADER_
#define MYLOGE(x) cerr<< x<<endl;
#define MYLOGI(x) cerr<< x<< endl;
#endif
#define OUTPUT_ERRNO " error: "<< strerror(errno)

static int getRecLinkFd(const char* myPath, const char* servAddr)
{
    int retfd;
    retfd = cli_conn(myPath, servAddr); 
    if(retfd <= 0){
        MYLOGE("getRecLinkFd failed to connect from "<< myPath<< " to "<< servAddr<<", ret: "<< retfd<< OUTPUT_ERRNO);
       return retfd;
    }
    MYLOGI("getRecLinkFd begin interaction with new link, client: "<< myPath<< "; server: "<< servAddr);
    Audiz_LinkResponse res;
    strcpy(res.req.head, AZ_RECLINKNAME);
    res.req.sid =getpid();
    vector<AZ_PckVec> pcks;
    res.req.pack_w(pcks);
    int err;
    writen(retfd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        MYLOGE("getRecLinkFd failed to write first packet to unpath "<< servAddr << OUTPUT_ERRNO);
        goto err_exit;
    }
    pcks.clear();
    res.pack_r(pcks);
    readn(retfd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        MYLOGE("getRecLinkFd failed to read response from server."<< OUTPUT_ERRNO);
        goto err_exit;
    }
    if(strcmp(res.req.head, AZ_RECLINKNAME) != 0 || res.req.sid != getpid() || strcmp(AZ_RECLINKACK, res.ack) != 0){
        MYLOGE("getRecLinkFd read unexpected response from server.");
        goto err_exit;
    }
    return retfd;
err_exit:
    close(retfd);
    return -1;
}

static int g_RecFd = -1;
static string g_ShmPath;
static const int g_iFtokId = 0;
static int g_ShmId = 0;
static char *g_ShmPtr = NULL;
static int g_iSmpType = 0;
static RmAddSmpFunc g_OpSmpAddr = NULL;
static const char* g_SmpDir = "samples";

static bool getSharedData(char* &stptr, int &shmId)
{
    key_t key = ftok(g_ShmPath.c_str(), g_iFtokId);
    if(key == -1){
        MYLOGE("getSharedData ftok failed."<< OUTPUT_ERRNO);
        return false;
    }
    shmId = shmget(key, 0, 0);
    if(shmId == -1){
        MYLOGE("getSharedData shmget failed."<< OUTPUT_ERRNO);
        return false;
    }
    stptr = (char*)shmat(shmId, 0, SHM_RDONLY);
    if(stptr == (void*)-1){
        MYLOGE("getSharedData shmat failed."<< OUTPUT_ERRNO);
        return false;
    }
    MYLOGE("getSharedData have get shared memory. key: "<< key<< "; id: "<< shmId<< "; Pointer: "<< (unsigned long)stptr);
    return true;
}

static void delSharedData(char *stptr, int shmId)
{
    if(shmdt(stptr) == -1){
        MYLOGE("delshareddata shmdt failed."<< OUTPUT_ERRNO);
    }
    if(shmctl(shmId, IPC_RMID, NULL) == -1){
        MYLOGE("delshareddata shmctl with IPC_RMID failed."<< OUTPUT_ERRNO);
    }
}

bool initRecSession(const char*workDir, const char* servAddr, const char* shmPath)
{
    char myPath[MAX_PATH];
    snprintf(myPath, MAX_PATH, "%s%s", workDir, "testrec");
    while(true){
        g_RecFd = getRecLinkFd(myPath, servAddr);
        if(g_RecFd < 0){
            sleep(3);
            continue;
        }
        break;
    }
    g_ShmPath = shmPath;
    if(!getSharedData(g_ShmPtr, g_ShmId)){
        return false;
    }   
    return true;
}

bool rlseRecSession()
{   
}

bool registerSampleConsumer(unsigned short type, RmAddSmpFunc addr2)
{
    g_OpSmpAddr = addr2;
    g_iSmpType = type;
    RecLinkMsg_Head head;
    head.type = AZ_RECREQ_SAMPLES;
    head.val = type;
    vector<AZ_PckVec> pcks;
    head.pack_w(pcks);
    int err;
    writen(g_RecFd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        MYLOGE("registerSampleConsumer failed to send msg to reclink."<< OUTPUT_ERRNO);
        return false;
    }
    return true;
}

bool procReadProjData(int fd, vector<AZ_PckVec>& pcks, unsigned segnum, uint64_t &pid, vector<AZ_PckVec>& data)
{
    //pid follows msg head.
    PckVec pck;
    pck.base = reinterpret_cast<char*>(&pid);
    pck.len = sizeof(uint64_t);
    int err;
    readn(g_RecFd, &pck, 1, &err, 0);
    if(err < 0){
        MYLOGE("readProjData failed to read pid."<< OUTPUT_ERRNO);
        return false;
    }
    vector<RecLinkDataUnit> src;
    src.resize(segnum);
    pcks.clear();
    for(int idx=0; idx < segnum; idx++){
        src[idx].pack(pcks);
    }
    readn(g_RecFd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        MYLOGE("readProjData failed to read dataunit."<< OUTPUT_ERRNO);
        return false;
    }
    for(int idx=0; idx < segnum; idx++){
        assert(src[idx].ftokId == 0);
        data.push_back(AZ_PckVec(g_ShmPtr + src[idx].start, src[idx].length));
    }
    return true;
}

static bool readSample(const char *smpdir, const char* filename)
{
    int t1, t2, t3;
    if(sscanf(filename, "%d_%x_%d", &t1, &t2, &t3) != 3 || t2 != g_iSmpType){
        return false;
    }
    string filepath = concatePath(smpdir, filename);
    FILE *fp = fopen(filepath.c_str(), "rb");
    if(fp == NULL){
        MYLOGE("readSample failed to open file "<< filepath);
        return false;
    }
    string tmpData;
    readData(fp, tmpData);
    g_OpSmpAddr(filename, const_cast<char*>(tmpData.c_str()), tmpData.size());
    fclose(fp);
    return true;
}

bool procAddRmSamples(int fd, vector<AZ_PckVec>& pcks, unsigned smpnum, bool bAdd)
{
    char smphead[SPKMDL_HDLEN];
    pcks.resize(1);
    pcks[0].base = smphead;
    pcks[0].len = SPKMDL_HDLEN;
    int err;
    const char *loghead;
    if(bAdd){
        loghead = "procAddSamples";
    }
    else{
        loghead = "procRmSamples";
    }
    for(int idx=0; idx< smpnum; idx++){
        readn(fd, reinterpret_cast<PckVec*>(&pcks[0]), 1, &err, 0);
        if(err < 0){
            MYLOGE(loghead<< " failed to read smphead from reclink."<< OUTPUT_ERRNO);
            return false;
        }
        
        if(bAdd){
            if(readSample(g_SmpDir, smphead)){
            }
            else{
                MYLOGE(loghead<< " failed to read sample data of head "<< smphead);
            }
        }
        else{
            g_OpSmpAddr(smphead, NULL, 0);
        }
    }
    return true;
}

void readAllSamples()
{
    unsigned retnum = procFilesInDir(g_SmpDir, readSample);
    MYLOGI("readAllSamples have read "<< retnum<< " samples successfully.");
}

bool fetchProject(uint64_t &pid, vector<AZ_PckVec>& data)
{
    data.clear();
    RecLinkMsg_Head head;
    vector<AZ_PckVec> pcks;
    while(true){
        pcks.clear();
        head.pack_r(pcks);
        int err;
        readn(g_RecFd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
        if(err < 0){
            MYLOGE("fetchProject failed to read msg head "<< OUTPUT_ERRNO);
            return false;
        }
        if(memcmp(head.strMark, AZ_MAGIC_8CHARS, 8) != 0){
            MYLOGE("fetchProject read invalid msg head.");
            return false;
        }
        if(head.type == AZ_PUSH_PROJDATA){
            if(!procReadProjData(g_RecFd, pcks, head.val, pid, data)) return false;
            break;
        }
        else if(head.type == AZ_RECFEED_SAMPLES){
            readAllSamples();
        }
        else if(head.type == AZ_RECADD_SAMPLE){
            if(!procAddRmSamples(g_RecFd, pcks, head.val, true)){
                return false;
            }
        }
        else if(head.type == AZ_RECRM_SAMPLE){
            if(!procAddRmSamples(g_RecFd, pcks, head.val, false)){
                return false;
            }
        }
        else{
            MYLOGE("fetchProject read unrecognized msg head.");
            return false;
        }
    }
    return true;
}

bool notifyFinished(uint64_t pid)
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
        MYLOGE("notifyFinished read pid."<< OUTPUT_ERRNO);
        return false;
    }
    return true;
}

bool reportProjectResult(const Audiz_Result &res)
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
        MYLOGE("reportProjectResult fail to write result."<< OUTPUT_ERRNO);
        return false;
    }
    return true;
}

}

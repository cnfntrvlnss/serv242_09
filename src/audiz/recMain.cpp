/*************************************************************************
	> File Name: recMain.cpp
	> Author: 
	> Mail: 
	> Created Time: Wed 22 Feb 2017 11:09:35 PM EST
 ************************************************************************/

#include <unistd.h>

#include <cstdint>
#include<iostream>
#include <string>
#include <thread>

#include "../audizrecst.h"
#include "../apueclient.h"
#include "globalfunc.h"
#include "ProjectBuffer.h"
#include "audizcli_c.h"

using namespace audiz;
using namespace std;

#define OUTPUT_ERRNO " error: "<< strerror(errno)
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

struct SpkMdlInfo{
    explicit SpkMdlInfo(int id=0, int type=0, int level=0):
        cfgId(id), alarmType(type), harmLevel(level)
    {}
    int cfgId;
    int alarmType;
    int harmLevel;
};
static vector<SpkMdlInfo> g_vecSpkInfo;
static vector<char*> g_vecSpkMdl;

static bool addOrRmSample(const char *head, char *data, unsigned len)
{
    int t1, t2, t3;
    if(sscanf(head, "%d_%x_%d", &t1, &t2, &t3) != 3) return false;
    if(data != NULL){
        g_vecSpkMdl.push_back(reinterpret_cast<char*>(malloc(len)));
        g_vecSpkInfo.push_back(SpkMdlInfo(t1, t2, t3));
    }
    else{
        //TODO remove sample.
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

static char g_csWorkDir[] = "./";
extern void* servRec_loop(void *param);
using namespace log4cplus;
int main()
{
    string myselfexe = getMyselfExe();
    PropertyConfigurator::doConfigure("log4cplus.ini");
    initProjPool(myselfexe.c_str());
    std::thread sertask(servRec_loop, reinterpret_cast<char*>(NULL));
    sertask.detach();

    initRecSession("./", AZ_RECCENTER, myselfexe.c_str());
    registerSampleConsumer(0x92, addOrRmSample);

    while(true){
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
        if(g_vecSpkInfo.size() > 0){
            res.m_iTargetID = g_vecSpkInfo[0].cfgId;
            res.m_iAlarmType = g_vecSpkInfo[0].alarmType;
            res.m_iHarmLevel = g_vecSpkInfo[0].harmLevel;
        }
        reportProjectResult(res);
    }

#if 0
    ProjectConsumerImpl con;
    addStream(&con);
    sleep(INT32_MAX);
    removeStream(&con);
    rlseProjPool();
#endif
    return 0;
}

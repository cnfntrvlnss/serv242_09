/*************************************************************************
	> File Name: interface242.cpp
	> Author: 
	> Mail: 
	> Created Time: Thu 09 Feb 2017 01:15:46 AM EST
 ************************************************************************/

#include "interface242.h"

#include <sys/types.h>
#include <dirent.h>
#include <cstdio>
#include <string>
#include<iostream>

#include "globalfunc.h"
#include "../audiz/audizcli_p.h"
//#include "log4z.h"

using namespace std;
static bool g_bInitialized = false;
static unsigned int g_iModuleID;
static SessionStruct *g_AudizSess;
static ReceiveResult g_ReportResult;

//static const char g_AudizPath[] = "ioacases/recogMain";
static string g_AudizPath = (string)"ioacases/" + AZ_DATACENTER;

static int audiz_reportResult(Audiz_Result *pResult)
{
    CDLLResult res;
    WavDataUnit data;
    res.m_pDataUnit[0] = &data;
    data.m_iDataLen = 0;
    data.m_pData = NULL;
    data.m_pPCB = NULL;
    data.m_iPCBID = pResult->m_iPCBID;
    res.m_iTargetID = pResult->m_iTargetID;
    res.m_iAlarmType = pResult->m_iAlarmType;
    res.m_iHarmLevel = pResult->m_iHarmLevel;
    res.m_fLikely = pResult->m_fLikely;
    LOGFMT_DEBUG(g_logger, "report target... PID=%lu TargetType=%u TargetID=%u.", res.m_pDataUnit[0]->m_iPCBID, res.m_iAlarmType, res.m_iTargetID);
    return g_ReportResult(g_iModuleID, &res);
}

static int audiz_getAllMdls(SpkMdlSt **pMdls)
{
    return 0;
}

static string g_SmpDir = "SpkModel";
class SpkMdlStVecImpl: public SpkMdlStVec, SpkMdlStVec::iterator
{
public:
    ~SpkMdlStVecImpl(){
        if(dp != NULL) closedir(dp);
    }
    SpkMdlStVecImpl* iter(){
        if(dp != NULL){ closedir(dp); }
        if(g_SmpDir[g_SmpDir.size() - 1] != '/'){
            g_SmpDir += "/";
        }
        dp = opendir(g_SmpDir.c_str());
        if(dp == NULL){
            LOGFMT_ERROR(g_logger, "SpkMdlStVecImpl:iter failed to open dir %s.", g_SmpDir.c_str());
            return NULL;
        }
        LOGFMT_DEBUG(g_logger, "SpkMdlStVecImpl::iter invoked.");
        return this;
    }
    SpkMdlSt* next(){
        LOGFMT_DEBUG(g_logger, "SpkMdlStVecImpl::next invoked.");
        struct dirent *dirp = NULL;
        while(true){
            dirp = readdir(dp);
            if(dirp == NULL){
                closedir(dp);
                return NULL;
            }
            char *filename = dirp->d_name; 
            if(strcmp(filename, ".") == 0) continue;
            if(strcmp(filename, ".") == 0) continue;
            int t1, t2, t3;
            if(sscanf(filename, "%d_%x_%d.", &t1, &t2, &t3) != 3){
                continue;
            }
            strncpy(mdl.head, filename, SPKMDL_HDLEN-1);
            string filepath = g_SmpDir + mdl.head;
            FILE *fp = fopen(filepath.c_str(), "rb");
            if(fp == NULL){
                LOGFMT_ERROR(g_logger, "SpkMdlStVecImpl::next failed to open file %s.", filepath.c_str());
                 continue;   
            }
            fseek(fp, 0, SEEK_END);
            mdl.len = ftell(fp);
            g_TmpData.resize(mdl.len);
            fseek(fp, 0, SEEK_SET);
            mdl.buf = const_cast<char*>(g_TmpData.c_str());
            fread(mdl.buf, 1, mdl.len, fp);
            fclose(fp);
            return &mdl;
        }
    }
private:
    DIR *dp = NULL;
    SpkMdlSt mdl;
    string g_TmpData;
};

static SpkMdlStVecImpl g_AllSmpVec;
int InitDLL(int iPriority,
        int iThreadNum,
        int *pThreadCPUID,
        ReceiveResult func,
        int iDataUnitBufSize,
        char *path,
        unsigned int iModuleID)
{
    if(g_bInitialized) {
        LOGE("InitDLL ioacas module has been already initialized.");
        return 0;
    }
    g_iModuleID = iModuleID;
    g_ReportResult = func; 
    g_AudizSess = new SessionStruct(g_AudizPath.c_str(), audiz_reportResult, &g_AllSmpVec);
    g_bInitialized = true;
    LOGI("InitDLL ioacas module is initialized successfully.");
    return 0;
}

int AddCfg(unsigned int id, 
        const char *strName,
        const char *strConfigFile,
        int iType,
        int iHarmLevel)
{
    return 0;
}

int AddCfgByBuf(const char *pData,
        int iDataBytes,
        unsigned int id,
        int iType,
        int iHarmLevel)
{
    return 0;
}

int AddCfgByDir(int iType, const char *strDir)
{
    return 0;
}

int RemoveAllCfg(int iType)
{
    return 0;
}

int RemoveCfgByID(unsigned int id, int iType, int iHarmLevel)
{
    return 0;
}

bool SetRecord(int iType, bool bRecord)
{
    return 0;
}

int SendData2DLL(WavDataUnit *p)
{
    clockoutput_start("SendData2DLL");
    Audiz_WaveUnit unit;
    unit.m_iPCBID = p->m_iPCBID;
    unit.m_iDataLen = p->m_iDataLen;
    unit.m_pData = p->m_pData;
    unit.m_pPCB = p->m_pPCB;
    g_AudizSess->writeData(&unit);
    string output = clockoutput_end();
    LOGFMT_TRACE(g_logger, output.c_str());
    return 0;
}

int GetDLLVersion(char *p, int &length)
{
    char strVersion[100];
    strcpy(strVersion, "ioacas/lang v0.1.0");
    length = strlen(strVersion);
    strncpy(p, strVersion, length);
    return 1;
}

int GetDataNum4Process(int iType[], int num[])
{
    return 0;
}

int CloseDLL()
{
    delete g_AudizSess;
    return 0;
}

extern "C" void notifyProjFinish(unsigned long pid)
{
    Audiz_WaveUnit unit;
    unit.m_iPCBID = pid;
    unit.m_iDataLen = 0;
    unit.m_pData = NULL;
    unit.m_pPCB = NULL;
    g_AudizSess->writeData(&unit);
}

extern "C" bool isAllFinished()
{
    unsigned retm = g_AudizSess->queryUnfinishedProjNum();
    if(retm != 0){
        return false;
    }
    return true;
}

extern "C" bool isConn2Server()
{
    return g_AudizSess->isConnected();
}

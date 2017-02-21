/*************************************************************************
	> File Name: interface242.cpp
	> Author: 
	> Mail: 
	> Created Time: Thu 09 Feb 2017 01:15:46 AM EST
 ************************************************************************/

#include "interface242.h"

#include <cstdio>
#include <string>
#include<iostream>

#include "globalfunc.h"
#include "audizcli_p.h"
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
    CDLLResult *pRes = reinterpret_cast<CDLLResult*>(pResult);
    LOGFMT_DEBUG(g_logger, "report target... PID=%lu TargetType=%u TargetID=%u.", pRes->m_pDataUnit[0]->m_iPCBID, pRes->m_iTargetID, pRes->m_iAlarmType);
    return g_ReportResult(g_iModuleID, pRes);
}

static int audiz_getAllMdls(SpkMdlSt **pMdls)
{
    return 0;
}

class SpkMdlStVecImpl: public SpkMdlStVec, SpkMdlStVec::iterator
{
public:
    SpkMdlStVecImpl* iter(){
        LOGFMT_INFO(g_logger, "SpkMdlStVecImpl::iter invoked.");
        return this;
    }
    SpkMdlSt* next(){
        LOGFMT_INFO(g_logger, "SpkMdlStVecImpl::next invoked.");
        return NULL;
    }
private:
    
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

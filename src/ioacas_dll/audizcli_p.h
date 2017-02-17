/*************************************************************************
    > File Name: AUDIZCLI_P__H
    > Author: zhengshurui
    > Mail:  zhengshurui@thinkit.cn
    > Created Time: Wed 24 Aug 2016 07:10:23 PM PDT
 ************************************************************************/
#ifndef AUDIZCLI_P__H
#define AUDIZCLI_P__H

#include <pthread.h>
#include <climits>
#include<iostream>
#include<queue>
#include <string>
#include <vector>

#include "../audizstruct.h"
#define MAX_PATH 512

typedef int (*AUDIZ_REPORTRESULTPROC)(Audiz_Result *pResult);
typedef int (*AUDIZ_GETALLMDLSPROC)(struct SpkMdlSt **);

/**
 * capsulate session working with servRec.
 * interfaces contain receive data, receive models.
 * pull all models, push results.
 *
 * notes: 
 */
class SessionStruct{
public:
    SessionStruct(const char* servPath, AUDIZ_REPORTRESULTPROC resFunc, AUDIZ_GETALLMDLSPROC getModlsFunc);
    ~SessionStruct();
    
    bool writeData(Audiz_WaveUnit* unit);
    bool writeSample(const char *name, char *buf, unsigned len);
    bool deleteSample(const char *name);
    bool queueSamples(std::vector<std::string> &smps);
    unsigned queueUnfinishedProjNum();

    friend void* maintainSession_ex(void* param);
    ////////////////data part//////////////////
private:
    SessionStruct(const SessionStruct&);
    SessionStruct& operator=(const SessionStruct&);
    bool connectServRec();
    void closeServRec();
    bool getIsRun(){
        bool retb;
        pthread_mutex_lock(&isRunLock);
        retb = isRunning;
        pthread_mutex_unlock(&isRunLock);
        return retb;
    }
    void closeDataLink();
    void closeModlLink();
    bool checkModlFd();
    bool checkDataFd();
    void setIsRun(bool val){
        pthread_mutex_lock(&isRunLock);
        isRunning = val;
        pthread_mutex_unlock(&isRunLock);
    }
    int prochandleResp();
    int procSendCfgCmd();
    int procExecCommonCfgCmd(std::vector<AZ_PckVec>& task, Audiz_PResult &result);

    /////////////////data part//////////////////
    char servPath[MAX_PATH];
    bool isRunning;
    pthread_mutex_t isRunLock;
    pthread_t modlThreadId;
    AUDIZ_REPORTRESULTPROC repResAddr;
    AUDIZ_GETALLMDLSPROC retMdlsAddr;
    int dataFd;
    int modlFd;
    pthread_mutex_t dataFdLock;
    //pthread_mutex_t modlFdLock;
    //used to commit task to modl link.
    pthread_mutex_t cfgCmdLock;
    pthread_cond_t cfgCmdResultSetCond;
    pthread_cond_t cfgCmdTaskEmptyCond;
    std::vector<AZ_PckVec> cfgCmdTask;
    Audiz_PResult cfgCmdResult;
    //used to send modl asanchronizely.
    //list<SpkMdlData> allmdls;
};

#endif 

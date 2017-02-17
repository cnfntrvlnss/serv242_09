/*************************************************************************
    > File Name: sessRec.cpp
    > Author: zhengshurui
    > Mail:  zhengshurui@thinkit.cn
    > Created Time: Wed 24 Aug 2016 11:30:02 PM PDT
 ************************************************************************/

#include <sys/socket.h>
#include <sys/select.h>
//#include <sys/time.h>
#include <unistd.h>
#include <poll.h>

#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cerrno>
#include<iostream>
using namespace std;

#include "globalfunc.h"
#include "audizcli_p.h"
#include "../apueclient.h"

# ifndef _GLOBALFUNC_H
enum BLOG_LEVEL{BLOGT, BLOGD, BLOGI, BLOGW, BLOGE};
#define BLOGLMT BLOGI
#define BIFO(x) if(BLOGLMT >= BLOG##x) 
#define BLOGE(fmt, ...)  BIFO(E) fprintf(stderr, "ERROR "fmt "\n", ##__VA_ARGS__)
#define BLOGW(fmt, ...) BIFO(W) fprintf(stderr, "WARN "fmt"\n", ##__VA_ARGS__)
#define BLOGI(fmt, ...) BIFO(I) fprintf(stderr, "INFO "fmt"\n", ##__VA_ARGS__)
#define BLOGD(fmt, ...) BIFO(D) fprintf(stderr, "DEBUG "fmt"\n", ##__VA_ARGS__)
#define BLOGT(fmt, ...) BIFO(T) fprintf(stderr, "TRACE "fmt"\n", ##__VA_ARGS__)
#else
#define BLOGE(fmt, ...) LOGFMT_ERROR(g_logger, fmt, ##__VA_ARGS__)
#define BLOGW(fmt, ...) LOGFMT_WARN(g_logger, fmt, ##__VA_ARGS__)
#define BLOGI(fmt, ...) LOGFMT_INFO(g_logger, fmt, ##__VA_ARGS__)
#define BLOGD(fmt, ...) LOGFMT_DEBUG(g_logger, fmt, ##__VA_ARGS__)
#define BLOGT(fmt, ...) LOGFMT_TRACE(g_logger, fmt, ##__VA_ARGS__)
#endif

static char g_csWorkDir[MAX_PATH] = "ioacas/";

/**
 * execute command synchronizely.
 *
 * the command commited to cfglink is pubulished in the shared space with cfglink,
 * and wait for result in shared space for result with cfglink.
 * attention: result should be freed manually.
 */
 int SessionStruct::procExecCommonCfgCmd(std::vector<AZ_PckVec>& task, Audiz_PResult &result)
{
    pthread_mutex_lock(&cfgCmdLock);
    while(cfgCmdTask.size() > 0){
        pthread_cond_wait(&cfgCmdTaskEmptyCond, &cfgCmdLock);
    }

    cfgCmdTask.insert(cfgCmdTask.end(), task.begin(), task.end());
    cfgCmdResult.reset();

    while(cfgCmdResult.head.type == 0){
        pthread_cond_wait(&cfgCmdResultSetCond, &cfgCmdLock);
    }
    cfgCmdTask.clear();
    result = cfgCmdResult;
    cfgCmdResult.reset();
    pthread_mutex_unlock(&cfgCmdLock);
    pthread_cond_broadcast(&cfgCmdTaskEmptyCond);
    return 0;
}

/*
size_t writen(int fd, string *vec, unsigned cnt, int *err, int istry)
{
    PckVec *tmpPcks = new PckVec[cnt];
    for(unsigned idx=0; idx < cnt; idx ++){
        tmpPcks[idx].len = vec[idx].size();
        tmpPcks[idx].base = const_cast<char*>(vec[idx].c_str());
    }
    size_t ret = writen(fd, tmpPcks, cnt, err, istry);
    delete [] tmpPcks;
    return ret;
}
*/

/**
 * send one cfgcmd to server, wait the response matching to the cfgcmd.
 *
 */
 int SessionStruct::procSendCfgCmd()
{
    //TODO add commit command whose result is not cared.
    pthread_mutex_lock(&cfgCmdLock);
    if(cfgCmdTask.size() == 0){
        pthread_mutex_unlock(&cfgCmdLock);
    }
    unsigned tolLen = 0;
    for(size_t idx=0; idx < cfgCmdTask.size(); idx++){
        tolLen += cfgCmdTask[idx].len;
    }
    int err;
    //vector<AZ_PckVec> vecResult;
    //Audiz_PResult_Head_OnWire::serialize(cfgCmdResult.head, vecResult);
    if(writen(modlFd, reinterpret_cast<PckVec*>(&cfgCmdTask[0]), cfgCmdTask.size(), &err, 0) != tolLen){
        BLOGE("procExecCfgCmd error write cmd to cfg link. error: %s.", strerror(errno));
        cfgCmdResult.head.type = CHARS_AS_INIT32(const_cast<char*>(cfgCmdTask[0].base)) + 1;
        cfgCmdResult.head.ack = -1;
        //TODO in most cases, the link needs be closed here.
        closeModlLink();
    }
    pthread_mutex_unlock(&cfgCmdLock);
    return 0;
}

/**
 *
 *
 */
int SessionStruct::prochandleResp()
{
    Audiz_PResult azres;
    Audiz_PResult_Head azreshd;
    vector<AZ_PckVec> vecResult;
    Audiz_PResult_Head_OnWire::getEmptyPckVec(azreshd, vecResult);
    int restype = AZOP_INVALID_MARK;
    pthread_mutex_lock(&cfgCmdLock);
    if(cfgCmdTask.size() > 0){
        restype = CHARS_AS_INIT32(const_cast<char*>(cfgCmdTask[0].base)) + 1;
    }
    else{
        pthread_mutex_lock(&cfgCmdLock);
    }
    int ret = 0;
    while(true){
        int err;
        unsigned retrn = readn(modlFd, reinterpret_cast<PckVec*>(&vecResult[0]), 2, &err, 0);
        if(retrn != 8){
            BLOGE("SessionStruct::prochandleResp error read head of response of cmd from modl link. error: %s.", strerror(errno));
            ret = -1;
            break;
        }
        if(err > 0){
            BLOGW("SessionStruct::prochandleResp have tried %u times to read response.", err);
        }
        unsigned arglen = azreshd.getArgLen();
        if(arglen > 0){
            PckVec argPck;
            argPck.base = (char*)malloc(arglen);
            argPck.len = arglen;
            retrn = readn(modlFd, &argPck, 1, &err, 0);
            if(retrn != arglen){
                BLOGE("SessionStruct::prochandleResp error read arg part of response of cmd from modl link. error: %s.", strerror(errno));
                free(argPck.base);
                ret = -1;
                break;
            }
            
            if(azreshd.type == AZOP_REC_RESULT){
                unsigned reslen = arglen / sizeof(Audiz_Result);
                Audiz_Result *ress = reinterpret_cast<Audiz_Result*>(argPck.base);
                for(unsigned idx=0; idx < reslen; idx++){
                    BLOGT("Project Result from modl. PID=%lu TargetID=%u.", ress[idx].m_Proj.m_iPCBID, ress[idx].m_iTargetID);
                    repResAddr(&ress[idx]);
                    
                }
                free(argPck.base);
            }
            else if(azreshd.type != restype){
                free(argPck.base);
            }
            else{
                azres.argBuf = argPck.base;
            }
        }
        else if(arglen < 0){
            BLOGE("SessionStruct::prochandleResp parse error packet, then close the link.");
            ret = -1;
            break;
        }

        if(azreshd.type != AZOP_REC_RESULT && azreshd.type != restype){
            BLOGE("SessionStruct::prochandleResp unexpected response from server. type: %d; ack: %d.", azreshd.type, azreshd.ack);
        }
        if(restype != AZOP_INVALID_MARK){
            if(restype != azreshd.type){
                 continue;      
            }
        }
        break;
    }
    if(ret == -1){
        //in most cases, the link needs be closed here.
        if(restype != AZOP_INVALID_MARK){
            azreshd.type = restype;
            azreshd.ack = -1;
        }
        closeModlLink();
    }
    if(restype != AZOP_INVALID_MARK){
        assert(azreshd.type == restype);    
        cfgCmdResult = azres;
        pthread_mutex_unlock(&cfgCmdLock);
    }

    return ret;
}

static int getModlLinkFd(const char* servAddr)
{
    int retfd;
    char myPath[MAX_PATH];
    snprintf(myPath, MAX_PATH, "%s%s", g_csWorkDir, "modl");
    retfd = cli_conn(myPath, servAddr); 
    if(retfd <= 0){
       BLOGE("getCfgLinkFd failed to connect from %s to %s, error: %s.", myPath, servAddr, strerror(errno));
       return retfd;
    }
    char cfgLinkName[64];
    unsigned long procid = getpid();
    snprintf(cfgLinkName, 64, "%s", AZ_CFGLINKNAME);
    PckVec pcks[2];
    pcks[0].base = cfgLinkName;
    pcks[0].len = 64;
    pcks[1].base = reinterpret_cast<char*>(&procid);
    pcks[1].len = sizeof(unsigned long);
    int err;
    if(writen(retfd, pcks, 2, &err, 0) != pcks[0].len + pcks[1].len){
        BLOGE("getCfgLinkFd failed to write the first packet to connection to unpath %s. error: %s.", servAddr, strerror(errno));
        close(retfd);
        return 0;
    }
    char retLinkName[64];
    unsigned long retsid;
    char retData[64];
    PckVec retPcks[3];
    retPcks[0].base = retLinkName;
    retPcks[0].len = 64;
    retPcks[1].base = reinterpret_cast<char*>(&retsid);
    retPcks[1].len = sizeof(unsigned long);
    retPcks[2].base = retData;
    retPcks[2].len = 64;
    unsigned tolLen = retPcks[0].len + retPcks[1].len + retPcks[2].len;
    if(readn(retfd, retPcks, 3, &err, 0) != tolLen){
        BLOGE("getCfgLinkFd failed to read message from server in second step. unpath: %s, error: %s.", servAddr, strerror(errno));
        close(retfd);
        return 0;
    }
    if(strcmp(retLinkName, cfgLinkName) != 0 || retsid != procid || strcmp(AZ_LINKBUILDOK, retData) != 0){
        BLOGE("getCfgLinkFd modl link failed to follow audiz protocol.");
        close(retfd);
        return 0;
    }
    return retfd;
}

static int getDataLinkFd(const char* servAddr)
{
    int retfd;
    char myPath[MAX_PATH];
    snprintf(myPath, MAX_PATH, "%s%s", g_csWorkDir, "data");
    retfd = cli_conn(myPath, servAddr); 
    if(retfd <= 0){
       BLOGE("getDataLinkFd failed to connect from %s to %s, error: %s.", myPath, servAddr, strerror(errno));
       return retfd;
    }
    char cfgLinkName[64];
    unsigned long procid = getpid();
    snprintf(cfgLinkName, 64, "%s", AZ_DATALINKNAME);
    PckVec pcks[2];
    pcks[0].base = cfgLinkName;
    pcks[0].len = 64;
    pcks[1].base = reinterpret_cast<char*>(&procid);
    pcks[1].len = sizeof(unsigned long);
    int err;
    if(writen(retfd, pcks, 2, &err, 0) != pcks[0].len + pcks[1].len){
        BLOGE("getDataLinkFd failed to write the first packet to connection to unpath %s. error: %s.", servAddr, strerror(errno));
        close(retfd);
        return 0;
    }
    char retLinkName[64];
    unsigned long retsid;
    char retData[64];
    PckVec retPcks[3];
    retPcks[0].base = retLinkName;
    retPcks[0].len = 64;
    retPcks[1].base = reinterpret_cast<char*>(&retsid);
    retPcks[1].len = sizeof(unsigned long);
    retPcks[2].base = retData;
    retPcks[2].len = 64;
    retPcks[1].len = sizeof(unsigned long);
    unsigned tolLen = retPcks[0].len + retPcks[1].len + retPcks[2].len;
    if(readn(retfd, retPcks, 3, &err, 0) != tolLen){
        BLOGE("getDataLinkFd failed to read message from server in second step. unpath: %s, error: %s.", servAddr, strerror(errno));
        close(retfd);
        return 0;
    }
    if(strcmp(retLinkName, cfgLinkName) != 0 || retsid != procid || strcmp(AZ_LINKBUILDOK, retData) != 0){
        BLOGE("getDataLinkFd data link failed to follow audiz protocol.");
        close(retfd);
        return 0;
    }
    return retfd;
}

bool SessionStruct::checkDataFd()
{
    pthread_mutex_lock(&dataFdLock);
    if(dataFd == -1){
        int fd = getDataLinkFd(servPath);
        if(fd > 0) dataFd = fd;
    }
    bool ret = true;
    if(dataFd == -1) ret = false;
    pthread_mutex_unlock(&dataFdLock);
    return ret;
}
bool SessionStruct::checkModlFd()
{
    if(modlFd == -1){
        int fd = getModlLinkFd(servPath);
        if(fd > 0){
            //TODO send all mdls.
             modlFd = fd;   
        }
        else return false;
    }
    return true;
}
/**
 * use signal to interrupt poll.
 *
 */
void* maintainSession_ex(void* param)
{
    //TODO setup signal mask. use sigint to anachronize with another thread.
    SessionStruct *pss = (SessionStruct*) param;
    struct pollfd fds[1];

    while(pss->getIsRun()){
        if(!pss->checkModlFd()){
            struct timeval tm;
            tm.tv_sec = 3;
            tm.tv_usec = 0;
            select(0, NULL, NULL, NULL, &tm);
            continue;
        }
        fds[0].fd = pss->modlFd;
        fds[0].events = POLLIN;
        BLOGI("maintainSession_ex start waiting in modl file descriptor...");
        int wmillisecs = 60 * 1000;
        int retp = poll(fds, 1, wmillisecs);
        if(retp > 0){
            BLOGT("maintainSession_ex modl has data to read.");
            pss->prochandleResp();
        }
        else if(retp < 0){
            if(errno == EINTR){
                BLOGW("maintainSession_ex interrupted by signal while polling in modl.");

            }
            else{
                BLOGE("maintainSession_ex error occure while polling in modl. error: %s.", strerror(errno));
            }
        }
        else if(retp == 0){
            BLOGT("maintainSession_ex time out while polling modl for %d milliseconds.", wmillisecs);
        }
        
        pss->procSendCfgCmd();
    }

    return NULL;
}


SessionStruct::SessionStruct(const char* servPath, AUDIZ_REPORTRESULTPROC resFunc, AUDIZ_GETALLMDLSPROC getModlsFunc)
{
    if(g_csWorkDir[0] != '/'){
        char tmpPath[MAX_PATH];
        strcpy(tmpPath, g_csWorkDir);
        if(getcwd(g_csWorkDir, MAX_PATH) == NULL){
        }
        strcat(g_csWorkDir, "/");
        strncat(g_csWorkDir, tmpPath, MAX_PATH);
    }
    strncpy(this->servPath, servPath, MAX_PATH);
    modlFd = -1;
    dataFd = -1;
    pthread_mutex_init(&isRunLock, NULL);
    pthread_mutex_init(&dataFdLock, NULL);
    pthread_mutex_init(&cfgCmdLock, NULL);
    pthread_cond_init(&cfgCmdResultSetCond, NULL);
    pthread_cond_init(&cfgCmdTaskEmptyCond, NULL);
    isRunning = true;
    int readp = pthread_create(&modlThreadId, NULL, maintainSession_ex, this);
    if(readp < 0){
        BLOGE("SessionStruct::SessionStruct failed to create thread maintainSession.\n");
        exit(1);
    }
}

SessionStruct::~SessionStruct()
{
    setIsRun(false);
    pthread_join(modlThreadId, NULL);
    pthread_mutex_destroy(&isRunLock);
    pthread_mutex_destroy(&dataFdLock);
    //pthread_mutex_destroy(&modlFdLock);
}


void SessionStruct::closeDataLink()
{
    close(dataFd);
    dataFd = -1;
}
void SessionStruct::closeModlLink()
{
    close(modlFd);
    modlFd = -1;
}

//const static unsigned DATAREDUNSIZE = 8;
//const static char dataRedunArr[DATAREDUNSIZE] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7};

bool SessionStruct::writeData(Audiz_WaveUnit *unit)
{
    if(!checkDataFd()){
        return false;
    }
    vector<AZ_PckVec> pcks;
    Audiz_Wave_OnWire::serialize(*unit, pcks);
    bool bret = false;
    pthread_mutex_lock(&dataFdLock);
    int fd = dataFd;
    //if(fd == -1){
    //    BLOGE("data link is not established.");
    //    pthread_mutex_unlock(&dataFdLock);
    //    return bret;
    //}

    bret = true;
    int cls = 0;
    writen(fd, reinterpret_cast<PckVec*>(&pcks[0]), 4, &cls, 0);
    if(cls != 0){
        if(cls == -1){
            closeDataLink();
            bret = false;
        }
        else {
            BLOGE("data link becomes congested, try count: %d", cls);       
        }
    }

    pthread_mutex_unlock(&dataFdLock);
    return bret;
}

bool SessionStruct::writeSample(const char *name, char *buf, unsigned len)
{
    SpkMdlSt tmpSpk;
    int hdsize = SPKMDL_HDLEN - 1;
    tmpSpk.head[hdsize] = '\0';
    strncpy(tmpSpk.head, name, hdsize);
    tmpSpk.buf = buf;
    tmpSpk.len = len;
    vector<AZ_PckVec> pcks;
    Audiz_PRequest_Head req;
    req.type = AZOP_ADD_SAMPLE;
    req.addLen = 0;
    Audiz_PRequest_Head_OnWire::serialize(req, pcks);
    unsigned st = pcks.size();
    SpkMdlSt_OnWire::Serialize(tmpSpk, pcks);
    for(unsigned idx = st; idx < pcks.size(); idx++){
        req.addLen += pcks[idx].len;
    }
    Audiz_PResult res;
    procExecCommonCfgCmd(pcks, res);
    return true;
}
bool SessionStruct::deleteSample(const char *name)
{
    return writeSample(name, NULL, 0);   
}
bool SessionStruct::queueSamples(std::vector<std::string> &smps)
{
    Audiz_PRequest_Head req;
    req.type = AZOP_QUERY_SAMPLE;
    req.addLen = 0;
    vector<AZ_PckVec> pcks;
    Audiz_PRequest_Head_OnWire::serialize(req, pcks);
    Audiz_PResult res;
    procExecCommonCfgCmd(pcks, res);
    if(res.argBuf != NULL){
        for(int idx=0; idx < res.head.getArgLen(); idx+= SPKMDL_HDLEN){
            smps.push_back(&res.argBuf[idx]);
        }
        free(res.argBuf);
    }

    return true;
}
unsigned SessionStruct::queueUnfinishedProjNum(){
    Audiz_PRequest_Head req;
    req.type = AZOP_QUERY_PROJ;
    req.addLen = 0;
    vector<AZ_PckVec> pcks;
    Audiz_PRequest_Head_OnWire::serialize(req, pcks);
    Audiz_PResult res;
    procExecCommonCfgCmd(pcks, res);
    assert(res.argBuf == NULL);
    return res.head.ack;
}

/*
bool SessionStruct::writeModl(const char* strHd, char *buf, unsigned len)
{
    bool ret = false;
    PckVec pcks[3];
    static char pckhd[SPKMDL_HDLEN];
    const unsigned pckhdlen = SPKMDL_HDLEN;
    memset(pckhd, 0, pckhdlen);
    strncpy(pckhd, strHd, pckhdlen);
    pcks[0].base = pckhd;
    pcks[0].len = pckhdlen;
    pcks[1].base = (char*)&len;
    pcks[1].len = sizeof(len);
    pcks[2].base = buf;
    pcks[2].len = len;
    pthread_mutex_lock(&fdsLock);
    int mdlfd = this->modlFd;
    pthread_mutex_unlock(&fdsLock);

    if(mdlfd){
        BLOGE("no client being connected to consume the data.\n");
        return ret;
    }
    int cls = 0;
    writen(mdlfd, pcks, 3, &cls, 0);
    if(cls !=0){
        if(cls == -1){
            BLOGE("the modl link is broken, then needs to be closed.\n");
            closeModlLink();
        }
        else {
            BLOGE("the modl link becomes congesting, try count: %d\n", cls);       
            ret =true;
        }
    }
    else{
        ret = true;
    }

    return ret;
}

bool SessionStruct::connectServRec()
{
    bool isSucc = false;
    assert(ressFd <0 && modlFd <0 && dataFd <0);
    while(true){
        int tmpfd = getDataFd(servPath);
        if(tmpfd <= 0) break;
        else dataFd = tmpfd;

        tmpfd = getModlFd(servPath);
        if(tmpfd <= 0) break;
        else modlFd = tmpfd;

        tmpfd = getRessFd(servPath);
        if(tmpfd <= 0) break;
        else ressFd = tmpfd;
        
        isSucc = true;
        break;
    }
    if(!isSucc){
        if(ressFd > 0) close(ressFd);
        if(modlFd > 0) close(modlFd);
        if(dataFd > 0) close(dataFd);
        ressFd = -1;
        modlFd = -1;
        dataFd = -1;

    }
    else{
        set_fl(modlFd, O_NONBLOCK);
        set_fl(dataFd, O_NONBLOCK);
        if(shutdown(modlFd, SHUT_RD) == -1){
            BLOGE("failed to shutdown the read endpoint of modlfd.\n");
        }
        if(shutdown(dataFd, SHUT_RD) == -1){
            BLOGE("failed to shutdown the read endpoint of datafd.\n");
        }
        if(shutdown(ressFd, SHUT_RD) == -1){
            BLOGE("failed to shutdown the write endpoint of ressfd.\n");
        }
        BLOGE("INFO have create three links to servRec: data, modl, ress.\n");
    }
    return isSucc;
}
void SessionStruct::closeServRec()
{
    pthread_mutex_lock(&fdsLock);
    if(ressFd != -1){
        close(ressFd);
        ressFd = -1;
    }
    if(modlFd != -1){
        close(modlFd);
        modlFd = -1;
    }
    if(dataFd != -1){
        close(dataFd);
        dataFd = -1;
    }
    pthread_mutex_unlock(&fdsLock);
}

static void closeOneLink(int &curfd, pthread_mutex_t &curlock)
{
    struct pollfd fdarr[1];
    pthread_mutex_lock(&curlock);
    if(curfd == -1) {
        pthread_mutex_unlock(&curlock);
        return;
    }
    fdarr[0].fd = curfd;
    fdarr[0].events = POLLOUT;
    int retpoll;
    short errbits = POLLERR | POLLHUP;
    retpoll = poll(fdarr, 1, 0);
    if(retpoll == 1 && fdarr[0].revents & errbits){
        BLOGE("the link being checked is broken, and need to close.\n");
        close(curfd);
        curfd = -1;
    }
    pthread_mutex_unlock(&curlock);
}

void SessionStruct::closeDataLink()
{
    int& curfd = dataFd;
    pthread_mutex_t& curlock = fdsLock;
    closeOneLink(curfd, curlock);
}

void SessionStruct::closeModlLink()
{
    int& curfd = modlFd;
    pthread_mutex_t& curlock = fdsLock;
    closeOneLink(curfd, curlock);
}

static int getfd(const char *srvpath, const char *myname, const  char *name)
{
    int clifd;
    clifd = cli_conn(myname, srvpath);
    if(clifd <=0) {
        BLOGE("failed to connect from %s to %s, err: %s.\n", myname, srvpath, strerror(errno));
        return clifd;
    }
    PckVec pck;
    pck.base = (char*)name;
    pck.len = strlen(name);
    int err;
    if(writen(clifd, &pck, 1, &err, 0) != pck.len){
        BLOGE("failt to write the first packet to connection to unpath %s\n", srvpath);
        close(clifd);
        return 0;
    }
    return clifd;
}

static int getDataFd(const char* servAddr)
{
    int retfd;
    char tmpPath[MAX_PATH];
    snprintf(tmpPath, MAX_PATH, "%s%s", g_csWorkDir, "data");
    retfd = getfd(servAddr, tmpPath, chnlNames[0]);
    if(retfd <= 0){
        return retfd;
    } 
    const unsigned BufLen = 10;
    char buf[BufLen];
    int retr = read(retfd, buf, BufLen -1);
    if(retr <=0){
        BLOGE("failed to read data from new data session, then close it.\n");
        close(retfd);
        return 0;
    }
    buf[retr] = '\0';
    if(strcmp(buf, "READY") != 0){
        BLOGE("unrecognized message from new data session, the link needs to close, msg: %s.\n", buf);
        close(retfd);
        return 0;
    }
    return retfd;
}

static int getModlFd(const char* servAddr)
{
    int retfd;
    char tmpPath[MAX_PATH];
    snprintf(tmpPath, MAX_PATH, "%s%s", g_csWorkDir, "modl");
    retfd = getfd(servAddr, tmpPath, chnlNames[1]);
    if(retfd <= 0){
        return retfd;
    } 
    const unsigned BufLen = 10;
    char buf[BufLen];
    int retr = read(retfd, buf, BufLen -1);
    if(retr <=0){
        BLOGE("failed to read data from new modl session, then close it.\n");
        close(retfd);
        return 0;
    }
    buf[retr] = '\0';
    if(strcmp(buf, "READY") != 0){
        BLOGE("unrecognized message from new modl session, the link needs to close, expected: 'READY'; received: %s.\n", buf);
        close(retfd);
        return 0;
    }
    return retfd;
}

//TODO why to do this? reconnect three sessions synchronously once one of them is broken.
void* maintainSession(void* param)
{
    SessionStruct* pss = (SessionStruct*) param;
    while(pss->getIsRun())
    {
        bool connected = pss->connectServRec();
        if(!connected){
            sleep(3);
        }
        struct pollfd fdarr[1];
        pthread_mutex_lock(&pss->fdsLock);
        fdarr[0].fd = pss->ressFd;
        fdarr[0].events = POLLIN;
        pthread_mutex_unlock(&pss->fdsLock);
        int timeout = 1 * 1000;
        int retpoll;
        short errbits = POLLERR | POLLHUP | POLLNVAL;
        while(pss->getIsRun())
        {
            bool needRecon = false;
            pthread_mutex_lock(&pss->fdsLock);
            needRecon = pss->dataFd == -1 || pss->modlFd == -1;
            pthread_mutex_unlock(&pss->fdsLock);
            if(needRecon) break;
            retpoll = poll(fdarr, 1, timeout);
            if(retpoll <= 0){
                if(!pss->getIsRun()) break;
                if(retpoll == 0){
                    continue;
                }
                else{
                   BLOGE("encounter errors while poll. error: %s\n", strerror(errno)); 
                    break;
                }
            }
            if(fdarr[0].revents & POLLIN){
                Audiz_Result res;
                int retread = read(fdarr[0].fd, &res, sizeof(res));
                if(retread != sizeof(res)){
                    if(retread == 0){
                        BLOGE("maintainSession result session is closed.\n");
                        break;
                    }
                    else{
                        BLOGE("maintainSession read less data than result_struct, len: %u, expect len:%u.\n", retread, sizeof(res));
                    }
                }
                pss->repResAddr(&res);
            }
            if(fdarr[0].revents & errbits){
                BLOGE("the link is broken while polling.\n");
                break;
            }
        }
        pss->closeServRec();
        sleep(3);//avoid reconnecting frequently.
    }
}
const char *chnlNames[3] = {
    "PUT_DATA",
    "PUT_SPKMDL",
    "GET_RESULT"
};

bool SessionStruct::writeData(unsigned long long id, char *buf, unsigned len)
{
    bool ret = false;
    PckVec pcks[4];
    pcks[0].base = (char *)&id;
    pcks[0].len = sizeof(unsigned long long);
    pcks[1].base = (char*)dataRedunArr;
    pcks[1].len = DATAREDUNSIZE;
    pcks[2].base = (char *)&len;
    pcks[2].len = sizeof(unsigned);
    pcks[3].base = buf;
    pcks[3].len = len;
    pthread_mutex_lock(&fdsLock);
    int datafd = this->dataFd;
    pthread_mutex_unlock(&fdsLock);
    if(datafd == -1){
        BLOGE("no client being connected to consume the data.\n");
        return ret;
    }

    int cls = 0;
    writen(datafd, pcks, 4, &cls, 0);
    if(cls != 0){
        if(cls == -1){
            closeDataLink();
        }
        else {
            BLOGE("the data link becomes congested, try count: %d\n", cls);       
            ret =true;
        }
    }
    else{
        ret = true;
    }

    return ret;
}

 */

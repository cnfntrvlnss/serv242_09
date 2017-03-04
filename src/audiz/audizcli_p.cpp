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
#include <signal.h>

#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cerrno>
#include<iostream>
using namespace std;

//#include "globalfunc.h"
#include "audizcli_p.h"
#include "../apueclient.h"

#ifdef _ZSUMMER_LOG4Z_H_
#define BLOGE(fmt, ...) LOGFMT_ERROR(g_logger, fmt, ##__VA_ARGS__)
#define BLOGW(fmt, ...) LOGFMT_WARN(g_logger, fmt, ##__VA_ARGS__)
#define BLOGI(fmt, ...) LOGFMT_INFO(g_logger, fmt, ##__VA_ARGS__)
#define BLOGD(fmt, ...) LOGFMT_DEBUG(g_logger, fmt, ##__VA_ARGS__)
#define BLOGT(fmt, ...) LOGFMT_TRACE(g_logger, fmt, ##__VA_ARGS__)
#else
enum BLOG_LEVEL{BLOGT, BLOGD, BLOGI, BLOGW, BLOGE};
#define BLOGLMT BLOGI
#define BIFO(x) if(BLOGLMT <= BLOG##x) 
#define BLOGE(fmt, ...)  BIFO(E) fprintf(stderr, "ERROR " fmt "\n", ##__VA_ARGS__)
#define BLOGW(fmt, ...) BIFO(W) fprintf(stderr, "WARN " fmt "\n", ##__VA_ARGS__)
#define BLOGI(fmt, ...) BIFO(I) fprintf(stderr, "INFO " fmt "\n", ##__VA_ARGS__)
#define BLOGD(fmt, ...) BIFO(D) fprintf(stderr, "DEBUG " fmt "\n", ##__VA_ARGS__)
#define BLOGT(fmt, ...) BIFO(T) fprintf(stderr, "TRACE " fmt "\n", ##__VA_ARGS__)
#endif 

static char g_csWorkDir[MAX_PATH] = "ioacases/";

/**
 * execute command synchronizely.
 *
 * communicate with servtask thread, with two shared spaces respectively for task and result.
 * note: result should be freed manually.
 */
 int SessionStruct::procExecCommonCfgCmd(std::vector<AZ_PckVec>& task, Audiz_PResult &result)
{
    pthread_mutex_lock(&cfgCmdLock);
    while(cfgCmdTask.size() > 0){
        pthread_cond_wait(&cfgCmdTaskEmptyCond, &cfgCmdLock);
    }

    cfgCmdTask.insert(cfgCmdTask.end(), task.begin(), task.end());
    cfgCmdResult.reset();
    pthread_mutex_lock(&modlFdLock);
    int fd = modlFd;
    pthread_mutex_unlock(&modlFdLock);
    int err;
    if(fd > 0){
        writen(fd, reinterpret_cast<PckVec*>(&cfgCmdTask[0]), cfgCmdTask.size(), &err, 0);
        if(err < 0){
            BLOGE("SessionStruct::procExecCommon error write cmd to cfg link. error: %s.", strerror(errno));
        }
        else{
            while(cfgCmdResult.head.type == 0){
                pthread_cond_wait(&cfgCmdResultSetCond, &cfgCmdLock);
            }
        }
    }
    else{
        BLOGW("SessionStruct::procExecCommon modl link is not opened.");
        err = -1;
    }

    if(err < 0){
        cfgCmdResult.head.type = CHARS_AS_INIT32(const_cast<char*>(cfgCmdTask[0].base)) + 1;
        cfgCmdResult.head.ack = -1;
    }
    cfgCmdTask.clear();
    result = cfgCmdResult;
    cfgCmdResult.reset();
    pthread_mutex_unlock(&cfgCmdLock);
    pthread_cond_broadcast(&cfgCmdTaskEmptyCond);
    return 0;
}


/**
 * ignore all input, and only for detecting link breaking.
 *
 */
bool SessionStruct::prochandleDataResp(int fd)
{
    string tmpbuf;
    tmpbuf.resize(1024);
    char *ptr = const_cast<char*>(tmpbuf.c_str());
    bool bret = true;
    unsigned ntol = 0;
    while(true){
        int nread = read(fd, ptr, 1024);
        if(nread < 0){
            int curerr = errno;
            if(curerr == EAGAIN){
                break;
            }
            else{
                BLOGE("prochandledataresp read error. error: %s.", strerror(curerr));
                bret = false;
                break;
            }
        }
        else if(nread == 0){
            BLOGE("prochandledataresp read eof.");
            bret = false;
            break;
        }
        ntol += ntol;
    }
    if(ntol > 0){
        BLOGW("prochandledataresp ignore data from server. size=%u.", ntol);
    }
    if(bret){
        closeDataLink(fd);
    }
    return bret;
}

/**
 * recv result, recv response for commonCfgCmd.
 *
 */
bool SessionStruct::prochandleResp(int fd)
{
    Audiz_PResult azres;
    //Audiz_PResult_Head azreshd;
    vector<AZ_PckVec> vecResult;
    azres.head.pack_r(vecResult);
    int restype = AZOP_INVALID_MARK;
    pthread_mutex_lock(&cfgCmdLock);
    if(cfgCmdTask.size() > 0){
        restype = CHARS_AS_INIT32(const_cast<char*>(cfgCmdTask[0].base)) + 1;
    }

    bool bret = true;
    while(true){
        int err;
        readn(fd, reinterpret_cast<PckVec*>(&vecResult[0]), 2, &err, 0);
        if(err < 0){
            BLOGE("SessionStruct::prochandleResp error read head of response of cmd from modl link. error: %s.", strerror(errno));
            bret = false;
            break;
        }
        if(err > 0){
            BLOGW("SessionStruct::prochandleResp have tried %u times to read response.", err);
        }
        unsigned arglen = azres.getArgLen();
        if(arglen > 0){
            PckVec argPck;
            argPck.base = (char*)malloc(arglen);
            argPck.len = arglen;
            readn(fd, &argPck, 1, &err, 0);
            if(err < 0){
                BLOGE("SessionStruct::prochandleResp error read arg part of response of cmd from modl link. error: %s.", strerror(errno));
                free(argPck.base);
                bret = false;
                break;
            }
            
            if(azres.head.type == AZOP_REC_RESULT){
                unsigned reslen = arglen / sizeof(Audiz_Result);
                Audiz_Result *ress = reinterpret_cast<Audiz_Result*>(argPck.base);
                for(unsigned idx=0; idx < reslen; idx++){
                    BLOGT("Project Result from modl. PID=%lu TargetID=%u.", ress[idx].m_iPCBID, ress[idx].m_iTargetID);
                    repResAddr(&ress[idx]);
                    
                }
                free(argPck.base);
            }
            else if(azres.head.type != restype){
                free(argPck.base);
            }
            else{
                azres.argBuf = argPck.base;
            }
        }
        else if(arglen < 0){
            BLOGE("SessionStruct::prochandleResp parse error packet, then close the link.");
            bret = false;
            break;
        }

        if(azres.head.type != AZOP_REC_RESULT && azres.head.type != restype){
            BLOGE("SessionStruct::prochandleResp unexpected response from server. type: %d; ack: %d.", azres.head.type, azres.head.ack);
        }
        if(restype != AZOP_INVALID_MARK){
            if(restype != azres.head.type){
                 continue;      
            }
        }
        break;
    }
    if(bret == false){
        //in most cases, the link needs be closed here.
        if(restype != AZOP_INVALID_MARK){
            azres.head.type = restype;
            azres.head.ack = -1;
        }
        closeModlLink(fd);
    }

    pthread_mutex_unlock(&cfgCmdLock);
    if(restype != AZOP_INVALID_MARK){
        assert(azres.head.type == restype);    
        cfgCmdResult = azres;
        pthread_cond_broadcast(&cfgCmdResultSetCond);
    }

    return bret;
}

static int getModlLinkFd(const char* servAddr)
{
    int retfd;
    char myPath[MAX_PATH];
    snprintf(myPath, MAX_PATH, "%s%s", g_csWorkDir, "modl");
    retfd = cli_conn(myPath, servAddr); 
    if(retfd <= 0){
       BLOGE("getCfgLinkFd failed to connect from %s to %s, ret: %d; error: %s.", myPath, servAddr, retfd, strerror(errno));
       return retfd;
    }
    BLOGT("getModlLinkFd have created the link, client: %s; server: %s.", myPath, servAddr);
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
    readn(retfd, retPcks, 3, &err, 0);
    if(err < 0){
        BLOGE("getCfgLinkFd failed to read response from server, error: %s.", strerror(errno));
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
       BLOGE("getDataLinkFd failed to connect from %s to %s, ret: %d; error: %s.", myPath, servAddr, retfd, strerror(errno));
       return retfd;
    }
    BLOGT("getDataLinkFd have created the link, client: %s; server: %s.", myPath, servAddr);
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
        BLOGE("getDataLinkFd failed to read response from server, error: %s.", strerror(errno));
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

int SessionStruct::checkDataFd(bool btry)
{
    pthread_mutex_lock(&dataFdLock);
    if(dataFd == -1 && btry){
        int fd = getDataLinkFd(servPath);
        if(fd > 0) dataFd = fd;
    }
    int ret = dataFd;
    pthread_mutex_unlock(&dataFdLock);
    return ret;
}

int SessionStruct::checkModlFd(bool btry)
{
    pthread_mutex_lock(&modlFdLock);
    if(modlFd == -1 && btry){
        int fd = getModlLinkFd(servPath);
        if(fd > 0){
             modlFd = fd;   
        }
    }
    int ret = modlFd;
    pthread_mutex_unlock(&modlFdLock);
    return ret;
}



static void emptysighandler(int signo)
{
    fprintf(stderr, "signal %d catched.\n", signo);
}

/**
 * invoked immediately after establishing modl link.
 * send all sample sequentially, finally send a end mark msg and wait for ack.
 *
 */
static bool sendToServerAllSmps(SpkMdlStVec *vec, int fd, unsigned &acculen)
{
    bool bret = true;
    vector<AZ_PckVec> pcks;
    Audiz_PRequest_Head req;
    req.type = AZOP_ADD_SAMPLE;
    SpkMdlStVec::iterator *it = vec->iter();
    int err;
    acculen = 0;
    for(SpkMdlSt* mdl= it->next(); mdl != NULL; mdl= it->next()){
        pcks.clear();
        req.addLen = 1;
        req.pack_w(pcks);
        mdl->pack_w(pcks);
        writen(fd, reinterpret_cast<PckVec*>(&pcks[0]),pcks.size(), &err, 0);
        if(err < 0){
            BLOGE("SendToServerAllSmps error occurs while writing mdl. idx=%u name=%s error=%s.", acculen, mdl->head, strerror(errno));
            return false;
        }
        acculen ++;
    }
    return true;
}

bool SessionStruct::feedAllSamples_inner()
{
    unsigned acculen;
    if(!sendToServerAllSmps(this->retMdlsAddr, this->modlFd, acculen)){
        return false;
    }

    Audiz_PRequest_Head req;
    req.type = AZOP_ADD_SAMPLE;
    vector<AZ_PckVec> pcks;
    req.addLen = 0;
    req.pack_w(pcks);
    int err;
    writen(this->modlFd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        BLOGE("feedAllSamples_inner error occures while writing msg of finishing adding. error=%s.", strerror(errno));
        return false;
    }
    Audiz_PResult res;
    res.reset();
    pcks.clear();
    res.head.pack_r(pcks);
    readn(this->modlFd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &err, 0);
    if(err < 0){
        BLOGE("feedAllSamples_inner error occures while reading response of msg of finishing adding. error=%s.", strerror(errno));
        return false;
    }
    if(res.head.type != AZOP_ADD_SAMPLE + 1){
        BLOGE("feedAllSamples_inner read unrecognized response while wait for response of msg of finishing adding.");
        return false;
    }
    if(res.head.ack != acculen){
        BLOGW("feedAllSamples_inner the num of received samples in server in not consistent of that send by client.");
    }
    return true;
}

void SessionStruct::feedAllSamples()
{
    setHasSamples();
    unsigned acculen;
    if(!sendToServerAllSmps(this->retMdlsAddr, this->modlFd, acculen)){
        return;
    }
    Audiz_PRequest_Head req;
    req.type = AZOP_ADD_SAMPLE;
    req.addLen = 0;
    vector<AZ_PckVec> pcks;
    req.pack_w(pcks);
    Audiz_PResult res;
    procExecCommonCfgCmd(pcks, res);

}
/**
 * 
 *
 */
void* maintainSession_ex(void* param)
{
    if(0)
    {
        //if we want poll can be interrepted by other than fd event, use the following code.
        sigset_t mask;
        sigset_t oldmask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCONT);
        int retp = pthread_sigmask(SIG_BLOCK, &mask, &oldmask);
        if(retp != 0){
            BLOGE("maintain session thread pthread_sigmask error!");
            exit(1);
        }
        struct sigaction act, oact;
        act.sa_handler = emptysighandler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        if(sigaction(SIGCONT, &act, &oact) < 0){
            BLOGE("maintain session thread sigaction with sigcont failed!");
            exit(1);
        }
    }
    SessionStruct *pss = (SessionStruct*) param;
    struct pollfd fds[2];

    while(pss->getIsRun()){
        int curmodlfd = pss->checkModlFd(false);
        if(curmodlfd < 0){
            curmodlfd = pss->checkModlFd(true);
            if(curmodlfd < 0){
                struct timeval tm;
                tm.tv_sec = 3;
                tm.tv_usec = 0;
                select(0, NULL, NULL, NULL, &tm);
                continue;
            }
            else{
                if(pss->getHasSamples() && !pss->feedAllSamples_inner()){
                    pss->closeModlLink(curmodlfd);
                    continue;
                }
            }
        }

        fds[0].fd = curmodlfd;
        fds[0].events = POLLIN;
        int curdatafd = pss->checkDataFd(true);
        if(curdatafd < 0){
            struct timeval tm;
            tm.tv_sec = 3;
            tm.tv_usec = 0;
            select(0, NULL, NULL, NULL, &tm);
            continue;
        }
        fds[1].fd = curdatafd;
        fds[1].events = POLLIN;
        pss->setConnected(true);
        BLOGI("maintainSession_ex start waiting in modl file descriptor...");
        int wmillisecs = 60 * 1000;
        struct timespec sttimeout;
        sttimeout.tv_sec = wmillisecs / 1000;
        sttimeout.tv_nsec = (wmillisecs - sttimeout.tv_sec * 1000) * 1000000;
        int retp = poll(fds, 2, wmillisecs);
        //int retp = ppoll(fds, 1, &sttimeout, &oldmask);
        if(retp > 0){
            bool bbreak = false;
            if(fds[0].revents != 0){
                BLOGT("maintainSession_ex modl link has data to read.");
                if(!pss->prochandleResp(fds[0].fd)){
                    bbreak = true;
                }
            }
            if(fds[1].revents != 0){
                BLOGT("maintainSession_ex data link has data to read.");
                if(!pss->prochandleDataResp(fds[1].fd)){
                    bbreak = true;
                }
            }
            if(bbreak) continue;
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
    }

    return NULL;
}


SessionStruct::SessionStruct(const char* servPath, AUDIZ_REPORTRESULTPROC resFunc, SpkMdlStVec * getModlsFunc)
{
    repResAddr = resFunc;
    retMdlsAddr = getModlsFunc;
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
    pthread_mutex_init(&modlFdLock, NULL);
    pthread_mutex_init(&dataFdLock, NULL);
    isRunning = true;
    pthread_mutex_init(&isRunLock, NULL);
    pthread_mutex_init(&cfgCmdLock, NULL);
    pthread_cond_init(&cfgCmdResultSetCond, NULL);
    pthread_cond_init(&cfgCmdTaskEmptyCond, NULL);
    bConnected = false;
    bHasSamples = false;
    pthread_mutex_init(&hasSamplesLock, NULL);
    int readp = pthread_create(&modlThreadId, NULL, maintainSession_ex, this);
    if(readp < 0){
        BLOGE("SessionStruct::SessionStruct failed to create thread maintainSession.\n");
        exit(1);
    }
}

SessionStruct::~SessionStruct()
{
    setIsRun(false);
    //pthread_kill(modlThreadId, SIGCONT);
    pthread_cancel(modlThreadId);
    pthread_join(modlThreadId, NULL);
    pthread_mutex_destroy(&isRunLock);
    pthread_mutex_destroy(&dataFdLock);
    pthread_mutex_destroy(&modlFdLock);
    pthread_mutex_destroy(&hasSamplesLock);
}


void SessionStruct::closeDataLink(int fd)
{
    close(dataFd);
    pthread_mutex_lock(&dataFdLock);
    if(fd == dataFd) dataFd = -1;
    pthread_mutex_unlock(&dataFdLock);
}
void SessionStruct::closeModlLink(int fd)
{
    close(fd);
    pthread_mutex_lock(&modlFdLock);
    if(fd == modlFd)modlFd = -1;
    pthread_mutex_unlock(&modlFdLock);
}

static pthread_mutex_t sendDataLock = PTHREAD_MUTEX_INITIALIZER;
bool SessionStruct::writeData(Audiz_WaveUnit *unit)
{
    int fd = checkDataFd(false);
    if(fd < 0){
        BLOGE("sessionstruct::writedata PID=%lu SIZE=%u data link is not established, discard this packet.", unit->m_iPCBID, unit->m_iDataLen);
        return false;
    }
    vector<AZ_PckVec> pcks;
    unit->pack_w(pcks);
    bool bret = false;

    bret = true;
    int cls = 0;
    pthread_mutex_lock(&sendDataLock);
    writen(fd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &cls, 0);
    pthread_mutex_unlock(&sendDataLock);
    if(cls != 0){
        if(cls == -1){
            //closeDataLink(fd);
            BLOGE("sessionstruct::writedata data link is broken.");
            bret = false;
        }
        else {
            BLOGE("data link becomes congested, try count: %d", cls);       
        }
    }

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
    req.type = AZOP_ADDRM_SAMPLE;
    req.addLen = 1;
    req.pack_w(pcks);
    unsigned st = pcks.size();
    tmpSpk.pack_w(pcks);
    for(unsigned idx = st; idx < pcks.size(); idx++){
        req.addLen += pcks[idx].len;
    }
    BLOGT("sessionstruct::writeSample start procExecCommonCfgCmd in server. name: ", name);
    Audiz_PResult res;
    procExecCommonCfgCmd(pcks, res);
    BLOGT("sessionstruct::writeSample result from procExecCommonCfgCmd %d.", res.head.ack);
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
    req.pack_w(pcks);
    Audiz_PResult res;
    BLOGI("sessionstruct::querysamples start querying all samples in server.");
    procExecCommonCfgCmd(pcks, res);
    if(res.argBuf != NULL){
        for(int idx=0; idx < res.getArgLen(); idx+= SPKMDL_HDLEN){
            smps.push_back(&res.argBuf[idx]);
        }
        free(res.argBuf);
    }

    return true;
}
unsigned SessionStruct::queryUnfinishedProjNum()
{
    Audiz_PRequest_Head req;
    req.type = AZOP_QUERY_PROJ;
    req.addLen = 0;
    vector<AZ_PckVec> pcks;
    req.pack_w(pcks);
    Audiz_PResult res;
    BLOGT("sessionstruct::queryprojnum start procExecCommonCfgCmd in server.");
    procExecCommonCfgCmd(pcks, res);
    BLOGT("sessionstruct::queryprojnum result from procExecCommonCfgCmd %d.", res.head.ack);
    assert(res.argBuf == NULL);
    if(res.head.ack >= 0){
        return res.head.ack;
    }
    else{
        BLOGE("query unfinshed failed.");
        return 1;
    }
}


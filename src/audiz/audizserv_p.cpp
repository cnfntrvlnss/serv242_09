/*************************************************************************
	> File Name: audizserv_p.cpp
	> Author: 
	> Mail: 
	> Created Time: Thu 16 Feb 2017 07:28:42 AM EST
 ************************************************************************/

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include<iostream>
using namespace std;

#include "BufferProject_fork.h"
#include "../audizstruct.h"
#include "../apueclient.h"
#include "globalfunc.h"

unsigned long g_SessID = 0;
int g_DataFd = -1;
int g_ModlFd = -1;
pthread_cond_t g_DataFdCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t g_DataFdLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_ModlFdCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t g_ModlFdLock = PTHREAD_MUTEX_INITIALIZER;

static bool setDataFd(int fd, unsigned long sid)
{
    if(sid != g_SessID){
        LOG4CPLUS_ERROR(g_logger, "setDataFd new id is not equal with old id, refuse it. id: "<< sid<<"; sid: " << g_SessID);
        return false;
    }
    pthread_mutex_lock(&g_DataFdLock);
    if(g_DataFd > 0){
        close(g_DataFd);
        g_DataFd = -1;
    }
    g_DataFd = fd;
    pthread_mutex_unlock(&g_DataFdLock);
    return true;
}

static void clearDataFd(int afd)
{
    pthread_mutex_lock(&g_DataFdLock);
    if(afd == g_DataFd){
        close(g_DataFd);
        g_DataFd = -1;
    }
    pthread_mutex_unlock(&g_DataFdLock);
}

static bool setModlFd(int mfd, unsigned long id)
{
    pthread_mutex_lock(&g_ModlFdLock);
    if(g_ModlFd > 0){
        LOG4CPLUS_INFO(g_logger, "close old modl link, for accepting new link. old sid:"<< g_SessID<< "; new sid: "<< id<< ".");
        close(g_ModlFd);
        g_ModlFd = -1;
    }
    g_ModlFd = mfd;
    g_SessID = id;
    pthread_mutex_unlock(&g_ModlFdLock);
    return true;
}
static void clearModlFd(int mfd)
{
    pthread_mutex_lock(&g_ModlFdLock);
    if(mfd == g_ModlFd){
        close(g_ModlFd);
        g_ModlFd = -1;
    }
    pthread_mutex_unlock(&g_ModlFdLock);
}

static inline size_t readn(int fd, vector<AZ_PckVec> &pcks, int *err, int istry)
{
    size_t retr = readn(fd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), err, istry);
    return retr;
}

static inline size_t readn(int fd, string *vals, unsigned len, int *err, int istry)
{
    vector<PckVec> pcks;
    pcks.resize(len);
    for(unsigned idx=0; idx < pcks.size(); idx++){
        pcks[idx].base = const_cast<char*>(vals[idx].c_str());
        pcks[idx].len = vals[idx].size();
    }
    return readn(fd, &pcks[0], pcks.size(), err, istry);
}

static inline size_t writen(int fd, vector<AZ_PckVec> &pcks, int *err, int istry)
{
    return writen(fd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), err, istry);
}
//static pthread_mutex_t g_ModlFdLock = PTHREAD_MUTEX_INITIALIZER;
static inline size_t writen_s(int fd, AZ_PckVec* pcks, unsigned len, int *err, int istry)
{
    pthread_mutex_lock(&g_ModlFdLock);
    size_t retw = writen(fd, reinterpret_cast<PckVec*>(pcks), len, err, istry);
    pthread_mutex_unlock(&g_ModlFdLock);
    return retw;
}

static bool parseSpkMdlName(char *tmpBuf, unsigned tok1, unsigned tok2, unsigned tok3)
{
    char *pch;
    char *pnu;
    pch = strtok(tmpBuf, "_");
    tok1 = strtol(pch, &pnu, 0);
    if(*pnu != '\0') return false;
    pch = strtok(NULL, "_");
    tok2 = strtol(pch, &pnu, 0);
    if(*pnu != '\0') return false;
    pch = strtok(NULL, ".");
    tok3 = strtol(pch, &pnu, 0);
    if(*pnu != '\0') return false;
    if(strcmp(pnu+1, "param") != 0) return false;
    return true;
}

static string g_tmpAudioData;
/*****************************************************
 * if receive project whose id is 0, then discard it;
 * if receive data with length 0, then regard it as finishing transfering this project.
 *
 * return false if the link is broken.
 *
 *****************************************************/
static bool procDataReceived(int dataFd)
{
    char redunarr[DATAREDUNSIZE];
    vector<AZ_PckVec> pcks;
    Audiz_WaveUnit unit;
    unit.pack_r(pcks, redunarr);
    int err;
    int cntLimit = 10;
    while(cntLimit-- > 0){
        int retr = readn(dataFd, pcks, &err, 1);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "the data link has broken, and close it. __LINE__: "<< __LINE__);
            return false;
        }
        else if(err > 0){
            LOG4CPLUS_ERROR(g_logger, "while readn for wave head, try "<< err<< " times.");
        }
        if(retr == 0){
             break;   
        }
        if(!unit.isValid(pcks)){
            return false;
        }
        if(unit.m_iDataLen == 0){
            notifyProjFinish(unit.m_iPCBID);
            continue;
        }

        g_tmpAudioData.resize(unit.m_iDataLen);
        readn(dataFd, &g_tmpAudioData, 1, &err, 0);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "the data link has broken, and close it. __LINE__: "<< __LINE__);
            return false;
        }
        else if(err > 0){
            LOG4CPLUS_ERROR(g_logger, "while readn for wave data, try "<< err<< " times.");
        }
        if(!recvProjSegment(unit.m_iPCBID, const_cast<char*>(g_tmpAudioData.c_str()), g_tmpAudioData.size())){
            LOG4CPLUS_ERROR(g_logger, "procDataReceived loss one data unit. pid="<< unit.m_iPCBID<< "; size="<< g_tmpAudioData.size());
        }
        else{
            LOG4CPLUS_DEBUG(g_logger, "PID="<< unit.m_iPCBID<< " new data arrived len="<< unit.m_iDataLen);
        }
    }
    return true;
}

/**
 *
 * TODO do some to verify it's ok to do write and read with the same fd concurrently. 
 */
static bool procModlReceived(int mdlFd)
{
    Audiz_PRequest_Head reqhd;
    vector<AZ_PckVec> pcks;
    reqhd.pack_r(pcks);
    int err;
    size_t retr = readn(mdlFd, pcks, &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "procModlReceived error occurs while reading Audiz_PRequest_Head.");
        return false;
    }
    else if(err > 0){
        LOG4CPLUS_ERROR(g_logger, "procModlReceived reading Audiz_PRequest_Head have tried "<< err<< " times.");
    }

    if(reqhd.type == AZOP_QUERY_SAMPLE){
        //TODO response with head of all samples.
        Audiz_PResult_Head reshd;
        reshd.type = AZOP_QUERY_SAMPLE + 1;
        reshd.ack = 0;
        vector<AZ_PckVec> pcks;
        reshd.pack_w(pcks);
        int err;
        writen_s(mdlFd, &pcks[0], pcks.size(), &err, 0);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "procModlReceived while processing AZOP_QUERY_SAMPLE failed to write Audiz_PResult_Head.");
            return false;
        }
        else{
            LOG4CPLUS_INFO(g_logger, "procModlReceived processing AZOP_QUERY_SAMPLE success.");
        }
    }
    else if(reqhd.type == AZOP_ADD_SAMPLE){
        //TODO add/del sample.
        Audiz_PResult_Head reshd;
        reshd.type = AZOP_ADD_SAMPLE + 1;
        reshd.ack = 0;
        vector<AZ_PckVec> pcks;
        reshd.pack_w(pcks);
        int err;
        writen_s(mdlFd, &pcks[0], pcks.size(), &err, 0);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "procModlReceived while processing AZOP_ADD_SAMPLE failed to write Audiz_PResult_Head.");
            return false;
        }
        else{
            LOG4CPLUS_INFO(g_logger, "procModlReceived processing AZOP_ADD_SAMPLE success.");
        }
    }
    else if(reqhd.type == AZOP_QUERY_PROJ){
        Audiz_PResult_Head reshd;
        reshd.type = AZOP_QUERY_PROJ + 1;
        reshd.ack = queryProjNum();
        vector<AZ_PckVec> pcks;
        reshd.pack_w(pcks);
        writen_s(mdlFd, &pcks[0], pcks.size(), &err, 0);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "procModlReceived while processing AZOP_QUERY_PROJ failed to write Audiz_PResult_Head.");
            return false;
        }
        else{
            LOG4CPLUS_INFO(g_logger, "procModlReceived processing AZOP_QUERY_PROJ success.");
        }
    }
    else {
        LOG4CPLUS_ERROR(g_logger, "procModlReceived unrecognized Audiz_PRequest_Head. type: "<< reqhd.type);
        return false;
    }
    return true;
}

/**
 * return true if a specified link is got, return false otherwise.
 *
 */
//bool implProtoAtAcceptLink(int servfd, int &clifd, char &which)
bool procImplAcceptLink(int servfd)
{
    uid_t uid;
    int tmpfd = serv_accept(servfd, &uid);
    if(tmpfd < 0){
        fprintf(stderr, "procImplAcceptLink serv_accept error. error: %s.\n", strerror(errno));
        return false;
    }
    Audiz_LinkResponse res;
    vector<AZ_PckVec> pcks;
    res.req.pack_r(pcks);
    int nread;
    int errr = 0;
    int errw = 0;
    bool bsucc = false;
    while(true){
        nread = readn(tmpfd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), &errr, 0);
        if(errr < 0){
            LOG4CPLUS_ERROR(g_logger, "procImplAcceptLink error occures while read first package from new link. error: "<< strerror(errno));
             break;   
        }
        if(strcmp(res.req.head, AZ_CFGLINKNAME) == 0){
            LOG4CPLUS_DEBUG(g_logger, "procImplAcceptLink begin establishing a new data link.");
            strcpy(res.ack, AZ_LINKBUILDOK);
            res.pack_w(pcks);
            writen(tmpfd, pcks, &errw, 0);
            if(errw <0) break;
            if(setModlFd(tmpfd, res.req.sid)){
                bsucc = true;
            }
        }
        else if(strcmp(res.req.head, AZ_DATALINKNAME) == 0){
            LOG4CPLUS_DEBUG(g_logger, "procImplAcceptLink begin establishing a new modl link.");
            strcpy(res.ack, AZ_LINKBUILDOK);
            res.pack_w(pcks);
            writen(tmpfd, pcks, &errw, 0);
            //data link should be nonblocking.
            set_fl(tmpfd, O_NONBLOCK);
            if(errw <0) break;
            if(setDataFd(tmpfd, res.req.sid)){
                bsucc = true;
            }
        }
        else{
            LOG4CPLUS_WARN(g_logger, "procImplAcceptLink unrecognized new link, name: "<< res.req.head);
        }
        break;
    }
    if(!bsucc){
        LOG4CPLUS_ERROR(g_logger, "procImplAcceptLink failed to process the new link.");
        close(tmpfd);
    }
    return true;
}

/**
 * accept new client link from serv link, place the link in right place.
 *
 */
void *servRec_loop(void *param)
{
    const char *unPath = AZ_DATACENTER;
    int servfd = serv_listen(unPath);
    if(servfd < 0){
        LOG4CPLUS_ERROR(g_logger, "serv_listen error. path: "<< unPath<< "; error: "<< strerror(errno));
        exit(1);
    }
    LOG4CPLUS_INFO(g_logger, "serv_listen at path "<< unPath);
    while(true){
        int modlidx = 0;
        int dataidx = 0;
        struct pollfd fdarr[3];
        fdarr[0].fd = servfd;
        fdarr[0].events = POLLIN;
        int fdlen = 1;
        if(g_ModlFd > 0){
            fdarr[fdlen].fd = g_ModlFd;
            fdarr[fdlen].events = POLLIN;
            modlidx = fdlen;
            fdlen ++;
        }
        if(g_DataFd > 0){
            fdarr[fdlen].fd = g_DataFd;
            fdarr[fdlen].events = POLLIN;
            dataidx = fdlen;
            fdlen ++;
        }
        int retpoll = poll(fdarr, fdlen, -1);
        if(retpoll == 0){
            continue;
        }
        else if(retpoll < 0){
            fprintf(stderr, "encounter errors while polling servfd. error: %s\n", strerror(errno));
            break;
        }
        short errbits = fdarr[0].revents & (POLLERR | POLLHUP | POLLNVAL);
        if(errbits){
            fprintf(stderr, "the serv link is broken while polling. errbits: %d\n", errbits);
            break;
        }
        if(fdarr[0].revents & POLLIN){
            procImplAcceptLink(servfd);
        }
        
        if(modlidx > 0 && fdarr[modlidx].revents != 0){
            bool bclose = false;
            int curidx = modlidx;
            if(fdarr[curidx].revents & POLLIN ) {
                if(!procModlReceived(fdarr[curidx].fd)){
                    bclose = true;   
                }
            }
            short errbits = fdarr[curidx].revents & (POLLERR | POLLHUP | POLLNVAL);
            if(!bclose && errbits){
                LOG4CPLUS_ERROR(g_logger, "the modl link is broken while polling, fd: "<< fdarr[curidx].fd<< "; errbits: "<< errbits<< ".");
                bclose = true;
            }
            if(bclose){
                clearModlFd(fdarr[curidx].fd);
            }
        }
        
        if(dataidx > 0 && fdarr[dataidx].revents != 0){
            bool bclose = false;
            int curidx = dataidx;
            if(fdarr[curidx].revents & POLLIN ){
                if(!procDataReceived(fdarr[curidx].fd)){
                    bclose = true;
                } 
            }
            short errbits = fdarr[curidx].revents & (POLLERR | POLLHUP | POLLNVAL);
            if(!bclose && errbits){
                LOG4CPLUS_ERROR(g_logger, "the data link is broken while polling, fd: "<< fdarr[curidx].fd<< "; errbits: "<< errbits<< ".");
                bclose = true;
            }
            if(bclose){
                clearDataFd(fdarr[curidx].fd);
            }
        }
    }
    return NULL;
}

#define handle_error(x, msg) do{perror(msg); exit(EXIT_FAILURE);} while(0)
int main(int argc, char* argv[])
{
    pthread_t servThrdId;
    int retp = pthread_create(&servThrdId, NULL, servRec_loop, NULL);
    if(retp !=0 ){
        handle_error(retp ,"pthread_create");
    }
    pthread_join(servThrdId, NULL);
    exit(EXIT_SUCCESS);
    return 0;
}

/*

void *onlineConsume_loop(void *param)
{
    struct pollfd fdarr[2];
    while(true){
        pthread_mutex_lock(&g_DtMdFdLock);
        while(g_DataFd == 0 || g_ModlFd== 0){
            pthread_cond_wait(&g_DtMdFdCond, &g_DtMdFdLock);
        }
        fdarr[0].fd = g_DataFd;
        fdarr[0].events = POLLIN;
        fdarr[1].fd = g_ModlFd;
        fdarr[1].events = POLLIN;
        pthread_mutex_unlock(&g_DtMdFdLock);

        while(true){
            int retpoll = poll(fdarr, 2, -1);
            if(retpoll <= 0){
                if(retpoll == 0){
                    LOG4CPLUS_WARN(g_logger, "poll returns with no fd ready and no error happening.");
                    continue;
                }
                else{
                    LOG4CPLUS_WARN(g_logger, "poll returns with error happening, choosing to resume the call of poll.");
                    continue;
                }
            }
            if(fdarr[0].revents != 0){
                bool bclose = false;
                if(fdarr[0].revents & POLLIN ){
                    if(!procDataReceived(fdarr[0].fd)){
                        bclose = true;
                    } 
                }
                short errbits = fdarr[0].revents & (POLLERR | POLLHUP | POLLNVAL);
                if(errbits){
                    LOG4CPLUS_ERROR(g_logger, "the data link is broken while polling, fd: "<< fdarr[0].fd<< "; errbits: "<< errbits<< ".");
                    bclose = true;
                }
                if(bclose){
                    clearDataFd(fdarr[0].fd);
                }
            }
            if(fdarr[1].revents != 0){
                bool bclose = false;
                if(fdarr[1].revents & POLLIN ) {
                    if(!procModlReceived(fdarr[1].fd)){
                        bclose = true;   
                    }
                }
                short errbits1 = fdarr[1].revents & (POLLERR | POLLHUP | POLLNVAL);
                if(errbits1){
                    LOG4CPLUS_ERROR(g_logger, "the modl link is broken while polling, fd: "<< fdarr[0].fd<< "; errbits: "<< errbits1<< ".");
                    bclose = true;
                }
                if(bclose){
                    clearModlFd(fdarr[1].fd);
                }
            }
        }
    }
}
void checkFdAndCloseBroken(int &curfd, pthread_mutex_t &curlock)
{
    pthread_mutex_lock(&curlock);
    struct pollfd fdarr[1];
    fdarr[0].fd = curfd;
    fdarr[0].events = POLLIN;
    poll(fdarr, 1, 0);
    if(fdarr[0].revents & (POLLERR | POLLHUP)){
        LOG4CPLUS_INFO_FMT(g_logger, "the current fd is broken through checking by poll, and needs to close, fd: %d.", curfd);
        close(curfd);
        curfd = 0;
    }
    else{
        LOG4CPLUS_WARN_FMT(g_logger, "the current fd is checked ok, and does not need to close, fd: %d", curfd);
    }
    pthread_mutex_unlock(&curlock);
}
bool establishDataLink(int clifd)
{
    const char *rspStr = "READY";
    unsigned rspLen = 5;
    int retw = write(clifd, rspStr, rspLen);
    if(retw != rspLen){
        fprintf(stderr, "error occures while write 'OK' to data link.\n");
        return false;
    }
    if(shutdown(clifd, SHUT_WR) == -1){
        fprintf(stderr, "fail to shutdown writing endpoint of data link, error: %s.\n", strerror(errno));
    }
    return true;
}
bool establishModelLink(int clifd)
{
    const char *rspStr = "READY";
    unsigned rspLen = 5;
    int retw = write(clifd, rspStr, rspLen);
    if(retw != rspLen){
        fprintf(stderr, "error occures while write 'OK' to model link.\n");
        return false;
    }
    if(shutdown(clifd, SHUT_WR) == -1){
        fprintf(stderr, "fail to shutdown writing endpoint of model link, error: %s.\n", strerror(errno));
    }
    return true;
}
*/

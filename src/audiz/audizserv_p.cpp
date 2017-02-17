/*************************************************************************
	> File Name: audizserv_p.cpp
	> Author: 
	> Mail: 
	> Created Time: Thu 16 Feb 2017 07:28:42 AM EST
 ************************************************************************/

#include <poll.h>
#include <pthread.h>

#include <cstdlib>
#include<iostream>
using namespace std;

#include "BufferProject_fork.cpp"
#include "../audizstruct.h"
#include "../apueclient.h"

#ifndef LOG4CPLUS
#define LOG4CPLUS_ERROR(x, ...) cerr<< __VA_ARGS__
#define LOG4CPLUS_DEBUG(x, ...) cerr<< __VA_ARGS__

#endif
int g_DataFd;
int g_ModlFd;
pthread_cond_t g_DtMdFdCond;
pthread_mutex_t g_DtMdFdLock;

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

static pthread_mutex_t g_ModlFdLock = PTHREAD_MUTEX_INITIALIZER;
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
 *****************************************************/
static bool procDataReceived(int dataFd)
{
    char redunarr[DATAREDUNSIZE];
    vector<AZ_PckVec> pcks;
    Audiz_WaveUnit unit;
    Audiz_Wave_OnWire::getEmptyPckVec(pcks, unit, redunarr);
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
        if(retr == 0) break;
        if(!Audiz_Wave_OnWire::isValid(pcks)){
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
        recvProjSegment(unit.m_iPCBID, const_cast<char*>(g_tmpAudioData.c_str()), g_tmpAudioData.size());
        LOG4CPLUS_DEBUG(g_logger, "PID="<< unit.m_iPCBID<< " new data arrived len="<< unit.m_iDataLen);
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
        Audiz_PResult_Head reshd;
        reshd.type = AZOP_QUERY_SAMPLE + 1;
        reshd.ack = 0;
        vector<AZ_PckVec> pcks;
        Audiz_PResult_Head_OnWire::serialize(reshd, pcks);
        int err;
        writen_s(mdlFd, &pcks[0], pcks.size(), &err, 0);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "procModlReceived writing Audiz_PResult_Head marking AZOP_QUERY_SAMPLE failed.");
            return false;
        }
    }
    else if(reqhd.type == AZOP_ADD_SAMPLE){
        Audiz_PResult_Head reshd;
        reshd.type = AZOP_ADD_SAMPLE + 1;
        reshd.ack = 0;
        vector<AZ_PckVec> pcks;
        Audiz_PResult_Head_OnWire::serialize(reshd, pcks);
        int err;
        writen_s(mdlFd, &pcks[0], pcks.size(), &err, 0);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "procModlReceived writing Audiz_PResult_Head marking AZOP_ADD_SAMPLE failed.");
            return false;
        }
    }
    else if(reqhd.type == AZOP_QUERY_PROJ){
        Audiz_PResult_Head reshd;
        reshd.type = AZOP_QUERY_PROJ + 1;
        reshd.ack = queryProjNum();
        vector<AZ_PckVec> pcks;
        Audiz_PResult_Head_OnWire::serialize(reshd, pcks);
        writen_s(mdlFd, &pcks[0], pcks.size(), &err, 0);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "procModlReceived writing Audiz_PResult_Head marking AZOP_QUERY_PROJ failed.");
            return false;
        }
    }
    else {
        LOG4CPLUS_ERROR(g_logger, "procModlReceived unrecognized Audiz_PRequest_Head. type: "<< reqhd.type);
        return false;
    }
}

//deprecated
/**
static bool procResFeedReceived()
{
    bool bRet = false;
    unsigned count;
    unsigned long long tmpId;
    int err;
    PckVec arr[1];
    arr[0].base = (char*)&count;
    arr[0].len = sizeof(unsigned);
    pthread_mutex_lock(&g_ResFdLock);
    readn(g_ResFd, arr, 1, &err);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "the ress link has been broken. file: "<< __FILE__<< ", line: "<< __LINE__);
        goto exit_func;
    }
    if(count == 0) {
        bRet = true;
        goto exit_func;
    }
    while(count -- > 0){
        arr[0].base = (char*)&tmpId;
        arr[0].len = sizeof(unsigned long long);
        readn(g_ResFd, arr, 1, &err);
        if(err < 0){
            LOG4CPLUS_ERROR(g_logger, "the ress link has been broken. file: "<< __FILE__<< ", line: "<< __LINE__);
            goto exit_func;
        }
        LOG4CPLUS_DEBUG(g_logger, "PID="<< tmpId<< " have confirmed the reception of report struct.");
    }
    bRet = true;
    exit_func:
    pthread_mutex_unlock(&g_ResFdLock);
    return true;
}
*/

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

/**
 * return true if a specified link is got, return false otherwise.
 *
 */
bool implProtoAtAcceptLink(int servfd, int &clifd, char &which)
{
    uid_t uid;
    unsigned BufLen = 100;
    char tmpBuf[BufLen];
    int tmpfd = serv_accept(servfd, &uid);
    if(tmpfd < 0){
        fprintf(stderr, "serv_accept error. error: %s.\n", strerror(errno));
        return false;
    }
    int nread = read(tmpfd, tmpBuf, BufLen);
    if(nread < 0){
        fprintf(stderr, "error occour at new client link while reading, error: %s.\n", strerror(errno));
        close(tmpfd);
        return false;
    }
    else if(nread == 0){
        fprintf(stderr, "unexpected eof while read new client link.\n");
        close(tmpfd);
        return false;
    }
    tmpBuf[nread] = '\0';
    if(strcmp(tmpBuf, g_ChnlNames[0]) == 0){
        //establish data link
        if(establishDataLink(tmpfd)){
            close(tmpfd);
            return false;
        }
        else{
            clifd = tmpfd;
            which = 0;
            return true;
        }
    }
    else if(strcmp(tmpBuf, g_ChnlNames[1]) == 0){
        //establish model link
        if(establishModelLink(tmpfd)){
            close(tmpfd);
            return false;
        }
        else{
            clifd = tmpfd;
            which = 1;
            return true;
        }
    }
    else if(strcmp(tmpBuf, g_ChnlNames[2]) == 0){
        //establish res link
        if(establishResultLink(tmpfd)){
            close(tmpfd);
            return false;
        }
        else{
            clifd = tmpfd;
            which = 2;
            return true;
        }
    }
    else{
        fprintf(stderr, "unrecognized connection request, ignore it.");
        close(tmpfd);
        return false;
    }
}

/**
 * accept new client link from serv link, place the link in right place.
 *
 */
void *servRec_loop(void *param)
{
    static const char *unPath = "recogServer";
    int servfd = serv_listen(unPath);
    if(servfd < 0){
        fprintf(stderr, "serv_listen error. error: %s\n", strerror(errno));
        exit(1);
    }
    struct pollfd fdarr[1];
    fdarr[0].fd = servfd;
    fdarr[0].events = POLLIN;
    while(true){
        int retpoll = poll(fdarr, 1, -1);
        if(retpoll <=0){
            if(retpoll == 0){
                continue;
            }
            else{
                fprintf(stderr, "encounter errors while polling servfd. error: %s\n", strerror(errno));
                break;
            }
        }
        short errbits = fdarr[0].revents & (POLLERR | POLLHUP | POLLNVAL);
        if(errbits){
            fprintf(stderr, "the serv link is broken while polling. errbits: %d\n", errbits);
            break;
        }
        if(fdarr[0].revents & POLLIN){
            int clifd;
            char whichLink;
            if(implProtoAtAcceptLink(servfd, clifd, whichLink)){
                switch(whichLink){
                    case 0:
                        pthread_mutex_lock(&g_DtMdFdLock);
                        if(g_DataFd != 0){
                            LOG4CPLUS_WARN(g_logger, "a new data link arrives, and overrides the old one.");
                            close(g_DataFd);
                        }
                        g_DataFd = clifd;
                        pthread_mutex_unlock(&g_DtMdFdLock);
                        pthread_cond_broadcast(&g_DtMdFdCond);
                        break;
                    case 1:
                        pthread_mutex_lock(&g_DtMdFdLock);
                        if(g_ModlFd != 0){
                            LOG4CPLUS_WARN(g_logger, "a new modl link arrives, and overrides the old one.");
                            close(g_ModlFd);
                        }
                        g_ModlFd = clifd;
                        pthread_mutex_unlock(&g_DtMdFdLock);
                        pthread_cond_broadcast(&g_DtMdFdCond);
                        break;
                    case 2:
                        pthread_mutex_lock(&g_ResFdLock);
                        if(g_ResFd != 0){
                            LOG4CPLUS_WARN(g_logger, "a new ress link arrives, and overrides the old one.");
                            close(g_ResFd);
                        }
                        g_ResFd = clifd;
                        pthread_mutex_unlock(&g_ResFdLock);
                        break;
                    default:
                        ;
                }
            }
        }
        
    }
    return NULL;
}

/**
 * the way to check whether fd is broken needs to refine.
 * 
 */
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
/**
 * fetch online data from link datafd and modlfd, hand it to the recogition system.
 *
 */
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
                if(fdarr[0].revents & POLLIN ){
                    if(!procDataReceived(fdarr[0].fd)) break;
                }

                short errbits = fdarr[0].revents & (POLLERR | POLLHUP | POLLNVAL);
                if(errbits){
                    LOG4CPLUS_ERROR_FMT(g_logger, "the data link is broken while polling, fd: %d; errbits: %d.", fdarr[0].fd, errbits);
                    checkFdAndCloseBroken(g_DataFd, g_DtMdFdLock);
                }
            }
            if(fdarr[1].revents != 0){
                if(fdarr[1].revents & POLLIN ) {
                    if(!procModlReceived(fdarr[1].fd)) break;
                }
                short errbits1 = fdarr[1].revents & (POLLERR | POLLHUP | POLLNVAL);
                if(errbits1){
                    LOG4CPLUS_ERROR_FMT(g_logger, "the modl link is broken while polling, fd:%d; errbits: %d.", fdarr[0].fd, errbits1);
                    checkFdAndCloseBroken(g_ModlFd, g_DtMdFdLock);
                }
            }
        }
    }
}

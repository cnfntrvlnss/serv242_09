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

int g_DataFd;
int g_ModlFd;
pthread_cond_t g_DtMdFdCond;
pthread_mutex_t g_DtMdFdLock;

static size_t readn(int fd, vector<AZ_PckVec> &pcks, int *err, int istry)
{
    return readn(fd, reinterpret_cast<PckVec*>(&pcks[0]), pcks.size(), err, istry);
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
        if(retr == 0) break;
        if(memcmp(datavec[1].base, dataRedunArr, DATAREDUNSIZE) != 0){
            LOG4CPLUS_ERROR(g_logger, "redundancy check fails, and close the data link.");
            return false;
        }
        static PckVec segs[1];
        segs[0].len = len;
        segs[0].base = getGlobalSegment(len);
        readn(dataFd, segs, 1, &err, 0);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "the data link has broken, and close it. __LINE__: "<< __LINE__);
            return false;
        }
        if(segs[0].len > 0){
            recvProjSegment(id, segs[0].base, segs[0].len, !g_bDiscardable);
        }
        else if(id != 0){
            notifyProjFinish(id);
        }
        LOG4CPLUS_DEBUG(g_logger, "PID="<< id<< " new data arrived len="<< len);
    }
    return true;
}

static bool procModlReceived(int mdlFd)
{
    static PckVec mdlvec[2];
    static unsigned len;
    static const unsigned hdlen = SPKMDL_HDLEN;
    static char tmpbuf[hdlen];
    mdlvec[0].base = tmpbuf;
    mdlvec[0].len = hdlen;
    mdlvec[1].base = (char*)&len;
    mdlvec[1].len = sizeof(unsigned);
    int err;
    while(true){
        size_t retr = readn(mdlFd, mdlvec, 2, &err, 1);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "the spk link has broken, and close it. __LINE__: "<< __LINE__);
            return false;
        }
        //finish all models on the wire.
        if(retr == 0) return true;
        unsigned speakid, srvtype, harmlevel;
        if(!parseSpkMdlName(tmpbuf, speakid, srvtype, harmlevel)){
            LOG4CPLUS_ERROR(g_logger, "fails to parse spkmdl from mdl link, and close it. __LINE__: "<< __LINE__);
            return false;
        }
        PckVec segs[1];
        segs[0].base = getGlobalSegment(len);
        segs[0].len = len;
        readn(mdlFd, segs, 1, &err, 0);
        if(err == -1){
            LOG4CPLUS_ERROR(g_logger, "the spk link has broken, and close it. __LINE__: "<< __LINE__);
            return false;
        }
        if(g_bUseSpk){
            if(segs[0].len == 0){
                RemoveSpeaker(speakid);   
            }
            else{
                AddSpeaker(speakid, segs[0].base, segs[0].len, srvtype, harmlevel);
            }
        }
        else{
            LOG4CPLUS_WARN(g_logger, "the spk model is overlooked as speaker module being disabled, speakerid: "<< speakid<< "; modellen: "<< segs[0].len);
        }

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

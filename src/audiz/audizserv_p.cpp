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

#include "samplelib.h"
#include "ProjectBuffer.h"
#include "../audizstruct.h"
#include "../apueclient.h"
#include "globalfunc.h"

using namespace std;
using namespace audiz;

#define OUTPUT_ERRNO " error"<< strerror(errno)

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
    initSampleLib();
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

void reportAudiz_Result(const Audiz_Result& res)
{
    Audiz_PResult_Head reshd;
    reshd.type = AZOP_REC_RESULT;
    reshd.ack = 1;
    vector<AZ_PckVec> pcks;
    reshd.pack_w(pcks);
    pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(const_cast<Audiz_Result*>(&res)), sizeof(Audiz_Result)));
    
    int err;
    writen_s(g_ModlFd, &pcks[0], pcks.size(), &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "reportAudiz_Result error occurs while write result to modl link.");
    }
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
        if(err < 0){
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
        if(err < 0){
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

static string g_TmpSampledata;
bool fetchSampleFromFd(int fd, vector<AZ_PckVec> &pcks, SpkMdlSt& mdl)
{
    int err;
    pcks.clear();
    mdl.pack_r(pcks);
    readn(fd, pcks, &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "fetchSampleFromFd failed to read sample head. error: "<< strerror(errno));
        return false;
    }
    g_TmpSampledata.resize(mdl.len);
    mdl.buf = const_cast<char*>(g_TmpSampledata.c_str());
    pcks.resize(1);
    pcks[0].base = mdl.buf;
    pcks[0].len = mdl.len;
    readn(fd, pcks, &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "fetchSampleFromFd failed to read sample data. error: "<< strerror(errno));
        return false;
    }
    return true;
}

/**
 *
 *
 */
static bool procModlReceived(int mdlFd)
{
    Audiz_PRequest_Head reqhd;
    vector<AZ_PckVec> pcks;
    reqhd.pack_r(pcks);
    int err;
    size_t retr = readn(mdlFd, pcks, &err, 0);
    if(err < 0){
        LOG4CPLUS_ERROR(g_logger, "procModlReceived error occurs while reading Audiz_PRequest_Head."<< OUTPUT_ERRNO);
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
        pcks.clear();
        reshd.pack_w(pcks);
        int err;
        writen_s(mdlFd, &pcks[0], pcks.size(), &err, 0);
        if(err < 0){
            LOG4CPLUS_ERROR(g_logger, "procModlReceived while processing AZOP_QUERY_SAMPLE failed to write Audiz_PResult_Head."<< OUTPUT_ERRNO);
            return false;
        }
        else{
            LOG4CPLUS_INFO(g_logger, "procModlReceived processing AZOP_QUERY_SAMPLE success.");
        }
    }
    else if(reqhd.type == AZOP_ADDRM_SAMPLE){
        SpkMdlSt mdl;
        if(!fetchSampleFromFd(mdlFd, pcks, mdl)){
            return false;
        }
        if(mdl.len == 0){
            rmSample(mdl.head);
        }
        else{
            addSample(mdl.head, mdl.buf, mdl.len);
        }
        Audiz_PResult_Head reshd;
        reshd.type = AZOP_ADDRM_SAMPLE + 1;
        reshd.ack = 0;
        pcks.clear();
        reshd.pack_w(pcks);
        int err;
        writen_s(mdlFd, &pcks[0], pcks.size(), &err, 0);
        if(err < 0){
            LOG4CPLUS_ERROR(g_logger, "procModlReceived while processing AZOP_ADDRM_SAMPLE failed to write Audiz_PResult_Head."<< OUTPUT_ERRNO);
            return false;
        }
        else{
            LOG4CPLUS_INFO(g_logger, "procModlReceived processing AZOP_ADDRM_SAMPLE success.");
        }
    }
    else if(reqhd.type == AZOP_ADD_SAMPLE){
        if(reqhd.addLen == 0){
            finishStore();
            Audiz_PResult_Head reshd;
            reshd.type = AZOP_ADD_SAMPLE + 1;
            reshd.ack = getSampleNum();
            pcks.clear();
            reshd.pack_w(pcks);
            writen_s(mdlFd, &pcks[0], pcks.size(), &err, 0);
            if(err < 0){
                LOG4CPLUS_ERROR(g_logger, "procModlReceived failed to write response of msg AZOP_ADD_SAMPLE."<< OUTPUT_ERRNO);   
                return false;
            }
        }
        else{
            for(int idx=0; idx< reqhd.addLen; idx++){
                SpkMdlSt mdl;
                if(!fetchSampleFromFd(mdlFd, pcks, mdl)){
                    return false;
                }
                if(mdl.len > 0){
                    storeSample(mdl.head, mdl.buf, mdl.len);
                }
            }
            LOG4CPLUS_DEBUG(g_logger, "procModlReceived have get "<< reqhd.addLen<< " samples in msg AZOP_ADDRM_SAMPLE.");
        }
    }
    else if(reqhd.type == AZOP_QUERY_PROJ){
        Audiz_PResult_Head reshd;
        reshd.type = AZOP_QUERY_PROJ + 1;
        reshd.ack = queryProjNum();
        pcks.clear();
        reshd.pack_w(pcks);
        writen_s(mdlFd, &pcks[0], pcks.size(), &err, 0);
        if(err < 0){
            LOG4CPLUS_ERROR(g_logger, "procModlReceived while processing AZOP_QUERY_PROJ failed to write Audiz_PResult_Head."<< OUTPUT_ERRNO);
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
            strcpy(res.ack, AZ_LINKBUILDOK);
            pcks.clear();
            res.pack_w(pcks);
            writen(tmpfd, pcks, &errw, 0);
            if(errw <0) break;
            LOG4CPLUS_DEBUG(g_logger, "procImplAcceptLink a new modl link. fd="<< tmpfd<< "; clientId="<< res.req.sid);
            if(setModlFd(tmpfd, res.req.sid)){
                bsucc = true;
            }
        }
        else if(strcmp(res.req.head, AZ_DATALINKNAME) == 0){
            strcpy(res.ack, AZ_LINKBUILDOK);
            pcks.clear();
            res.pack_w(pcks);
            writen(tmpfd, pcks, &errw, 0);
            //data link should be nonblocking.
            set_fl(tmpfd, O_NONBLOCK);
            if(errw <0) break;
            LOG4CPLUS_DEBUG(g_logger, "procImplAcceptLink a new data link. fd="<< tmpfd<< "; clientId="<< res.req.sid);
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

static int g_ServFd;
/**
 * accept new client link from serv link, place the link in right place.
 *
 */
void *servtask_loop(void *param)
{
    while(true){
        int modlidx = 0;
        int dataidx = 0;
        struct pollfd fdarr[3];
        fdarr[0].fd = g_ServFd;
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
            LOG4CPLUS_ERROR(g_logger, "servtask_loop polling error. error: "<< strerror(errno));
            break;
        }
        short errbits = fdarr[0].revents & (POLLERR | POLLHUP | POLLNVAL);
        if(errbits){
            fprintf(stderr, "the serv link is broken while polling. errbits: %d\n", errbits);
            break;
        }
        if(fdarr[0].revents & POLLIN){
            procImplAcceptLink(g_ServFd);
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
    exit(1);
    return NULL;
}


#define handle_error(x, msg) do{perror(msg); exit(EXIT_FAILURE);} while(0)
static pthread_t servThrdId;
void startServTask()
{
    const char *unPath = AZ_DATACENTER;
    g_ServFd = serv_listen(unPath);
    if(g_ServFd < 0){
        LOG4CPLUS_ERROR(g_logger, "startServTask serv_listen error. path: "<< unPath<< "; error: "<< strerror(errno));
        exit(1);
    }
    LOG4CPLUS_INFO(g_logger, "startServTask serv_listen at path "<< unPath);
    int retp = pthread_create(&servThrdId, NULL, servtask_loop, NULL);
    if(retp !=0 ){
        handle_error(retp ,"pthread_create");
    }
}
void endServTask()
{
    pthread_cancel(servThrdId);
    pthread_join(servThrdId, NULL);
}

#if 0
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

#endif

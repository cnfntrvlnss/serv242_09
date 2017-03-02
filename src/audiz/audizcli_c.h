/*************************************************************************
	> File Name: /home/zheng/Desktop/serv242_09/src/audiz/audizcli_c.h
	> Author: 
	> Mail: 
	> Created Time: Wed 01 Mar 2017 05:22:22 AM EST
 ************************************************************************/

#ifndef _AUDIZ_AUDIZCLI_C_H
#define _AUDIZ_AUDIZCLI_C_H

#include <vector>
#include "../audizrecst.h"
namespace audiz{

bool initRecSession(const char*workDir, const char* servAddr, const char* shmPath);
bool rlseRecSession();

bool fetchProject(uint64_t &pid, std::vector<AZ_PckVec>& data);
bool notifyFinished(uint64_t pid);
bool reportProjectResult(const Audiz_Result &res);

//TODO add consuming sample
typedef bool (*GetAllSmpsFunc)();
typedef bool (*RmAddSmpFunc)(const char *head, char *data, unsigned len);
void registerTypeConsumer(unsigned short type, GetAllSmpsFunc addr1, RmAddSmpFunc addr2);
}

#endif

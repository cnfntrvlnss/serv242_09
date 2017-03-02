/*************************************************************************
    > File Name: utilites.cpp
    > Author: ma6174
    > Mail: ma6174@163.com 
    > Created Time: Sun 01 Mar 2015 05:37:15 PM PST
 ************************************************************************/

#include <stdarg.h>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include "utilites.h"
#include <algorithm>
#include <string>
#include <map>
#include <sstream>
#include <fstream>

using namespace std;
#define MAX_PATH 512
#define MAX_LINE 1024

//////////////////////////////////////////////////////////////////////////
// LockHelper
//////////////////////////////////////////////////////////////////////////
LockHelper::LockHelper()
{
#ifdef WIN32
    InitializeCriticalSection(&_crit);
#else
    //_crit = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
    //pthread_mutexattr_t attr;
    //pthread_mutexattr_init(&attr);
    //pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    //pthread_mutex_init(&_crit, &attr);
    //pthread_mutexattr_destroy(&attr);
    pthread_mutex_init(&_crit, NULL);
#endif
}
LockHelper::~LockHelper()
{
#ifdef WIN32
    DeleteCriticalSection(&_crit);
#else
    pthread_mutex_destroy(&_crit);
#endif
}

void LockHelper::lock()
{
#ifdef WIN32
    EnterCriticalSection(&_crit);
#else
    pthread_mutex_lock(&_crit);
#endif
}
void LockHelper::unLock()
{
#ifdef WIN32
    LeaveCriticalSection(&_crit);
#else
    pthread_mutex_unlock(&_crit);
#endif
}

/**
 * 分割字符串，用通用的默认形式，即，空格类字符作为分割符。
 */
vector<string> split(const string& s)
{
	vector<string> results;
	int st = -1;
	for(int i=0; i<s.size(); i++) {
		if(isspace(s[i]) && st != -1){
			results.push_back(s.substr(st, i-st));
			st = -1;
		}
		else if(!isspace(s[i]) && st == -1){
			st = i;
		}
	}
	if(st != -1) {
		results.push_back(s.substr(st, s.size()-st));
	}
	return results;
}
/**
 *根据delim子串分割字符串s, 若keep_empty为true, 返回的结果中就保留空字符串；反之，亦然。
 */
vector<string> split(const string& s, const string& delim,
		const bool keep_empty )
{
	vector<string> result;
	if (delim.empty())
	{
		result.push_back(s);
		return result;
	}
	string::const_iterator substart = s.begin(), subend;
	while (true)
	{
		subend = search(substart, s.end(), delim.begin(), delim.end());
		string temp(substart, subend);
		if (keep_empty || !temp.empty())
		{
			result.push_back(temp);
		}
		if (subend == s.end())
		{
			break;
		}
		substart = subend + delim.size();
	}
	return result;
}

vector<string> loadFileList(const char *listfile)
{
    char strline[512];
    vector<string> ret;
    FILE *fp = fopen(listfile, "r");
    if(fp == NULL){
        return ret;
    }
    while(fgets(strline, 512, fp)){
        int lastidx = strlen(strline) - 1;
        while (lastidx >=0 && strchr("\t\r \n",strline[lastidx]) != NULL){
            strline[lastidx -- ] = '\0';
        }
        if(lastidx > 0){
            ret.push_back(strline);
        }
    }
    fclose(fp);
    return ret;
}

bool make_directorys(const char *mypath)
{
	if(access(mypath, F_OK) == 0) return true;
	char firstPath[MAX_PATH];
	char fileName[MAX_PATH];
	strncpy(firstPath, mypath, MAX_PATH);
	unsigned pathLen = strlen(firstPath);
	if(firstPath[pathLen -1] == '/'){
		firstPath[pathLen - 1] = '\0';
	}
	char *pSl = strrchr(firstPath, '/');
	strcpy(fileName, pSl + 1);
	*(pSl + 1) = '\0';
	make_directorys(firstPath);
	if(mkdir(mypath, 0775) == 0) {
		return true;
	}
	return false;
}

unsigned  procFilesInDir(const char* szDir, FuncProcessFile addr)
{
    DIR *dp;
    struct dirent *dirp;
    struct stat statbuf;
    int cnt = 0;

    if((dp = opendir(szDir)) == NULL) return 0;
    while((dirp = readdir(dp)) != NULL){
        if(strcmp(dirp->d_name, ".") == 0 ||
                strcmp(dirp->d_name, "..") == 0) continue;
        string tmpPath = concatePath(szDir, dirp->d_name);
        if(stat(tmpPath.c_str(), &statbuf) < 0) continue;
        if(S_ISREG(statbuf.st_mode)) {
            if(addr(szDir, dirp->d_name)) cnt++;
        }
    }
    closedir(dp);
    return cnt;
}

bool copyFile(const char* src, const char* des)
{
    std::ifstream in(src, ios::binary);
    std::ofstream out(des, ios::binary);
    if(!in || !out){
        return false;
    }
    out<< in.rdbuf();
    return true;
}

bool copyFile_S(const char* src, const char* des)
{
    string tmpdes = (string)des + ".tmp";
    if(!copyFile(src, tmpdes.c_str())){
        return false;
    } 
    if(remove(des) != 0){
        return false;
    }
    if(rename(tmpdes.c_str(), des) != 0){
        return false;
    }
    return true;
}
bool moveFile(const char* src, const char *des)
{
    string tmpdes = (string)des + ".tmp";
    if(!copyFile(src, tmpdes.c_str())){
        return false;
    } 
    if(remove(src) != 0){
        return false;
    }
    if(rename(tmpdes.c_str(), des) != 0){
        return false;
    }
    return true;

}

union MyConfigItemValue{
	bool *bvar;
	int *ivar;
	char *svar;
	float *fvar;
};
struct MyConfigItem{
	const char *name;
	char type;
	MyConfigItemValue pvalue;
};
/*****************************************************
 *可变参数部分为 const char*, type[*],...;即，两个为一组，直到最后的NULL参数为止。
 * 含义是: 参数[1]的开头的字母为B/S/I/F，对应的后面的一个参数的type为bool/char* /int/float, 若为char star,*是多余的。
 *每组里面的第三个参数存储返回值，若配置文件中存在第一个参数指定的配置项，就存储解析出的值
 *****************************************************/
int parse_params_from_file(const char *fileName, ...)
{
	FILE *fp = fopen(fileName, "r");
    if(fp== NULL){
        return 0;
    }
	MyConfigItem *confarr = NULL;
	int count = 100;
	confarr = (MyConfigItem*)malloc(sizeof(MyConfigItem) * count);
	va_list valist;
	va_start(valist, fileName);
	int idx = 0;
	bool isfinish = false;
	for(; idx<count; idx++)
	{
		confarr[idx].name = va_arg(valist, char*);
		if(confarr[idx].name == NULL) break;
		confarr[idx].type = confarr[idx].name[0];
		confarr[idx].name ++;
		switch (confarr[idx].type){
			case 'B': confarr[idx].pvalue.bvar = va_arg(valist, bool*);   break;
			case 'S': confarr[idx].pvalue.svar = va_arg(valist, char*);  break;
			case 'I': confarr[idx].pvalue.ivar = va_arg(valist, int*);  break;
			case 'F': confarr[idx].pvalue.fvar = va_arg(valist, float*);  break;
			default: isfinish = true; break;
		}
		if(isfinish) break;
	}
	va_end(valist);
	if(idx == 0) {
		free(confarr);
		return 0;
	}
	
	char szsect[MAX_PATH];
	char szname[MAX_PATH];
	char szvalue[MAX_PATH];
	char szline[1024];
	szsect[0] = 0;
	while(!feof(fp))
	{
        szline[0] = '\0';
        fgets(szline, 1024, fp);
        if('\0' == szline[0] || '\n' == szline[0] || '#' == szline[0])
        {
            continue;
        }
		int ispair = false;
        for(int ii = 0; ii < strlen(szline); ii++)
        {
            if('=' == szline[ii])
            {
				ispair = true;
                szline[ii] = ' ';
            }
			else if('#' == szline[ii]){
                szline[ii] = 0;
				break;
			}
        }
		if(ispair == false){
			sscanf(szline, "[%s]", szsect);
		}
		else {
			int retsc = sscanf(szline, "%s %s", szname, szvalue);
			if(retsc < 2){ continue; }
			char dotedname[MAX_PATH];
			sprintf(dotedname, "%s.%s", szsect, szname);
			for(int i=0; i<idx; i++){
				if(!strcmp(confarr[i].name, szname) || !strcmp(confarr[i].name, dotedname)){
					switch (confarr[i].type){
						case 'B':
							if(!strcmp(szvalue, "0")){
								*(confarr[i].pvalue.bvar) = false; break;
							}
							if(!strcmp(szvalue, "1")){
								*(confarr[i].pvalue.bvar) = true; break;
							}
							for(int ii=0; ii< strlen(szvalue); ii++){
								if(szvalue[ii] < 'a') szvalue[ii] += 32;
							}
							if(!strcmp(szvalue, "true")){
								*(confarr[i].pvalue.bvar) = true; break;
							}
							if(!strcmp(szvalue, "false")){
								*(confarr[i].pvalue.bvar) = false; break;
							}
							break;
						case 'S':
							strcpy(confarr[i].pvalue.svar, szvalue);break;
						case 'I':
							*(confarr[i].pvalue.ivar) = atoi(szvalue); break;
						case 'F':
							*(confarr[i].pvalue.fvar) = atof(szvalue); break;
						default: assert(false);
					}
					break;
				}
			}
		}

	}
    free(confarr);
	return idx;
}

void Config_getValue(ConfigRoom *cfg, const char *group, const char *key, std::string& val)
{
    std::string value;
    cfg->accessValue(group, key, value);
    if(value != ""){
        val = value;
    }
}
static char* getValidString(char *tmpStr)
{
    if(tmpStr[0] == '\0') return NULL;
    unsigned tmplen = strlen(tmpStr);
    char* lastPtr = tmpStr + tmplen - 1;
    while(lastPtr != tmpStr && isspace(*lastPtr)){
        lastPtr --;
    }
    if(lastPtr == tmpStr){
        if(isspace(*lastPtr)) return NULL;
        else {
            *(lastPtr + 1) = '\0';
            return tmpStr;
        }
    }
    *(lastPtr + 1) = '\0';
    
    while(isspace(*tmpStr)) tmpStr ++;
    return tmpStr;
}

bool ConfigRoom::loadFromFile(const char* filePath)
{
    AutoLock lock(mylock);
    if(configFile.size() == 0 && filePath == NULL){
        fprintf(stderr, "ERROR no config file specified.\n");
        return false;
    }
    if(configFile.size() != 0 && configFile != filePath){
        fprintf(stderr, "WARN the current configure file is not used, for being not the same file with initial file. initial: %s, current: %s.\n", configFile.c_str(), filePath);
    }
    if(configFile.size() == 0){
        configFile = filePath;
    }
    for(map<string, StringPair>::iterator it = allConfigs.begin(); it != allConfigs.end(); it ++){
        (*it).second.current = "";
    }
    struct stat statbuf;
    if(stat(configFile.c_str(), &statbuf) < 0){
        lastLoadFile = 0;
        fprintf(stderr, "WARN the configure file does not exist. file: %s.\n", configFile.c_str());
        return false;
    }
    lastLoadFile = statbuf.st_mtime;
    FILE *fp = fopen(configFile.c_str(), "r");
    if(fp == NULL){
        fprintf(stderr, "WARN the configure file is not opened. file: %s.\n", configFile.c_str());
        lastLoadFile = 0;
        return false;
    }
    char groupName[MAX_PATH];
    groupName[0] = '\0';
    char tmpLine[MAX_LINE];
    while(fgets(tmpLine, MAX_LINE, fp) != NULL){
        char *validStr = getValidString(tmpLine);
        if(validStr == NULL) continue;
        char *comSt = strchr(validStr, '#');
        if(comSt != NULL) *comSt = '\0';
        if(*validStr == '['){
            char *closePairPtr = strrchr(validStr, ']');
            if(closePairPtr == NULL){
                fprintf(stderr, "ERROR the line in configure file is illegal. line: %s.", validStr);
                continue;
            }
            *closePairPtr = '\0';
            validStr = getValidString(validStr + 1);
            if(validStr == NULL) continue;
            strncpy(groupName, validStr, MAX_PATH);
            continue;
        }
        char * sepPtr = strchr(validStr, '=');
        if(sepPtr != NULL){
            *sepPtr = '\0';
            allConfigs[(string)groupName + "." + getValidString(validStr)].current = getValidString(sepPtr + 1);
            continue;
        }
        fprintf(stderr, "ERROR unrecognized line: %s.\n", tmpLine);
    }
    fclose(fp);
    return true;
}

bool ConfigRoom::checkAndLoad()
{
    bool ret = false;
    mylock.lock();
    time_t lasttime = lastLoadFile;
    mylock.unLock();
    struct stat statbuf;
    if(stat(configFile.c_str(), &statbuf) < 0){
        statbuf.st_mtime = 0;
        if(lasttime !=0){
            this->loadFromFile(configFile.c_str());
            ret = true;
        }
    }
    if(lasttime < statbuf.st_mtime){
        this->loadFromFile(configFile.c_str());
        ret = true;
    }
    return ret;
}
bool ConfigRoom::isUpdated(const char* group, const char* key)
{
    AutoLock lock(mylock);
    string innerKey = (string)group + "." + key;
    if(allConfigs.find(innerKey) == allConfigs.end()){
        return false;
    }
    if(allConfigs[innerKey].used == allConfigs[innerKey].current){
        return false;
    }
    return true;
}

void ConfigRoom::accessValue(const char* group, const char* key, string& value)
{
    AutoLock lock(mylock);
    string innerKey = (string)group + "." + key;
    if(allConfigs.find(innerKey) == allConfigs.end()){
        value = "";
    }
    value = allConfigs[innerKey].current;
    allConfigs[innerKey].used = value;
}

void ConfigRoom::accessValue(const char* group, const char* key, FuncUseConfig funcAddr)
{
    string value;
    accessValue(group, key, value);
    funcAddr(group, key, value.c_str());
}

//#define TEST_MAIN
#ifdef TEST_MAIN
#include <list>
template<typename T>
void print_config(const char* group, const char *key, const T &val)
{
    cout << "new config entry: "<< group<< "." << key<< " = "<< val << ".\n";
}

void print_config(const char* group, const char *key, const char *value)
{
    printf("new config entry: %s.%s = %s.\n", group, key, value);
}
void testConfigRoom(const char *cfgFile)
{
    ConfigRoom cfg(cfgFile); 
    while(true){
        //cfg.isUpdated("", "");
        cfg.checkAndLoad();
        map<string, ConfigRoom::StringPair>::iterator it = cfg.allConfigs.begin();
        while(it != cfg.allConfigs.end()){
            string innerKey = it->first;
            size_t sePos = innerKey.find_first_of(".");
            string group = innerKey.substr(0, sePos);
            string key = innerKey.substr(sePos + 1);
            if(cfg.isUpdated(group.c_str(), key.c_str())){
                cfg.accessValue(group.c_str(), key.c_str(), print_config);
            }
            it ++;
        }
        if(cfg.isUpdated("lid", "ifUseLID")){
            cfg.accessValue("lid", "ifUseLID", print_config);
        }
        sleep(3);
    }
}

void testDataBlock()
{
    sleep(5);
    vector<DataBlock> vec;
    for(unsigned idx=0; idx < 1024; idx++){
        DataBlock blk(1024 * 1024);
        //for(unsigned jdx=0; jdx < blk.getCap(); jdx++){
        //    memset(&blk.getPtr()[blk.len++], 256, 1);
        //}
        blk.len = 1024;
        vec.push_back(blk);
    }
    list<DataBlock> li(vec.begin(), vec.end());
    sleep(3);
    li.clear();
    vector<DataBlock>::iterator it= vec.end();
    do{
        it--;
        assert(it->len == 1024);
        vec.erase(it);
    }while(vec.end() == vec.begin());
    sleep(3);
}

int main(int argc, char** argv)
{
    //testConfigRoom(argv[1])
    testDataBlock();
    sleep(3);
    return 0;
}

#endif

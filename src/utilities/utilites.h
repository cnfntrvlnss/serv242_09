/*************************************************************************
    > File Name: utilites.h
    > Author: shurui
    > Mail: 
    > Created Time: Tue 03 Feb 2015 11:33:29 AM CST
 ************************************************************************/
#ifndef UTILITES__H
#define UTILITES__H

#include <cassert>
#include <cstring>
#include <sstream>
#include <vector>
#include <string>
#include<iostream>
#include <map>

#include <stdio.h>

//////////////////////////////////////////////////////////////////////////
//! LockHelper
//////////////////////////////////////////////////////////////////////////
class LockHelper
{
public:
    LockHelper();
    virtual ~LockHelper();

public:
    void lock();
    void unLock();
//private:
#ifdef WIN32
    CRITICAL_SECTION _crit;
#else
    pthread_mutex_t  _crit;
#endif
};

//////////////////////////////////////////////////////////////////////////
//! AutoLock
//////////////////////////////////////////////////////////////////////////
class AutoLock
{
public:
    explicit AutoLock(LockHelper & lk):_lock(lk){_lock.lock();}
    ~AutoLock(){_lock.unLock();}
private:
    LockHelper & _lock;
};

///////////////////////////////////////////////////////////////////////
//! self managed block, only support multithreads in read term.
//////////////////////////////////////////////////////////////////////
struct SharedCharArr{
    explicit SharedCharArr(unsigned size):
        m_cap(size)
    {
        m_cnt = 1;
        pthread_mutex_init(&cntLock, NULL);
        m_buf = NULL;
        if(size > 0) m_buf = new char[size];
    }
    ~SharedCharArr(){
        assert(m_cnt == 0);
        pthread_mutex_destroy(&cntLock);
        if(m_buf) delete []m_buf;
    }
    void addOne(){
        pthread_mutex_lock(&cntLock);
        m_cnt ++;
        pthread_mutex_unlock(&cntLock);
    }
    short dcrOne(){
        pthread_mutex_lock(&cntLock);
        short cnt = --m_cnt;
        pthread_mutex_unlock(&cntLock);
        return cnt;
    }
    short getCnt(){
        pthread_mutex_lock(&cntLock);
        short cnt = m_cnt;
        pthread_mutex_unlock(&cntLock);
        return cnt;
    }
    short m_cnt;
    pthread_mutex_t cntLock;
    const unsigned m_cap;
    char *m_buf;
private:
    SharedCharArr(const SharedCharArr&);
    SharedCharArr& operator=(const SharedCharArr&);
};
class DataBlock{
    SharedCharArr *ptrBuf;
public:
    unsigned offset;
    unsigned len;
    DataBlock():
        ptrBuf(NULL), offset(0), len(0)
    { }
    explicit DataBlock(unsigned size){
        ptrBuf = new SharedCharArr(size);
        offset = 0;
        len = 0;
    }
    char *getPtr() const{
        if(ptrBuf) return ptrBuf->m_buf;
        else return NULL;
    }
    unsigned getCap()const{
        if(ptrBuf) return ptrBuf->m_cap;
        else return 0;
    }
    short getPeerNum()const{
        if(ptrBuf) return ptrBuf->getCnt();
        else return 0;
    }
    ~DataBlock(){
        if(ptrBuf){
            if(ptrBuf->dcrOne() == 0){
                delete ptrBuf;
            }
        }
    }
    DataBlock(const DataBlock& oth){
        ptrBuf = oth.ptrBuf;
        offset = oth.offset;
        len = oth.len;
        if(ptrBuf) ptrBuf->addOne();
    }
    DataBlock& operator=(const DataBlock& oth){
        if(oth.ptrBuf) oth.ptrBuf->addOne();
        if(ptrBuf){
            if(ptrBuf->dcrOne() == 0){
                delete ptrBuf;
            }
        }
        ptrBuf = oth.ptrBuf;
        offset = oth.offset;
        len = oth.len;
        return *this;
    }
    bool operator<(const DataBlock& oth) const{
        if(ptrBuf < oth.ptrBuf) return true;
        else return false;
    }
};
/////////////////////////////////////////////////
//config file
////////////////////////////////////////////////
int parse_params_from_file(const char *fileName, ...);
/**
 * configuration comes form file. also be synchronized with that in file.
 *
 */
typedef void (*FuncUseConfig)(const char *group, const char *key, const char* value);
struct ConfigRoom{
    ConfigRoom(){
        lastLoadFile = 0;
    }
    explicit ConfigRoom(const char *filePath){
        loadFromFile(filePath);
    }
    ~ConfigRoom(){
    }
private:
    ConfigRoom(const ConfigRoom&);
    ConfigRoom& operator=(const ConfigRoom&);
public:
    struct StringPair{
        std::string used;
        std::string current;
    };
    std::map<std::string, StringPair> allConfigs;
    std::string configFile;
    time_t lastLoadFile;
    LockHelper mylock;

    bool loadFromFile(const char* filePath = NULL);
    bool checkAndLoad();
    //not existing is equal to empty value.
    bool isUpdated(const char* group, const char* key);
    void accessValue(const char* group, const char* key, std::string& value);
    void accessValue(const char* group, const char* key, FuncUseConfig funcAddr);
};

template<typename T>
void Config_getValue(ConfigRoom *cfg, const char *group, const char *key, T& val)
{
    std::string value;
    cfg->accessValue(group, key, value);
    if(value != ""){
        std::istringstream iss(value);
        iss >> val;
    }
}
void Config_getValue(ConfigRoom *cfg, const char *group, const char *key, std::string& val);

///////////////////////////////////////////////
// string and path
///////////////////////////////////////////////
#define MAX_PATH 512
/** 用于debug输出数组数组变量。
 *
 */
template <typename T>
std::string arr2str(T *arr, int size){
	assert(arr != 0);
	std::stringstream ss;
	ss << "[";
	if( size > 0){
		ss << arr[0];
	}
	for(int i=1; i<size; i++){
		ss << ',' << arr[i];	
	}
	ss << "]";
	return ss.str();
}

inline std::string int2str(int i){
	char temp[100];
	sprintf(temp, "%d", i);
	return std::string(temp);
}
std::vector<std::string> split(const std::string& s);
std::vector<std::string> split(const std::string& s, const std::string& delim, const bool keep_empty = true);

inline void insertStrAt0(char *src, char *tmpStr)
{
    int m = strlen(tmpStr);
    int n = strlen(src);
    char *curPtr = src + n + 1;
    do{
        curPtr --;
        *(curPtr + m) = *curPtr;
    }while(curPtr != src);
    while(*tmpStr != '\0'){
        *(curPtr++) = *(tmpStr++);
    }
}

inline std::string concatePath(const char* path, const char* name)
{
    char wpath[MAX_PATH];
    unsigned plen = snprintf(wpath, MAX_PATH, "%s", path);
    if(wpath[plen - 1] != '/'){
        wpath[plen] = '/';
        plen += 1;
    }

    plen += snprintf(wpath + plen, MAX_PATH - plen, "%s", name);
    return wpath;
}

inline std::string getBasename(const char* path)
{
    const char* psep = strrchr(path, '/');   
    if(psep != NULL) path = psep + 1;
    psep = strrchr(path, '.');
    if(psep == NULL) psep += strlen(path);
    return std::string(path, psep);
}

std::vector<std::string> loadFileList(const char *listfile);
bool make_directorys(const char *mypath);

typedef bool (*FuncProcessFile)(const char*, const char*);
unsigned procFilesInDir(const char* szDir, FuncProcessFile addr);
bool copyFile(const char* src, const char* des);
bool copyFile_S(const char* src, const char* des);
bool moveFile(const char* src, const char *des);
#endif

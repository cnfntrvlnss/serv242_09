/*************************************************************************
    > File Name: include/interface242.h
    > Author: zhengshurui
    > Mail:  zhengshurui@thinkit.cn
    > Created Time: Mon 05 Sep 2016 11:44:39 PM PDT
 ************************************************************************/
#ifndef INTERFACE242__H
#define INTERFACE242__H

#include <stdint.h>
struct WavDataUnit{
    uint64_t m_iPCBID; //节目ID, 长度8字节
    unsigned int m_iDataLen;//数据字节长度
    char *m_pData;//数据缓冲区
    void *m_pPCB;
};

#define MAX_DATAUNIT_NUM 10
struct CDLLResult{
    unsigned int m_iTargetID; //目标ID
    unsigned int m_iAlarmType; //业务类型编码
    int m_iHarmLevel; //目标危害度
    float m_fLikely; //识别结果的置信度0~100
    float m_fTargetMatchLen; //匹配上的目标长度
    int m_iDataUnitNum; //结果中包含的片段数量1~MAX_DATAUNIT_NUM
    float m_fSegLikely[MAX_DATAUNIT_NUM]; //节目数据段(WavDataUnit) 与目标的相似度
    float m_fSegPosInPCB[MAX_DATAUNIT_NUM]; //节目数据段在节目中的时间位置
    float m_fSegPosInTarget[MAX_DATAUNIT_NUM]; //节目数据段在目标模板/配置项中的起始位置
    WavDataUnit *m_pDataUnit[MAX_DATAUNIT_NUM];
    char m_strInfo[1024]; //保留
};

/**
 *
 *  param iModuleID 软件模块初始化时，由主程序分配的软件模块ID.
 *  
 */
typedef int (*ReceiveResult)(unsigned int iModuleID, CDLLResult *pResult);

extern "C"{

/**
 *  初始化分析模块动态链接库，设置分析模块向主程序传递结果的回调函数，
 *  分析模块中的扫描线程数量及其优先级.
 *
 */
int InitDLL(int iPriority,
        int iThreadNum,
        int *pThreadCPUID,
        ReceiveResult func,
        int iDataUnitBufSize,
        char *path,
        unsigned int iModuleID);

/**
 * 向动态库添加配置项文件。如果该业务类型id的配置项已经存在，则删除之前
 * 的配置项，重新添加，即更新.
 *
 */
int AddCfg(unsigned int id, 
        const char *strName,
        const char *strConfigFile,
        int iType,
        int iHarmLevel);

/**
 * 向动态链接库添加配置项文件。如果该id的配置项已经存在，则删除之前的配
 * 置项，重新添加，即更新.
 *
 */
int AddCfgByBuf(const char *pData,
        int iDataBytes,
        unsigned int id,
        int iType,
        int iHarmLevel);

/**
 * 将指定文件夹strDir内，业务类型为iType的所有配置项文件添加到动态链接
 * 库中。如果该id的配置项已经存在，则删除之前的配置项，重新添加，即更新.
 *
 */
int AddCfgByDir(int iType, const char *strDir);

/**
 *  从分析模块中移除指定类型的配置项.
 *
 */
int RemoveAllCfg(int iType);

/**
 * 将指定业务类型，id 的配置项 删除.
 *
 */
int RemoveCfgByID(unsigned int id, int iType, int iHarmLevel);

/**
 * 设置分析模块的iType类型功能在发现目标后，是否将数据保存到硬盘文件.
 * 保存路径由初始化函数指定，建议的文件格式：
 * XXX_目标ID_节目ID(PCBID)_YYMMDD_HHMMSS.wav
 *
 */
bool SetRecord(int iType, bool bRecord);

/**
 * 接收需要处理的数据。该函数应该在接收数据并拷贝保存后立即返回，不可以
 * 修改参数结构体的数值.
 *
 */
int SendData2DLL(WavDataUnit *p);

/**
 * 获取版本号.
 *
 */
int GetDLLVersion(char *p, int &length);

/**
 * 某个业务类型待处理的数据单元数量.
 *
 */
int GetDataNum4Process(int iType[], int num[]);

int CloseDLL();

}

#endif

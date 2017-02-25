/*************************************************************************
	> File Name: audizcomm.h
	> Author: 
	> Mail: 
	> Created Time: Thu 23 Feb 2017 03:34:51 AM EST
 ************************************************************************/

#ifndef _AUDIZCOMM_H
#define _AUDIZCOMM_H

//#define AUDIZ_RESULT_MAX_NUM 10
#define MAX_PATH 512
struct Audiz_Result{
    uint64_t m_iPCBID;
    unsigned int m_iTargetID; //目标ID
    unsigned int m_iAlarmType; //业务类型编码
    int m_iHarmLevel; //目标危害度
    float m_fLikely; //识别结果的置信度0~100
    float m_fTargetMatchLen; //匹配上的目标长度
    float m_fSegPosInTarget; //节目数据段在目标模板/配置项中的起始位置
    float m_fSegLikely; //节目数据段(WavDataUnit) 与目标的相似度
    float m_fSegPosInPCB; //节目数据段在节目中的时间位置
    //int64_t m_iPCBID;
    //Audiz_WaveUnit m_Proj;
    char m_strInfo[MAX_PATH];
};

struct AZ_PckVec{
    AZ_PckVec(const char* base, unsigned len):
        base(const_cast<char*>(base)), len(len)
    {}
    char *base;
    unsigned len;
};

#define SPKMDL_HDLEN 64
struct SpkMdlSt{
    char head[SPKMDL_HDLEN];
    unsigned len;
    char *buf;

    void pack_w(std::vector<AZ_PckVec> &pcks) const{
        pcks.clear();
        pcks.push_back(AZ_PckVec(const_cast<char*>(head), SPKMDL_HDLEN));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&const_cast<unsigned &>(len)), sizeof(unsigned)));
        pcks.push_back(AZ_PckVec(const_cast<char*>(buf), len));
    }
    void pack_r(std::vector<AZ_PckVec> &pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec((head), SPKMDL_HDLEN));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&(len)), sizeof(unsigned)));
    }
};

#endif
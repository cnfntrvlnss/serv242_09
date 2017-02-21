/*************************************************************************
	> File Name: audizstruct.h
	> Author: 
	> Mail: 
	> Created Time: Wed 08 Feb 2017 09:59:16 PM EST
 ************************************************************************/

#ifndef _AUDIZSTRUCT_H
#define _AUDIZSTRUCT_H

//#include <cstdint>
#include <cstring>
#include <vector>

typedef long int int64_t;
typedef int int32_t;

struct AZ_PckVec{
    AZ_PckVec(const char* base, unsigned len):
        base(const_cast<char*>(base)), len(len)
    {}
    char *base;
    unsigned len;
};

static const unsigned DATAREDUNSIZE = 8;
const static char dataRedunArr[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7};
struct Audiz_WaveUnit{
    int64_t m_iPCBID; //节目ID, 长度8字节
    int32_t m_iDataLen;//数据字节长度
    char *m_pData;//数据缓冲区
    void *m_pPCB;

    static void appendFixedFields(Audiz_WaveUnit &unit, std::vector<AZ_PckVec> &pcks){
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&unit.m_iPCBID), sizeof(int64_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&unit.m_pPCB),  sizeof(void*)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&unit.m_iDataLen), sizeof(int32_t)));
    }
    void pack_w(std::vector<AZ_PckVec> &pcks) const{
        pcks.clear();
        pcks.push_back(AZ_PckVec(const_cast<char*>(dataRedunArr), 8));
        appendFixedFields(const_cast<Audiz_WaveUnit&>(*this), pcks);
        if(m_iDataLen > 0) pcks.push_back(AZ_PckVec(m_pData, m_iDataLen));
    }

    void pack_r( std::vector<AZ_PckVec> &pcks, char *buBuf){
        pcks.clear();
        pcks.push_back(AZ_PckVec((buBuf), 8));
        appendFixedFields(*this, pcks);
    }
    
    static bool isValid(std::vector<AZ_PckVec> &pcks){
        if(memcmp(pcks[0].base, dataRedunArr, DATAREDUNSIZE) != 0) return false;
        return true;
    }
};

//#define AUDIZ_RESULT_MAX_NUM 10
#define MAX_PATH 512
struct Audiz_Result{
    unsigned int m_iTargetID; //目标ID
    unsigned int m_iAlarmType; //业务类型编码
    int m_iHarmLevel; //目标危害度
    float m_fLikely; //识别结果的置信度0~100
    float m_fTargetMatchLen; //匹配上的目标长度
    float m_fSegPosInTarget; //节目数据段在目标模板/配置项中的起始位置
    float m_fSegLikely; //节目数据段(WavDataUnit) 与目标的相似度
    float m_fSegPosInPCB; //节目数据段在节目中的时间位置
    int64_t m_iPCBID;
    Audiz_WaveUnit m_Proj;
    char m_strInfo[MAX_PATH];
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

#define AZ_DATACENTER "dataCenter"
#define AZ_CFGLINKNAME "cfg link"
#define AZ_DATALINKNAME "data link"
#define AZ_LINKBUILDOK "server ok"

#define AZOP_INVALID_MARK 0
#define AZOP_START_MARK 64
#define AZOP_QUERY_SAMPLE AZOP_START_MARK
#define AZOP_ADD_SAMPLE (AZOP_START_MARK + 2)
//#define AZOP_DEL_SAMPLE (AZOP_START_MARK + 4)
#define AZOP_ADD_SAMPLEFILE (AZOP_START_MARK + 6)
#define AZOP_QUERY_PROJ (AZOP_START_MARK + 8)
#define AZOP_QUERY_SPACE (AZOP_START_MARK + 10)
//#define AZOP_WAIT_RESULT (AZOP_START_MARK + 12)
#define AZOP_REC_RESULT (AZOP_START_MARK + 13)

#define CHARS_AS_INIT32(chs) *(reinterpret_cast<int32_t*>(chs))
#define CHARS_AS_INIT64(chs) *(reinterpret_cast<int64_t*>(chs))

#define LinkNameLen 64
struct Audiz_LinkRequest{
    char head[LinkNameLen];
    unsigned long sid;
    
    void pack_w(std::vector<AZ_PckVec>& pcks) const{
        pcks.clear();
        pcks.push_back(AZ_PckVec(const_cast<char*>(head), LinkNameLen));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(const_cast<unsigned long*>(&sid)), sizeof(unsigned long)));
    }
    void pack_r(std::vector<AZ_PckVec>& pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec((head), LinkNameLen));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&(sid)), sizeof(unsigned long)));
    }
};
struct Audiz_LinkResponse{
    Audiz_LinkRequest req;
    char ack[LinkNameLen];

    void pack_w(std::vector<AZ_PckVec>& pcks) const{
        req.pack_w(pcks);
        pcks.push_back(AZ_PckVec((const_cast<char*>(ack)), LinkNameLen));
    }
    void pack_r(std::vector<AZ_PckVec>& pcks){
        req.pack_r(pcks);
        pcks.push_back(AZ_PckVec(((ack)), LinkNameLen));
    }
};

struct Audiz_PResult_Head{
    int32_t type;
    int32_t ack;

    void pack_w(std::vector<AZ_PckVec>& pcks) const{
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(const_cast<int32_t*>(&type)), sizeof(int32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(const_cast<int32_t*>(&ack)), sizeof(int32_t)));
    }
    void pack_r(std::vector<AZ_PckVec>& pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&type), sizeof(int32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&ack), sizeof(int32_t)));
    }
};


//used in p_client.
struct Audiz_PResult{
    Audiz_PResult(){
        reset();
    }
    Audiz_PResult_Head head;
    char *argBuf;
    //TODO redundant defination.
    int getArgLen(){
        if(head.type == AZOP_QUERY_SAMPLE + 1){
            if(head.ack > 0) return head.ack * SPKMDL_HDLEN;
            else return 0;
        }
        else if(head.type == AZOP_ADD_SAMPLE + 1){
            return 0;
        }
        else if(head.type == AZOP_ADD_SAMPLEFILE + 1){
            return 0;
        }
        else if(head.type == AZOP_REC_RESULT){
            return head.ack * sizeof(Audiz_Result);
        }
        else if(head.type == AZOP_QUERY_PROJ + 1){
            return 0;
        }
        else{
            return -1;
        }
        return -1;
    }
    void reset(){
        head.type = 0;
        head.ack = 0;
        argBuf = NULL;
    }
};

struct Audiz_PRequest_Head{
    int32_t type;
    int32_t addLen;

    void pack_w(std::vector<AZ_PckVec> &pcks) const{
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(const_cast<int32_t*>(&type)), sizeof(int32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(const_cast<int32_t*>(&addLen)), sizeof(int32_t)));
    }
    void pack_r(std::vector<AZ_PckVec> &pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&(type)), sizeof(int32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&(addLen)), sizeof(int32_t)));
    }
};

#endif

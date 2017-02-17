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

struct Audiz_WaveUnit{
    int64_t m_iPCBID; //节目ID, 长度8字节
    int32_t m_iDataLen;//数据字节长度
    char *m_pData;//数据缓冲区
    void *m_pPCB;
};

//#define AUDIZ_RESULT_MAX_NUM 10
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
    char m_strInfo[512]; //保留
};

#define SPKMDL_HDLEN 64
struct SpkMdlSt{
    char head[SPKMDL_HDLEN];
    unsigned len;
    char *buf;
};

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

struct Audiz_PResult_Head{
    int32_t type;
    int32_t ack;
    int getArgLen(){
        if(type == AZOP_QUERY_SAMPLE + 1){
            if(ack > 0) return ack * SPKMDL_HDLEN;
            else return 0;
        }
        else if(type == AZOP_ADD_SAMPLE + 1){
            return 0;
        }
        else if(type == AZOP_ADD_SAMPLEFILE + 1){
            return 0;
        }
        else if(type == AZOP_REC_RESULT){
            return ack * sizeof(Audiz_Result);
        }
        else if(type == AZOP_QUERY_PROJ){
            return 0;
        }
        else{
            return -1;
        }
        return -1;
    }
};

class Audiz_PResult_Head_OnWire{
public:
    static void serialize(const Audiz_PResult_Head &res, std::vector<AZ_PckVec>& pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&const_cast<int32_t&>(res.type)), sizeof(int32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&const_cast<int32_t&>(res.ack)), sizeof(int32_t)));
    }
    static void getEmptyPckVec(Audiz_PResult_Head &res, std::vector<AZ_PckVec>& pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&res.type), sizeof(int32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&res.ack), sizeof(int32_t)));
    }
};

//used in p_client.
struct Audiz_PResult{
    Audiz_PResult_Head head;
    char *argBuf;
    void reset(){
        head.type = 0;
        head.ack = 0;
        argBuf = NULL;
    }
};

struct Audiz_PRequest_Head{
    int32_t type;
    int32_t addLen;
    unsigned getArgLen(){
        return addLen;
    }
};

struct Audiz_PRequest{
    Audiz_PRequest_Head head;
    char *addBuf;
};

class Audiz_PRequest_Head_OnWire{
public:
    static void serialize(const Audiz_PRequest_Head &req, std::vector<AZ_PckVec> &pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&const_cast<int32_t&>(req.type)), sizeof(int32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&const_cast<int32_t&>(req.addLen)), sizeof(int32_t)));
    }
    static void getEmptyPckVec(Audiz_PRequest_Head &req, std::vector<AZ_PckVec> &pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&(req.type)), sizeof(int32_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&(req.addLen)), sizeof(int32_t)));
    }
};

class SpkMdlSt_OnWire{
public:
    static void Serialize(const SpkMdlSt &mdl, std::vector<AZ_PckVec> &pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec(const_cast<char*>(mdl.head), SPKMDL_HDLEN));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&const_cast<unsigned &>(mdl.len)), sizeof(unsigned)));
        pcks.push_back(AZ_PckVec(const_cast<char*>(mdl.buf), mdl.len));
    }
    static void getEmptyPckVec(SpkMdlSt &mdl, std::vector<AZ_PckVec> &pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec((mdl.head), SPKMDL_HDLEN));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&(mdl.len)), sizeof(unsigned)));
    }
};

static const unsigned DATAREDUNSIZE = 8;
const static char dataRedunArr[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7};
class Audiz_Wave_OnWire{
public:
    //static const char* redunStr = "\001\002\003\004\005";
    static void appendFixedFields(Audiz_WaveUnit &unit, std::vector<AZ_PckVec> &pcks){
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&unit.m_iPCBID), sizeof(int64_t)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&unit.m_pPCB),  sizeof(void*)));
        pcks.push_back(AZ_PckVec(reinterpret_cast<char*>(&unit.m_iDataLen), sizeof(int32_t)));
    }
    static void serialize(Audiz_WaveUnit &unit, std::vector<AZ_PckVec> &pcks){
        pcks.clear();
        pcks.push_back(AZ_PckVec(const_cast<char*>(dataRedunArr), 8));
        appendFixedFields(unit, pcks);
        if(unit.m_iDataLen > 0) pcks.push_back(AZ_PckVec(unit.m_pData, unit.m_iDataLen));
    }

    static void getEmptyPckVec( std::vector<AZ_PckVec> &pcks, Audiz_WaveUnit &unit, char *buBuf){
        pcks.clear();
        pcks.push_back(AZ_PckVec((buBuf), 8));
        appendFixedFields(unit, pcks);
    }
    
    static bool isValid(std::vector<AZ_PckVec> &pcks){
        if(memcmp(pcks[0].base, dataRedunArr, DATAREDUNSIZE) != 0) return false;
        return true;
    }
    /*
    static bool parse(const vector<AZ_PckVec> &pcks, Audiz_WaveUnit& unit){
        if(memcmp(pcks[0].base, dataRedunArr, DATAREDUNSIZE) != 0) return false;
        unit.m_iPCBID = CHARS_AS_INIT64(pcks[1].base);
        unit.m_pPCB = *(reinterpret_cast<void **>(&pcks[2].base));
        unit.m_iDataLen = CHARS_AS_INIT32(pcks[3].base);
        if(pcks.size() == 4){
            unit.m_pData = NULL;
        }
        else{
            unit.m_pData = pcks[4].base;
        }
        return true;
    }
    static void getFixedPartShell(vector<AZ_PckVec>& pcks, char* &buBuf){
        pcks.clear();

        pcks.push_back(AZ_PckVec(NULL, DATAREDUNSIZE));
        pcks.push_back(AZ_PckVec(NULL, sizeof(int64_t)));
        pcks.push_back(AZ_PckVec(NULL, sizeof(void*)));
        pcks.push_back(AZ_PckVec(NULL, sizeof(int32_t)));
    }
    */
};

#endif

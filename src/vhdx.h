#pragma once
#ifndef _VHDX_H_
#define _VHDX_H_

#include <iostream>
#include <string>
#include <list>
#include <fstream>
#include "ncIVDParser.h"
using namespace std;

/* struct DataArea
{
    uint32_t offset;
    uint32_t length;
}; */
struct VDVHDXState;
class  VHDXParser : public ncIVDParser
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NCIVDPARSE
    VHDXParser();
    ~VHDXParser();

private:
    void vhdxInit(VDVHDXState *s);
    bool vhdxSignatureCheck(VDVHDXState *s);
    int  vhdxRegionCheck(VDVHDXState *s, uint64_t start, uint64_t length);
    void vhdxCalcBatEntries(VDVHDXState *s);
    int  vhdxParseMetadata(VDVHDXState *s);
    int  vhdxOpenRegionTables(VDVHDXState *s);
    void vhdxRegionRegister(VDVHDXState *s, uint64_t start, uint64_t length);
    void vhdxParseHeader(VDVHDXState *s);
    void vhdxRegionUnregisterAll(VDVHDXState *s);
private:
    std::ifstream fileHandle;
    VDVHDXState *s;
};



#endif // !_VHDX_H_



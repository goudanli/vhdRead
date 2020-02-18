#pragma once
#include <iostream>
#include <string>
#include <list>
#include <fstream>
#include "ncIVDParser.h"

/* struct DataArea
{
    uint32_t offset;
    uint32_t length;
}; */

struct VDVHDState;
class  VHDParser : public ncIVDParser
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NCIVDPARSE
    VHDParser();
    ~VHDParser();

private:
    void vhdParseHeader(VDVHDState *s);
    void vhdInit(VDVHDState *pImage);
private:
    std::string _filePath;
    std::ifstream fileHandle;
    VDVHDState *pImage;
};

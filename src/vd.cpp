#include "vd.h"
using namespace std;

uint16_t swab16(const uint16_t & v)
{
    return (v & 0xff) << 8 | (v >> 8);
}

uint32_t swap32(const uint32_t &v)
{
    return (v >> 24)
        | ((v & 0x00ff0000) >> 8)
        | ((v & 0x0000ff00) << 8)
        | (v << 24);
}

uint64_t swap64(const uint64_t &v)
{
    return (v >> 56)
        | ((v & 0x00ff000000000000) >> 40)
        | ((v & 0x0000ff0000000000) >> 24)
        | ((v & 0x000000ff00000000) >> 8)
        | ((v & 0x00000000ff000000) << 8)
        | ((v & 0x0000000000ff0000) << 24)
        | ((v & 0x000000000000ff00) << 40)
        | (v << 56);
}

bool GetEndianness()
{
    short s = 0x0110;
    char *p = (char *)&s;
    if (p[0] == 0x10)
        return false;// 小端格式  
    else
        return true;// 大端格式  
}

void Read(std::ifstream & infile, uint64_t offset, char * buffer, uint64_t size)
{
    infile.clear();
    infile.seekg(offset, ios::beg);
    infile.read(buffer, size);
}

void Write(std::ofstream & outfile, uint64_t offset, char * buffer, uint64_t size)
{
    outfile.clear();
    outfile.seekp(offset, ios::beg);
    outfile.write(buffer, size);
}

uint64_t GetFileSize(std::ifstream & infile)
{
    infile.seekg(0,ios::end);
    uint64_t fileSize = infile.tellg();
    infile.seekg(0, ios::beg);
    return fileSize;
}


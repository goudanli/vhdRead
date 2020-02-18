#pragma once
#include <iostream>
#include <string>
#include <list>
#include <fstream>


uint16_t swab16(const uint16_t & v);

uint32_t swap32(const uint32_t &v);

uint64_t swap64(const uint64_t &v);

bool GetEndianness();

void Read(std::ifstream & infile, uint64_t offset, char * buffer, uint64_t size);

void Write(std::ofstream & outfile, uint64_t offset, char * buffer, uint64_t size);

uint64_t GetFileSize(std::ifstream & infile);

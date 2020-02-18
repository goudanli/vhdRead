#include <abprec.h>
#include "ncIVDParser.h"


static void  alignDataArea(std::list<DataArea> &arealist, size_t len)
{
    if (len < 1 || len > 256) {
        throw exception("unsupported len");
    }

    std::list<DataArea>::iterator iter1 = arealist.begin();
    if ((iter1->length) % len == 0) {
        for (; iter1 != arealist.end();) {
            //if (iter1->length > len) {
                uint32_t start = iter1->offset + len;
                uint32_t end = iter1->offset + iter1->length;
                std::list<DataArea> tmp;
                for (uint32_t i = start; i < end; i += len) {
                    DataArea area;
                    area.offset = i;
                    area.length =len;
                    tmp.push_back(area);
                }
                iter1->length = len;
                iter1 ++;
                arealist.insert(iter1, tmp.begin(), tmp.end());
        }
    }
}


static void  internalMerge(std::list<DataArea> &arealist)
{
    std::list<DataArea>::iterator first = arealist.begin();
    std::list<DataArea>::iterator second = first;
    first++;
    for (; first != arealist.end();) {
        if ((second->offset + second->length) == first->offset) {
            second->length += first->length;
            first = arealist.erase(first);
        }
        else {
            first++;
            second++;
        }
    }
}

//
static void  externalMarge(std::list<DataArea> &arealist1, std::list<DataArea> &arealist2, std::list<DataArea> &result)
{
    std::list<DataArea>::iterator iter1,iter2;
    iter1 = arealist1.begin();
    iter2 = arealist2.begin();
    for (; iter1 != arealist1.end()&& iter2 != arealist2.end();) {
        if (iter2->offset > iter1->offset) {
            DataArea area;
            area.offset = iter1->offset;
            area.length = iter1->length;
            result.push_back(area);
            iter1++;

        }
        else if(iter2->offset < iter1->offset){
            DataArea area;
            area.offset = iter2->offset;
            area.length = iter2->length;
            result.push_back(area);
            iter2++;
        }
        else if(iter2->offset == iter1->offset)
        {
            DataArea area;
            area.offset = iter2->offset;
            area.length = iter2->length;
            result.push_back(area);
            iter2++;
            iter1++;
        }
    }
    for (; iter1 != arealist1.end(); ++iter1) {
        result.push_back(*iter1);
    }
    for (; iter2 != arealist2.end(); ++iter2) {
        result.push_back(*iter2);
    }
}


void GetBackupDisksBlocks(ncIVDParser *parser,std::list<string> & backupDisksPath,std::list<DataArea> & backupBlocks)
{
    std::list<DataArea> arealist;
    size_t len = 0;
    for(auto & diskPath : backupDisksPath) {
        parser->Open(diskPath);
        std::list<DataArea> arealistTmp;
        parser->GetDataAreaList(arealistTmp);
        //internalMerge(arealistTmp);
        //arealist.insert(arealist.end(),arealistTmp.begin(),arealistTmp.end());
        if(arealistTmp.size()) {
            if(len) {
                if(arealistTmp.front().length > len) {
                    alignDataArea(arealistTmp,len);
                    std::list<DataArea> result;
                    externalMarge(arealistTmp,arealist,result);
                    arealist.swap(result);
                }
                else if(arealistTmp.front().length < len) {
                    len = arealistTmp.front().length;
                    alignDataArea(arealist,len);
                    std::list<DataArea> result;
                    externalMarge(arealistTmp,arealist,result);
                    arealist.swap(result);
                }
                else if(arealistTmp.front().length == len) {
                    std::list<DataArea> result;
                    externalMarge(arealistTmp,arealist,result);
                    arealist.swap(result);
                }
            }
            else {
                len = arealistTmp.front().length;
                arealist.swap(arealistTmp);
            }
            arealistTmp.clear();
        }
        parser->Close();
    }
    internalMerge(arealist);
    backupBlocks.swap(arealist);
}
/*
 * DO NOT EDIT.  THIS FILE IS GENERATED FROM D:/code/Apollo/apollo/src/cpp/common/calchashmanager/public/ncIVDParser.idl
 */

#ifndef __gen_ncIVDParser_h__
#define __gen_ncIVDParser_h__


#ifndef __gen_nsISupports_h__
#include "nsISupports.h"
#endif

/* For IDL files that don't want to include root IDL files. */
#ifndef NS_NO_VTABLE
#define NS_NO_VTABLE
#endif
#include "nsID.h"
#include "nsISupportsBase.h"
struct DataArea
{
    uint32_t offset;
    uint32_t length;
};

/* starting interface:    ncIVDParser */
#define NCIVDPARSE_IID_STR "ca919b23-7dec-4f13-832d-a7a76e867c8d"

#define NCIVDPARSE_IID \
  {0xca919b23, 0x7dec, 0x4f13, \
    { 0x83, 0x2d, 0xa7, 0xa7, 0x6e, 0x86, 0x7c, 0x8d }}

class NS_NO_VTABLE ncIVDParser : public nsISupports {
 public: 

  NS_DECLARE_STATIC_IID_ACCESSOR(NCIVDPARSE_IID)

  /* [notxpcom] void Open ([const] in stlstringRef filePath); */
  NS_IMETHOD_(void) Open(const std::string & filePath) = 0;

  /* [notxpcom] void Close (); */
  NS_IMETHOD_(void) Close(void) = 0;

  /* [notxpcom] void GetDataAreaList (in ListDataAreaRef arealist); */
  NS_IMETHOD_(void) GetDataAreaList(std::list<DataArea> & arealist) = 0;

};

  NS_DEFINE_STATIC_IID_ACCESSOR(ncIVDParser, NCIVDPARSE_IID)

/* Use this macro when declaring classes that implement this interface. */
#define NS_DECL_NCIVDPARSE \
  NS_IMETHOD_(void) Open(const std::string & filePath); \
  NS_IMETHOD_(void) Close(void); \
  NS_IMETHOD_(void) GetDataAreaList(std::list<DataArea> & arealist); 

/* Use this macro to declare functions that forward the behavior of this interface to another object. */
#define NS_FORWARD_NCIVDPARSE(_to) \
  NS_IMETHOD_(void) Open(const std::string & filePath) { return _to Open(filePath); } \
  NS_IMETHOD_(void) Close(void) { return _to Close(); } \
  NS_IMETHOD_(void) GetDataAreaList(std::list<DataArea> & arealist) { return _to GetDataAreaList(arealist); } 

/* Use this macro to declare functions that forward the behavior of this interface to another object in a safe way. */
#define NS_FORWARD_SAFE_NCIVDPARSE(_to) \
  NS_IMETHOD_(void) Open(const std::string & filePath) { return !_to ? NS_ERROR_NULL_POINTER : _to->Open(filePath); } \
  NS_IMETHOD_(void) Close(void) { return !_to ? NS_ERROR_NULL_POINTER : _to->Close(); } \
  NS_IMETHOD_(void) GetDataAreaList(std::list<DataArea> & arealist) { return !_to ? NS_ERROR_NULL_POINTER : _to->GetDataAreaList(arealist); } 


/* void  alignDataArea(std::list<DataArea> &arealist, int len);

void  internalMerge(std::list<DataArea> &arealist);

void  externalMarge(std::list<DataArea> &arealist1, std::list<DataArea> &arealist2, std::list<DataArea> &result); */

void GetBackupDisksBlocks(ncIVDParser *parser,std::list<string> & backupDisksPath,std::list<DataArea> & arealist);

#endif /* __gen_ncIVDParser_h__ */

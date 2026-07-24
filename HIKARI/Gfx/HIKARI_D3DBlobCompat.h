#pragma once
#include <unknwn.h>
#include <basetsd.h>
#include <winnt.h>

#ifndef __ID3D10Blob_INTERFACE_DEFINED__
#define __ID3D10Blob_INTERFACE_DEFINED__
struct ID3D10Blob : public IUnknown
{
    virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};
#endif

#ifndef __ID3DBlob_INTERFACE_DEFINED__
#define __ID3DBlob_INTERFACE_DEFINED__
typedef ID3D10Blob ID3DBlob;
#endif

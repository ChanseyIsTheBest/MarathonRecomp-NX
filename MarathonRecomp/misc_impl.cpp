#include "stdafx.h"
#include <kernel/function.h>
#include <kernel/xdm.h>

uint32_t QueryPerformanceCounterImpl(LARGE_INTEGER* lpPerformanceCount)
{
    lpPerformanceCount->QuadPart = ByteSwap(std::chrono::steady_clock::now().time_since_epoch().count());
    return TRUE;
}

uint32_t QueryPerformanceFrequencyImpl(LARGE_INTEGER* lpFrequency)
{
    constexpr auto Frequency = std::chrono::steady_clock::period::den / std::chrono::steady_clock::period::num;
    lpFrequency->QuadPart = ByteSwap(Frequency);
    return TRUE;
}

uint32_t GetTickCountImpl()
{
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void GlobalMemoryStatusImpl(XLPMEMORYSTATUS lpMemoryStatus)
{
    lpMemoryStatus->dwLength = sizeof(XMEMORYSTATUS);
    lpMemoryStatus->dwMemoryLoad = 0;
    lpMemoryStatus->dwTotalPhys = 0x20000000;
    lpMemoryStatus->dwAvailPhys = 0x20000000;
    lpMemoryStatus->dwTotalPageFile = 0x20000000;
    lpMemoryStatus->dwAvailPageFile = 0x20000000;
    lpMemoryStatus->dwTotalVirtual = 0x20000000;
    lpMemoryStatus->dwAvailVirtual = 0x20000000;
}

#if defined(__SWITCH__)
// [Switch] os/switch/perf/native_crt.cpp: memmove compiled into the app, which keeps x16 free for
// os/switch/exception_switch.cpp to resume a fault on an uncommitted guest page; the prebuilt newlib memcpy/memmove
// (one function) does not. The same bytes as that memcpy/memmove. (newlib's memset leaves x16 alone.)
void* GuestMemmove(void* destination, const void* source, size_t size);
#endif

#ifndef _WIN32
int memcpy_s(void* dest, size_t dest_size, const void* src, size_t count) {
    if (dest == nullptr || src == nullptr) {
        return EINVAL;
    }
    if (dest_size < count) {
        return ERANGE;
    }

#if defined(__SWITCH__)
    GuestMemmove(dest, src, count);
#else
    memcpy(dest, src, count);
#endif
    return 0;
}
#endif

#if defined(__SWITCH__)
GUEST_FUNCTION_HOOK(sub_826DF680, GuestMemmove);
#else
GUEST_FUNCTION_HOOK(sub_826DF680, memcpy);
#endif
// GUEST_FUNCTION_HOOK(sub_831CCB98, memcpy);
// GUEST_FUNCTION_HOOK(sub_831CEAE0, memcpy);
// GUEST_FUNCTION_HOOK(sub_831CEE04, memcpy);
// GUEST_FUNCTION_HOOK(sub_831CF2D0, memcpy);
// GUEST_FUNCTION_HOOK(sub_831CF660, memcpy);
// GUEST_FUNCTION_HOOK(sub_826DFAA0, memcpy); // [Switch] os/switch/perf/native_crt.cpp: memcpy unless the ranges overlap
#if defined(__SWITCH__)
GUEST_FUNCTION_HOOK(sub_826DE940, GuestMemmove);
#else
GUEST_FUNCTION_HOOK(sub_826DE940, memmove);
#endif
GUEST_FUNCTION_HOOK(sub_826DFD40, memset);
GUEST_FUNCTION_HOOK(sub_826DEA00, memcpy_s);
// GUEST_FUNCTION_HOOK(sub_831CCAA0, memset);

#ifdef _WIN32
GUEST_FUNCTION_HOOK(sub_82537770, OutputDebugStringA);
#else
GUEST_FUNCTION_STUB(sub_82537770);
#endif

GUEST_FUNCTION_HOOK(sub_826FCE58, QueryPerformanceCounterImpl); // replaced
GUEST_FUNCTION_HOOK(sub_826FC3C8, QueryPerformanceFrequencyImpl); // repalced
GUEST_FUNCTION_HOOK(sub_826FD790, GetTickCountImpl); // replaced

// [Switch] More guest functions replaced by native code, each behind a [Switch] key (user/switch/config_native.inl):
// the RTTI functions, the D3D shader constant setters, CRT string routines and the archive loader's zlib inflate,
// in os/switch/perf/native_*.cpp.

// GUEST_FUNCTION_HOOK(sub_82BD4BC0, GlobalMemoryStatusImpl);

// sprintf
// PPC_FUNC(sub_82BD4AE8)
// {
//     sub_831B1630(ctx, base);
// }

#include <cstddef>
#include <switch.h>

extern "C"
{
    u32 __NvOptimusEnablement = 1;
    u32 __NvDeveloperOption = 1;
    u32 __nx_applet_type = AppletType_Application;
    // Size the newlib heap to nearly all RAM: guest-window commits are backed by
    // heap memory remapped via the process-memory syscalls, so it must be large.
    size_t __nx_heap_size = 0;

    // Exception stack for __libnx_exception_handler (exception_switch.cpp).
    alignas(16) u8 __nx_exception_stack[0x10000];
    u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

    // HOS only time-slices threads at the preemption priority (0x3B); other
    // priorities are cooperative. Recompiled guest code spin-waits constantly,
    // so guest threads run at 0x3B and host service threads run higher
    // (numerically lower) to always preempt them.
    void SwitchSetCurrentThreadPriority(int priority)
    {
        Thread* self = threadGetSelf();
        if (self != nullptr)
            svcSetThreadPriority(self->handle, priority);
    }
}

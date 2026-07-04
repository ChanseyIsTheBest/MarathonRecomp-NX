// Weak Vulkan ICD fallback. The real Mesa/NVK libvulkan.a overrides these when
// linked; without it they let the executable link and make plume fail cleanly
// at instance creation (zero extensions -> required-extension check bails out
// before any null Vulkan call), producing a bootable non-rendering NRO.
#if defined(__SWITCH__)

#include <cstdint>
#include <cstring>

extern "C"
{
    typedef void (*PFN_vkVoidFunction)(void);
    typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(void* instance, const char* pName);

    static constexpr int32_t kVkSuccess = 0;
    static constexpr uint32_t kVkApiVersion12 = (1u << 22) | (2u << 12);

    static int32_t Stub_EnumerateInstanceExtensionProperties(const char* /*layer*/, uint32_t* pCount, void* /*pProps*/)
    {
        if (pCount != nullptr)
            *pCount = 0;
        return kVkSuccess;
    }

    static int32_t Stub_EnumerateInstanceVersion(uint32_t* pApiVersion)
    {
        if (pApiVersion != nullptr)
            *pApiVersion = kVkApiVersion12;
        return kVkSuccess;
    }

    __attribute__((weak)) PFN_vkVoidFunction vk_icdGetInstanceProcAddr(void* /*instance*/, const char* pName)
    {
        if (pName == nullptr)
            return nullptr;

        if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(&vk_icdGetInstanceProcAddr);

        if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(&Stub_EnumerateInstanceExtensionProperties);

        if (std::strcmp(pName, "vkEnumerateInstanceVersion") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(&Stub_EnumerateInstanceVersion);

        return nullptr;
    }

    __attribute__((weak)) int32_t vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion)
    {
        if (pSupportedVersion != nullptr && *pSupportedVersion > 5u)
            *pSupportedVersion = 5u;
        return kVkSuccess;
    }
}

#endif

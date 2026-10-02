#if defined(__SWITCH__)

#include "spirv_analysis_switch.h"

#include <cstring>
#include <vector>

// [Switch] See spirv_analysis_switch.h. The modules are the smol-v decoded SPIR-V of the shader cache and the app
// shaders (DXC output), i.e. what vkCreateShaderModule receives.

namespace
{
    constexpr uint32_t SPIRV_MAGIC = 0x07230203;

    constexpr uint32_t OP_UNDEF = 1;
    constexpr uint32_t OP_EXT_INST = 12;
    constexpr uint32_t OP_EXECUTION_MODE = 16;
    constexpr uint32_t OP_CAPABILITY = 17;
    constexpr uint32_t OP_TYPE_POINTER = 32;
    constexpr uint32_t OP_TYPE_FORWARD_POINTER = 39;
    constexpr uint32_t OP_FUNCTION = 54;
    constexpr uint32_t OP_FUNCTION_PARAMETER = 55;
    constexpr uint32_t OP_FUNCTION_CALL = 57;
    constexpr uint32_t OP_VARIABLE = 59;
    constexpr uint32_t OP_LOAD = 61;
    constexpr uint32_t OP_STORE = 62;
    constexpr uint32_t OP_COPY_MEMORY = 63;
    constexpr uint32_t OP_COPY_MEMORY_SIZED = 64;
    constexpr uint32_t OP_ACCESS_CHAIN = 65;
    constexpr uint32_t OP_IN_BOUNDS_ACCESS_CHAIN = 66;
    constexpr uint32_t OP_PTR_ACCESS_CHAIN = 67;
    constexpr uint32_t OP_IN_BOUNDS_PTR_ACCESS_CHAIN = 70;
    constexpr uint32_t OP_DECORATE = 71;
    constexpr uint32_t OP_MEMBER_DECORATE = 72;
    constexpr uint32_t OP_COPY_OBJECT = 83;
    constexpr uint32_t OP_IMAGE_WRITE = 99;
    constexpr uint32_t OP_CONVERT_U_TO_PTR = 120;
    constexpr uint32_t OP_BITCAST = 124;
    constexpr uint32_t OP_SELECT = 169;
    constexpr uint32_t OP_ATOMIC_FIRST = 227; // OpAtomicLoad .. OpAtomicXor
    constexpr uint32_t OP_ATOMIC_LAST = 242;
    constexpr uint32_t OP_PHI = 245;
    constexpr uint32_t OP_KILL = 252;
    constexpr uint32_t OP_ATOMIC_FLAG_TEST_AND_SET = 318;
    constexpr uint32_t OP_ATOMIC_FLAG_CLEAR = 319;
    constexpr uint32_t OP_TERMINATE_INVOCATION = 4416;
    constexpr uint32_t OP_DEMOTE_TO_HELPER_INVOCATION = 5380;
    constexpr uint32_t OP_ATOMIC_FMIN_EXT = 5614;
    constexpr uint32_t OP_ATOMIC_FMAX_EXT = 5615;
    constexpr uint32_t OP_ATOMIC_FADD_EXT = 6035;

    constexpr uint32_t CAPABILITY_VARIABLE_POINTERS_STORAGE_BUFFER = 4441;
    constexpr uint32_t CAPABILITY_VARIABLE_POINTERS = 4442;
    constexpr uint32_t CAPABILITY_PHYSICAL_STORAGE_BUFFER_ADDRESSES = 5347;

    constexpr uint32_t DECORATION_BUILTIN = 11;
    constexpr uint32_t DECORATION_LOCATION = 30;
    constexpr uint32_t DECORATION_DESCRIPTOR_SET = 34;

    constexpr uint32_t BUILTIN_SAMPLE_MASK = 20;
    constexpr uint32_t BUILTIN_FRAG_DEPTH = 22;
    constexpr uint32_t BUILTIN_FRAG_STENCIL_REF_EXT = 5014;

    constexpr uint32_t EXECUTION_MODE_DEPTH_REPLACING = 12;
    constexpr uint32_t EXECUTION_MODE_STENCIL_REF_REPLACING_EXT = 5027;

    constexpr uint32_t STORAGE_CLASS_INPUT = 1;
    constexpr uint32_t STORAGE_CLASS_OUTPUT = 3;
    constexpr uint32_t STORAGE_CLASS_PRIVATE = 6;
    constexpr uint32_t STORAGE_CLASS_FUNCTION = 7;

    // The words of a module, read without assuming alignment. Valid() checks the header; every instruction is then
    // bounds-checked by ForEachInstruction, which stops (and reports false) at a malformed one.
    struct SpirvModule
    {
        const uint8_t* data;
        size_t count;

        SpirvModule(const void* data, size_t size)
            : data(static_cast<const uint8_t*>(data)), count(size / sizeof(uint32_t))
        {
        }

        uint32_t Word(size_t index) const
        {
            uint32_t value;
            memcpy(&value, data + index * sizeof(uint32_t), sizeof(value));
            return value;
        }

        bool Valid() const
        {
            return data != nullptr && count >= 5 && Word(0) == SPIRV_MAGIC;
        }

        // The id bound of the header: every id is below it.
        uint32_t Bound() const
        {
            return Word(3);
        }

        // function(opcode, index of the instruction's first word, word count) returns false to stop early.
        template<typename TFunction>
        bool ForEachInstruction(const TFunction& function) const
        {
            for (size_t i = 5; i < count;)
            {
                const uint32_t instruction = Word(i);
                const uint32_t wordCount = instruction >> 16;
                const uint32_t opcode = instruction & 0xFFFF;

                if (wordCount == 0 || i + wordCount > count)
                    return false;

                if (!function(opcode, i, wordCount))
                    return true;

                i += wordCount;
            }

            return true;
        }
    };

    // Instructions whose result can be a pointer, with the form (opcode, result type, result id, ...).
    bool MayProducePointer(uint32_t opcode)
    {
        switch (opcode)
        {
        case OP_UNDEF:
        case OP_FUNCTION_PARAMETER:
        case OP_FUNCTION_CALL:
        case OP_VARIABLE:
        case OP_LOAD:
        case OP_ACCESS_CHAIN:
        case OP_IN_BOUNDS_ACCESS_CHAIN:
        case OP_PTR_ACCESS_CHAIN:
        case OP_IN_BOUNDS_PTR_ACCESS_CHAIN:
        case OP_COPY_OBJECT:
        case OP_CONVERT_U_TO_PTR:
        case OP_BITCAST:
        case OP_SELECT:
        case OP_PHI:
            return true;
        default:
            return false;
        }
    }

    bool IsAtomic(uint32_t opcode)
    {
        return (opcode >= OP_ATOMIC_FIRST && opcode <= OP_ATOMIC_LAST) || opcode == OP_ATOMIC_FLAG_TEST_AND_SET ||
            opcode == OP_ATOMIC_FLAG_CLEAR || opcode == OP_ATOMIC_FMIN_EXT || opcode == OP_ATOMIC_FMAX_EXT ||
            opcode == OP_ATOMIC_FADD_EXT;
    }
}

namespace switch_spirv
{
    // Capabilities and decorations come before the first function, so the scan stops there.
    bool ReadsConstantsThroughUbo(const void* data, size_t size, uint32_t uboSet)
    {
        const SpirvModule module(data, size);
        if (!module.Valid())
            return false;

        bool pointers = false;
        bool uboSetDecorated = false;

        const bool complete = module.ForEachInstruction([&](uint32_t opcode, size_t i, uint32_t wordCount)
            {
                if (opcode == OP_FUNCTION)
                    return false;

                if (opcode == OP_CAPABILITY && wordCount >= 2 && module.Word(i + 1) == CAPABILITY_PHYSICAL_STORAGE_BUFFER_ADDRESSES)
                    pointers = true;
                else if (opcode == OP_DECORATE && wordCount >= 4 && module.Word(i + 2) == DECORATION_DESCRIPTOR_SET && module.Word(i + 3) == uboSet)
                    uboSetDecorated = true;

                return true;
            });

        return complete && (uboSetDecorated || !pointers);
    }

    bool RemovableInDepthOnlyPass(const void* data, size_t size, uint32_t allowedKills)
    {
        const SpirvModule module(data, size);
        if (!module.Valid())
            return false;

        const uint32_t bound = module.Bound();
        if (bound == 0 || bound > (1u << 22))
            return false;

        // Storage class (+ 1, 0 = not a pointer type) of each pointer type, and the result type of each id that may be
        // a pointer, to find what every store writes to.
        std::vector<uint32_t> pointerTypeStorage(bound, 0);
        std::vector<uint32_t> resultTypes(bound, 0);
        std::vector<uint32_t> storeTargets;
        uint32_t kills = 0;
        bool effect = false;

        const bool complete = module.ForEachInstruction([&](uint32_t opcode, size_t i, uint32_t wordCount)
            {
                if ((opcode == OP_DECORATE && wordCount >= 4 && module.Word(i + 2) == DECORATION_BUILTIN) ||
                    (opcode == OP_MEMBER_DECORATE && wordCount >= 5 && module.Word(i + 3) == DECORATION_BUILTIN))
                {
                    const uint32_t builtIn = module.Word(i + wordCount - 1);
                    if (builtIn == BUILTIN_FRAG_DEPTH || builtIn == BUILTIN_SAMPLE_MASK || builtIn == BUILTIN_FRAG_STENCIL_REF_EXT)
                        effect = true;
                }
                else if (opcode == OP_EXECUTION_MODE && wordCount >= 3 &&
                    (module.Word(i + 2) == EXECUTION_MODE_DEPTH_REPLACING || module.Word(i + 2) == EXECUTION_MODE_STENCIL_REF_REPLACING_EXT))
                {
                    effect = true;
                }
                else if ((opcode == OP_TYPE_POINTER || opcode == OP_TYPE_FORWARD_POINTER) && wordCount >= 3)
                {
                    const uint32_t type = module.Word(i + 1);
                    if (type < bound)
                        pointerTypeStorage[type] = module.Word(i + 2) + 1;
                }
                else if (opcode == OP_STORE || opcode == OP_COPY_MEMORY || opcode == OP_COPY_MEMORY_SIZED)
                {
                    if (wordCount >= 3)
                        storeTargets.push_back(module.Word(i + 1));
                    else
                        effect = true;
                }
                else if (opcode == OP_IMAGE_WRITE || IsAtomic(opcode))
                {
                    effect = true;
                }
                else if (opcode == OP_KILL || opcode == OP_TERMINATE_INVOCATION || opcode == OP_DEMOTE_TO_HELPER_INVOCATION)
                {
                    kills++;
                }

                if (MayProducePointer(opcode) && wordCount >= 3)
                {
                    const uint32_t id = module.Word(i + 2);
                    if (id < bound)
                        resultTypes[id] = module.Word(i + 1);
                }

                return !effect;
            });

        if (!complete || effect || kills > allowedKills)
            return false;

        // Stores only to the shader's own variables and outputs (colour outputs; depth, stencil and sample mask
        // outputs were rejected above). A store through a buffer, an image or a physical pointer is an effect.
        for (const uint32_t target : storeTargets)
        {
            if (target >= bound)
                return false;

            const uint32_t type = resultTypes[target];
            if (type == 0 || type >= bound || pointerTypeStorage[type] == 0)
                return false;

            const uint32_t storageClass = pointerTypeStorage[type] - 1;
            if (storageClass != STORAGE_CLASS_FUNCTION && storageClass != STORAGE_CLASS_PRIVATE && storageClass != STORAGE_CLASS_OUTPUT)
                return false;
        }

        return true;
    }

    // SPIR-V can only read an Input variable through OpLoad, OpCopyMemory(Sized), an access chain, OpCopyObject of the
    // pointer, a function call argument or an InterpolateAt* extended instruction; the pointer is looked for in
    // exactly those operands. Variable pointers could carry it anywhere else.
    uint32_t InputLocationsRead(const void* data, size_t size)
    {
        const SpirvModule module(data, size);
        if (!module.Valid())
            return ~0u;

        const uint32_t bound = module.Bound();
        if (bound == 0 || bound > (1u << 22))
            return ~0u;

        constexpr uint32_t NO_LOCATION = ~0u;
        std::vector<uint32_t> locations(bound, NO_LOCATION);
        std::vector<uint8_t> builtIns(bound, 0);
        std::vector<uint8_t> inputs(bound, 0);
        std::vector<uint32_t> inputIds;
        bool unsure = false;

        // Declarations come first.
        bool complete = module.ForEachInstruction([&](uint32_t opcode, size_t i, uint32_t wordCount)
            {
                if (opcode == OP_CAPABILITY && wordCount >= 2 &&
                    (module.Word(i + 1) == CAPABILITY_VARIABLE_POINTERS || module.Word(i + 1) == CAPABILITY_VARIABLE_POINTERS_STORAGE_BUFFER))
                {
                    unsure = true;
                }
                else if (opcode == OP_DECORATE && wordCount >= 4 && module.Word(i + 1) < bound)
                {
                    if (module.Word(i + 2) == DECORATION_LOCATION)
                        locations[module.Word(i + 1)] = module.Word(i + 3);
                    else if (module.Word(i + 2) == DECORATION_BUILTIN)
                        builtIns[module.Word(i + 1)] = 1;
                }
                else if (opcode == OP_VARIABLE && wordCount >= 4 && module.Word(i + 3) == STORAGE_CLASS_INPUT && module.Word(i + 2) < bound)
                {
                    inputs[module.Word(i + 2)] = 1;
                    inputIds.push_back(module.Word(i + 2));
                }

                return !unsure;
            });

        if (!complete || unsure)
            return ~0u;

        // An input that is neither at a location nor a built-in (a block with member locations): unknown.
        for (const uint32_t id : inputIds)
        {
            if (locations[id] == NO_LOCATION && builtIns[id] == 0)
                return ~0u;
        }

        uint32_t read = 0;
        auto markRead = [&](uint32_t id)
            {
                if (id >= bound || inputs[id] == 0 || locations[id] == NO_LOCATION)
                    return; // Not an input, or a built-in (position, front face...), not an interpolator.

                read |= locations[id] < 32 ? (1u << locations[id]) : 0u;
            };

        complete = module.ForEachInstruction([&](uint32_t opcode, size_t i, uint32_t wordCount)
            {
                switch (opcode)
                {
                case OP_LOAD:
                case OP_ACCESS_CHAIN:
                case OP_IN_BOUNDS_ACCESS_CHAIN:
                case OP_PTR_ACCESS_CHAIN:
                case OP_IN_BOUNDS_PTR_ACCESS_CHAIN:
                case OP_COPY_OBJECT:
                    if (wordCount >= 4)
                        markRead(module.Word(i + 3));
                    break;

                case OP_COPY_MEMORY:
                case OP_COPY_MEMORY_SIZED:
                    if (wordCount >= 3)
                        markRead(module.Word(i + 2));
                    break;

                case OP_FUNCTION_CALL:
                    for (size_t j = 4; j < wordCount; j++)
                        markRead(module.Word(i + j));
                    break;

                case OP_EXT_INST:
                    for (size_t j = 5; j < wordCount; j++)
                        markRead(module.Word(i + j));
                    break;
                }

                return true;
            });

        return complete ? read : ~0u;
    }
}

#endif

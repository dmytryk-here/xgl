/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2014-2018 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 **********************************************************************************************************************/
/**
 ***********************************************************************************************************************
 * @file  vk_utils.h
 * @brief Utility functions for Vulkan.
 ***********************************************************************************************************************
 */

#ifndef __VK_UTILS_H__
#define __VK_UTILS_H__

#pragma once

#include "include/khronos/vulkan.h"
#include "pal.h"
#include "palAssert.h"
#include "palSysUtil.h"
#include "palSysMemory.h"
#include "palFormatInfo.h"
#include "palCmdBuffer.h"

#include <cwchar>
#include <cctype>
#include <type_traits>
#include <malloc.h>

#include <unistd.h>
#include <linux/limits.h>

// Reuse some PAL macros here
#define VK_ASSERT PAL_ASSERT
#define VK_DEBUG_BUILD_ONLY_ASSERT PAL_DEBUG_BUILD_ONLY_ASSERT
#define VK_ALERT PAL_ALERT
#define VK_SOFT_ASSERT(expr) VK_ALERT(!(expr))
#define VK_INLINE PAL_INLINE
#define VK_NEW PAL_NEW
#define VK_PLACEMENT_NEW PAL_PLACEMENT_NEW
#define VK_NOT_IMPLEMENTED do { PAL_NOT_IMPLEMENTED(); } while (0)
#define VK_NEVER_CALLED PAL_NEVER_CALLED
#define VK_NOT_TESTED PAL_NOT_TESTED

#if DEBUG
#define VK_DBG_DECL(decl)      decl
#define VK_DBG_EXPR(expr)      expr
#define VK_DBG_CHECK(cond,msg) VK_ASSERT((cond) && (msg))
#else
#define VK_DBG_DECL(decl)
#define VK_DBG_EXPR(expr)
#define VK_DBG_CHECK(cond,msg)
#endif

#if   defined(__GNUG__)
#define VK_FORCEINLINE __attribute__((always_inline))
#else
#define VK_FORCEINLINE inline
#endif

// Wrap _malloca and _freea for compilers other than MSVS
#define VK_ALLOC_A(_numBytes) alloca(_numBytes)

// Helper macro to mark some input ignored intentionally; eventually all such macro uses should disappear
#define VK_IGNORE(input)

// Default alignment for memory allocation
#define VK_DEFAULT_MEM_ALIGN 16

#define VK_ARRAY_SIZE(a) ((sizeof(a) / sizeof(a[0])))

#define VK_ENUM_IN_RANGE(value, type)   (((value) >= type##_BEGIN_RANGE) && ((value) <= type##_END_RANGE))
#define VK_ENUM_IN_RANGE_AMD(value, type)   (((value) >= type##_BEGIN_RANGE_AMD) && ((value) <= type##_END_RANGE_AMD))

// Helper macro to make code conditional on being a minimum PAL major/minor version
#define VK_IS_PAL_VERSION_AT_LEAST(major, minor) \
    (PAL_CLIENT_INTERFACE_MAJOR_VERSION == major && PAL_CLIENT_INTERFACE_MINOR_VERSION >= minor) || \
    (PAL_CLIENT_INTERFACE_MAJOR_VERSION > major)

namespace vk
{

// This structure represents the common data that's at the front of almost all
// Vulkan API structures. It can be used to walk opaque lists of structure definitions.
// New Vulkan structures should probably include this at the front of them rather than
// explicitly including sType and pNext. For now, stick it here although we could
// promote it to the Vulkan header.
struct VkStructHeader
{
    VkStructureType         sType;
    const VkStructHeader*   pNext;
};

struct VkStructHeaderNonConst
{
    VkStructureType         sType;
    VkStructHeader*         pNext;
};

namespace utils
{

// =====================================================================================================================
// this function can be used to get the right externsion structure of specific type in case there are more than one
// externsion is supported
VK_INLINE const VkStructHeader * GetExtensionStructure(const VkStructHeader* pHeader, VkStructureType type)
{
    const VkStructHeader* pIter = pHeader;
    while(pIter != nullptr)
    {
        if (pIter->sType == type)
        {
            return pIter;
        }
        else
        {
            pIter = pIter->pNext;
        }
    }
    return nullptr;
}

// =====================================================================================================================
// Returns the number of indices of a particular index type that fit into a buffer of the given byte-size.
VK_INLINE uint32_t BufferSizeToIndexCount(Pal::IndexType indexType, VkDeviceSize bufferSize)
{
    static_assert((static_cast<int32_t>(Pal::IndexType::Idx8)  == 0) &&
                  (static_cast<int32_t>(Pal::IndexType::Idx16) == 1) &&
                  (static_cast<int32_t>(Pal::IndexType::Idx32) == 2),
                  "Pal::IndexType enum has changed, need to update this function");

    return static_cast<uint32_t>(bufferSize >> static_cast<uint32_t>(indexType));
}

// =====================================================================================================================
VK_INLINE void GetExecutableNameAndPath(wchar_t* pExecutableName, wchar_t* pExecutablePath)
{
    // Get the wchar_t executable name and path
    wchar_t  executableNameAndPathBuffer[PATH_MAX];

    wchar_t* pExecutablePtr;
    Pal::Result palResult = Util::GetExecutableName(&executableNameAndPathBuffer[0],
                                                    &pExecutablePtr,
                                                    sizeof(executableNameAndPathBuffer));
    VK_ASSERT(palResult == Pal::Result::Success);

    // Extract the executable path and add the null terminator
    const size_t executablePathLength = static_cast<size_t>(pExecutablePtr - executableNameAndPathBuffer);
    memcpy(pExecutablePath, executableNameAndPathBuffer, executablePathLength * sizeof(wchar_t));
    pExecutablePath[executablePathLength] = L'\0';

    // Copy the executable name and add the null terminator
    const size_t executableNameLength = wcslen(executableNameAndPathBuffer) - executablePathLength;
    memcpy(pExecutableName, pExecutablePtr, executableNameLength * sizeof(wchar_t));
    pExecutableName[executableNameLength] = L'\0';
}

// =====================================================================================================================
VK_INLINE int StrCmpCaseInsensitive(
    const char* a,
    const char* b)
{
    while (true)
    {
        const char ac = tolower(*a);
        const char bc = tolower(*b);

        if ((ac != bc) || (ac == '\0') || (bc == '\0'))
        {
            if (ac == bc)
            {
                return 0;
            }
            else
            {
                return (ac < bc) ? -1 : 1;
            }
        }

        a++;
        b++;
    }
}

// =====================================================================================================================
class IterateMask
{
public:
    VK_INLINE IterateMask(uint32_t mask) :
        m_index(0),
        m_bitsFound(0),
        m_mask(mask)
    {}

    VK_INLINE bool Iterate()
    {
        if (Util::BitMaskScanForward(&m_index, m_mask) == true)
        {
            m_mask ^= (1 << m_index);
            m_bitsFound++;
            return true;
        }
        return false;
    }

    VK_INLINE uint32_t Index() const
    {
        return m_index;
    }

    VK_INLINE uint32_t Count() const
    {
        return m_bitsFound;
    }

private:
    uint32_t    m_index;
    uint32_t    m_bitsFound;
    uint32_t    m_mask;
};

} // namespace utils

} // namespace vk

#endif /* __VK_RESULT_H__ */

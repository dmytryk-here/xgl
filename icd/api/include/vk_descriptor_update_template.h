/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2017-2018 Advanced Micro Devices, Inc. All Rights Reserved.
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
 * @file  vk_descriptor_update_template.h
 * @brief Functionality related to Vulkan descriptor update template objects.
 ***********************************************************************************************************************
 */

#ifndef __VK_DESCRIPTOR_UPDATE_TEMPLATE_H__
#define __VK_DESCRIPTOR_UPDATE_TEMPLATE_H__

#pragma once

#include "include/khronos/vulkan.h"
#include "include/vk_dispatch.h"
#include "include/vk_descriptor_set_layout.h"

namespace vk
{

class Device;

// =====================================================================================================================
// A Vulkan descriptor update template provides a way to update a descriptor set using with a pointer to user defined
// data, which describes the descriptor writes.
class DescriptorUpdateTemplate : public NonDispatchable<VkDescriptorUpdateTemplate, DescriptorUpdateTemplate>
{
public:
    static VkResult Create(
        const Device*                                   pDevice,
        const VkDescriptorUpdateTemplateCreateInfo*     pCreateInfo,
        const VkAllocationCallbacks*                    pAllocator,
        VkDescriptorUpdateTemplate*                     pDescriptorUpdateTemplate);

    VkResult Destroy(
        const Device*                pDevice,
        const VkAllocationCallbacks* pAllocator);

    void Update(
        const Device*   pDevice,
        uint32_t        deviceIdx,
        VkDescriptorSet descriptorSet,
        const void*     pData);

private:

    DescriptorUpdateTemplate(
        uint32_t                    numEntries);

    ~DescriptorUpdateTemplate();

    struct TemplateUpdateInfo;

    typedef void(*PfnUpdateEntry)(
        const Device*               pDevice,
        VkDescriptorSet             descriptorSet,
        uint32_t                    deviceIdx,
        const void*                 pDescriptorInfo,
        const TemplateUpdateInfo&   entry);

    struct TemplateUpdateInfo
    {
        PfnUpdateEntry  pFunc;
        size_t          srcOffset;
        size_t          srcStride;
        size_t          dstStaOffset;
        uint32_t        descriptorCount;
        uint32_t        dstBindStaDwArrayStride;
        uint32_t        dstBindDynDataDwArrayStride;
        size_t          dstDynOffset;
    };

    const TemplateUpdateInfo* GetEntries() const
    {
        return static_cast<const TemplateUpdateInfo*>(Util::VoidPtrInc(this, sizeof(*this)));
    }

    template <size_t imageDescSize, size_t samplerDescSize, size_t bufferDescSize>
    static PfnUpdateEntry GetUpdateEntryFunc(
        const Device*                           pDevice,
        VkDescriptorType                        descriptorType,
        const DescriptorSetLayout::BindingInfo& dstBinding);

    static PfnUpdateEntry GetUpdateEntryFunc(
        const Device*                           pDevice,
        VkDescriptorType                        descriptorType,
        const DescriptorSetLayout::BindingInfo& dstBinding);

    template <size_t imageDescSize, bool updateFmask>
    static void UpdateEntrySampledImage(
            const Device*               pDevice,
            VkDescriptorSet             descriptorSet,
            uint32_t                    deviceIdx,
            const void*                 pDescriptorInfo,
            const TemplateUpdateInfo&   entry);

    template <size_t samplerDescSize>
    static void UpdateEntrySampler(
            const Device*               pDevice,
            VkDescriptorSet             descriptorSet,
            uint32_t                    deviceIdx,
            const void*                 pDescriptorInfo,
            const TemplateUpdateInfo&   entry);

    template <VkDescriptorType descriptorType>
    static void UpdateEntryBuffer(
            const Device*               pDevice,
            VkDescriptorSet             descriptorSet,
            uint32_t                    deviceIdx,
            const void*                 pDescriptorInfo,
            const TemplateUpdateInfo&   entry);

    template <size_t bufferDescSize, VkDescriptorType descriptorType>
    static void UpdateEntryTexelBuffer(
            const Device*               pDevice,
            VkDescriptorSet             descriptorSet,
            uint32_t                    deviceIdx,
            const void*                 pDescriptorInfo,
            const TemplateUpdateInfo&   entry);

    template <size_t imageDescSize, size_t samplerDescSize, bool updateFmask, bool immutable>
    static void UpdateEntryCombinedImageSampler(
            const Device*               pDevice,
            VkDescriptorSet             descriptorSet,
            uint32_t                    deviceIdx,
            const void*                 pDescriptorInfo,
            const TemplateUpdateInfo&   entry);

    uint32_t                    m_numEntries;
};

namespace entry
{

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(
    VkDevice                                        device,
    VkDescriptorUpdateTemplate                      descriptorUpdateTemplate,
    const VkAllocationCallbacks*                    pAllocator);

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(
    VkDevice                                        device,
    VkDescriptorSet                                 descriptorSet,
    VkDescriptorUpdateTemplate                      descriptorUpdateTemplate,
    const void*                                     pData);

} // namespace entry

} // namespace vk

#endif /* __VK_DESCRIPTOR_UPDATE_TEMPLATE_H__ */

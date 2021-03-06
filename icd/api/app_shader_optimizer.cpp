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
**************************************************************************************************
* @file  app_shader_optimizer.cpp
* @brief Functions for tuning specific shader compile parameters for optimized code generation.
**************************************************************************************************
*/
#ifdef ICD_BUILD_APPPROFILE
#include "include/vk_device.h"
#include "include/vk_instance.h"
#include "include/vk_physical_device.h"
#include "include/vk_shader_code.h"
#include "include/vk_utils.h"

#include "include/app_shader_optimizer.h"

#include "palDbgPrint.h"
#include "palFile.h"

#if ICD_RUNTIME_APP_PROFILE
#include "utils/json_reader.h"
#endif

namespace vk
{

// =====================================================================================================================
ShaderOptimizer::ShaderOptimizer(
    Device*         pDevice,
    PhysicalDevice* pPhysicalDevice)
    :
    m_pDevice(pDevice),
    m_settings(pPhysicalDevice->GetRuntimeSettings())
{
#if PAL_ENABLE_PRINTS_ASSERTS
    m_printMutex.Init();
#endif
}

// =====================================================================================================================
void ShaderOptimizer::Init()
{
    BuildAppProfile();

#if ICD_RUNTIME_APP_PROFILE
    BuildRuntimeProfile();
#endif
}

// =====================================================================================================================
void ShaderOptimizer::ApplyProfileToShaderCreateInfo(
    const PipelineProfile&           profile,
    const PipelineOptimizerKey&      pipelineKey,
    ShaderStage                      shaderStage,
    PipelineShaderOptionsPtr         options)
{
    for (uint32_t entry = 0; entry < profile.entryCount; ++entry)
    {
        const PipelineProfileEntry& profileEntry = profile.entries[entry];

        if (ProfilePatternMatchesPipeline(profileEntry.pattern, pipelineKey))
        {
            const auto& shaderCreate = profileEntry.action.shaders[static_cast<uint32_t>(shaderStage)].shaderCreate;

            if (options.pOptions != nullptr)
            {
                if (shaderCreate.apply.vgprLimit)
                {
                    options.pOptions->vgprLimit = shaderCreate.tuningOptions.vgprLimit;
                }

                if (shaderCreate.apply.sgprLimit)
                {
                    options.pOptions->sgprLimit = shaderCreate.tuningOptions.sgprLimit;
                }

                if (shaderCreate.apply.maxThreadGroupsPerComputeUnit)
                {
                    options.pOptions->maxThreadGroupsPerComputeUnit =
                        shaderCreate.tuningOptions.maxThreadGroupsPerComputeUnit;
                }

                if (shaderCreate.apply.debugMode)
                {
                    options.pOptions->debugMode = true;
                }

                if (shaderCreate.apply.trapPresent)
                {
                    options.pOptions->trapPresent = true;
                }
            }

        }
    }
}

// =====================================================================================================================
void ShaderOptimizer::OverrideShaderCreateInfo(
    const PipelineOptimizerKey&        pipelineKey,
    ShaderStage                        shaderStage,
    PipelineShaderOptionsPtr           options)
{
    ApplyProfileToShaderCreateInfo(m_appProfile, pipelineKey, shaderStage, options);

#if ICD_RUNTIME_APP_PROFILE
    ApplyProfileToShaderCreateInfo(m_runtimeProfile, pipelineKey, shaderStage, options);
#endif
}

// =====================================================================================================================
void ShaderOptimizer::OverrideGraphicsPipelineCreateInfo(
    const PipelineOptimizerKey&       pipelineKey,
    VkShaderStageFlagBits             shaderStages,
    Pal::GraphicsPipelineCreateInfo*  pPalCreateInfo,
    Pal::DynamicGraphicsShaderInfos*  pGraphicsWaveLimitParams)
{
    ApplyProfileToGraphicsPipelineCreateInfo(
        m_appProfile, pipelineKey, shaderStages, pPalCreateInfo, pGraphicsWaveLimitParams);

#if ICD_RUNTIME_APP_PROFILE
    ApplyProfileToGraphicsPipelineCreateInfo(
        m_runtimeProfile, pipelineKey, shaderStages, pPalCreateInfo, pGraphicsWaveLimitParams);
#endif
}

// =====================================================================================================================
void ShaderOptimizer::OverrideComputePipelineCreateInfo(
    const PipelineOptimizerKey&      pipelineKey,
    Pal::DynamicComputeShaderInfo*   pDynamicCompueShaderInfo)
{
    ApplyProfileToComputePipelineCreateInfo(m_appProfile, pipelineKey, pDynamicCompueShaderInfo);

#if ICD_RUNTIME_APP_PROFILE
    ApplyProfileToComputePipelineCreateInfo(m_runtimeProfile, pipelineKey, pDynamicCompueShaderInfo);
#endif
}

// =====================================================================================================================
ShaderOptimizer::~ShaderOptimizer()
{

}

// =====================================================================================================================
void ShaderOptimizer::ApplyProfileToDynamicComputeShaderInfo(
    const ShaderProfileAction&     action,
    Pal::DynamicComputeShaderInfo* pComputeShaderInfo)
{

    if (action.dynamicShaderInfo.apply.maxThreadGroupsPerCu)
    {
        pComputeShaderInfo->maxThreadGroupsPerCu = action.dynamicShaderInfo.maxThreadGroupsPerCu;
    }
}

// =====================================================================================================================
void ShaderOptimizer::ApplyProfileToDynamicGraphicsShaderInfo(
    const ShaderProfileAction&      action,
    Pal::DynamicGraphicsShaderInfo* pGraphicsShaderInfo)
{

    if (action.dynamicShaderInfo.apply.cuEnableMask)
    {
        pGraphicsShaderInfo->cuEnableMask = action.dynamicShaderInfo.cuEnableMask;
    }
}

// =====================================================================================================================
void ShaderOptimizer::ApplyProfileToGraphicsPipelineCreateInfo(
    const PipelineProfile&            profile,
    const PipelineOptimizerKey&       pipelineKey,
    VkShaderStageFlagBits             shaderStages,
    Pal::GraphicsPipelineCreateInfo*  pPalCreateInfo,
    Pal::DynamicGraphicsShaderInfos*  pGraphicsShaderInfos)
{
    for (uint32_t entry = 0; entry < profile.entryCount; ++entry)
    {
        const PipelineProfileEntry& profileEntry = profile.entries[entry];

        if (ProfilePatternMatchesPipeline(profileEntry.pattern, pipelineKey))
        {
            // Apply parameters to DynamicGraphicsShaderInfo
            const auto& shaders = profileEntry.action.shaders;

            if (shaderStages & VK_SHADER_STAGE_VERTEX_BIT)
            {
                ApplyProfileToDynamicGraphicsShaderInfo(shaders[ShaderStageVertex], &pGraphicsShaderInfos->vs);
            }

            if (shaderStages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
            {
                ApplyProfileToDynamicGraphicsShaderInfo(shaders[ShaderStageTessControl], &pGraphicsShaderInfos->hs);
            }

            if (shaderStages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
            {
                ApplyProfileToDynamicGraphicsShaderInfo(shaders[ShaderStageTessEvaluation], &pGraphicsShaderInfos->ds);
            }

            if (shaderStages & VK_SHADER_STAGE_GEOMETRY_BIT)
            {
                ApplyProfileToDynamicGraphicsShaderInfo(shaders[ShaderStageGeometry], &pGraphicsShaderInfos->gs);
            }

            if (shaderStages & VK_SHADER_STAGE_FRAGMENT_BIT)
            {
                ApplyProfileToDynamicGraphicsShaderInfo(shaders[ShaderStageFragment], &pGraphicsShaderInfos->ps);
            }

            // Apply parameters to Pal::GraphicsPipelineCreateInfo
            const auto& createInfo = profileEntry.action.createInfo;

            if (createInfo.apply.lateAllocVsLimit)
            {
                pPalCreateInfo->useLateAllocVsLimit = true;
                pPalCreateInfo->lateAllocVsLimit    = createInfo.lateAllocVsLimit;
            }

#if PAL_ENABLE_PRINTS_ASSERTS
            if (m_settings.pipelineProfileDbgPrintProfileMatch)
            {
                PrintProfileEntryMatch(profile, entry, pipelineKey);
            }
#endif
        }
    }
}

// =====================================================================================================================
void ShaderOptimizer::ApplyProfileToComputePipelineCreateInfo(
    const PipelineProfile&           profile,
    const PipelineOptimizerKey&      pipelineKey,
    Pal::DynamicComputeShaderInfo*   pDynamicComputeShaderInfo)
{
    for (uint32_t entry = 0; entry < profile.entryCount; ++entry)
    {
        const PipelineProfileEntry& profileEntry = profile.entries[entry];

        if (ProfilePatternMatchesPipeline(profileEntry.pattern, pipelineKey))
        {
            ApplyProfileToDynamicComputeShaderInfo(
                profileEntry.action.shaders[ShaderStageCompute],
                pDynamicComputeShaderInfo);
        }
    }
}

// =====================================================================================================================
bool ShaderOptimizer::ProfilePatternMatchesPipeline(
    const PipelineProfilePattern& pattern,
    const PipelineOptimizerKey&   pipelineKey)
{
    if (pattern.match.always)
    {
        return true;
    }

    for (uint32_t stage = 0; stage < ShaderStageCount; ++stage)
    {
        const ShaderProfilePattern& shaderPattern = pattern.shaders[stage];

        if (shaderPattern.match.u32All != 0)
        {
            const ShaderOptimizerKey& shaderKey = pipelineKey.shaders[stage];

            // Test if this stage is active in the pipeline
            if (shaderPattern.match.stageActive && (shaderKey.codeSize == 0))
            {
                return false;
            }

            // Test if this stage is inactive in the pipeline
            if (shaderPattern.match.stageInactive && (shaderKey.codeSize != 0))
            {
                return false;
            }

            // Test if lower code hash word matches
            if (shaderPattern.match.codeHash &&
                (shaderPattern.codeHash.lower != shaderKey.codeHash.lower ||
                 shaderPattern.codeHash.upper != shaderKey.codeHash.upper))
            {
                return false;
            }

            // Test by code size (less than)
            if ((shaderPattern.match.codeSizeLessThan != 0) &&
                (shaderPattern.codeSizeLessThanValue >= shaderKey.codeSize))
            {
                return false;
            }
        }
    }

    return true;
}

// =====================================================================================================================
void ShaderOptimizer::BuildAppProfile()
{
    const AppProfile appProfile      = m_pDevice->GetAppProfile();
    const Pal::GfxIpLevel gfxIpLevel = m_pDevice->VkPhysicalDevice()->PalProperties().gfxLevel;

    // TODO: These need to be auto-generated from source JSON but for now we write profile programmatically
    memset(&m_appProfile, 0, sizeof(m_appProfile));

    // Early-out if the panel has dictated that we should ignore any active pipeline optimizations due to app profile
    if (m_settings.pipelineProfileIgnoresAppProfile)
    {
        return;
    }

}

#if PAL_ENABLE_PRINTS_ASSERTS
// =====================================================================================================================
void ShaderOptimizer::PrintProfileEntryMatch(
    const PipelineProfile&      profile,
    uint32_t                    index,
    const PipelineOptimizerKey& key)
{
    Util::MutexAuto lock(&m_printMutex);

    const char* pProfile = "Unknown profile";

    if (&profile == &m_appProfile)
    {
        pProfile = "Application";
    }
#if ICD_RUNTIME_APP_PROFILE
    else if (&profile == &m_runtimeProfile)
    {
        pProfile = "Runtime";
    }
#endif
    else
    {
        VK_NEVER_CALLED();
    }

    Util::DbgPrintf(Util::DbgPrintCatInfoMsg, Util::DbgPrintStyleDefault,
        "%s pipeline profile entry %u triggered for pipeline:", pProfile, index);

    for (uint32_t stageIdx = 0; stageIdx < ShaderStageCount; ++stageIdx)
    {
        const auto& shader = key.shaders[stageIdx];

        if (shader.codeSize != 0)
        {
            const char* pStage = "???";

            switch (stageIdx)
            {
            case ShaderStageVertex:
                pStage = "VS";
                break;
            case ShaderStageTessControl:
                pStage = "HS";
                break;
            case ShaderStageTessEvaluation:
                pStage = "DS";
                break;
            case ShaderStageGeometry:
                pStage = "GS";
                break;
            case ShaderStageFragment:
                pStage = "PS";
                break;
            case ShaderStageCompute:
                pStage = "CS";
                break;
            default:
                VK_NEVER_CALLED();
                break;
            }

            Util::DbgPrintf(Util::DbgPrintCatInfoMsg, Util::DbgPrintStyleDefault,
                "  %s: Hash: %016llX %016llX Size: %8zu", pStage, shader.codeHash.upper, shader.codeHash.lower, shader.codeSize);
        }
    }
}
#endif

#if ICD_RUNTIME_APP_PROFILE

// =====================================================================================================================
// Tests that each key in the given JSON object matches at least one of the keys in the array.
static bool CheckValidKeys(
    utils::Json* pObject,
    size_t       numKeys,
    const char** pKeys)
{
    bool success = true;

    if ((pObject != nullptr) && (pObject->type == utils::JsonValueType::Object))
    {
        for (utils::Json* pChild = pObject->pChild; success && (pChild != nullptr); pChild = pChild->pNext)
        {
            if (pChild->pKey != nullptr)
            {
                bool found = false;

                for (size_t i = 0; (found == false) && (i < numKeys); ++i)
                {
                    found |= (strcmp(pKeys[i], pChild->pKey) == 0);
                }

                success &= found;
            }
        }
    }
    else
    {
        success = false;
    }

    return success;
}

// =====================================================================================================================
static bool ParseJsonMinVgprOptions(
    utils::Json*               pJson,
    Scpc::ShaderTuningOptions* pTuningOptions)
{
    bool success = true;

    if (pJson->type == utils::JsonValueType::Number)
    {
        pTuningOptions->minVgprOptions.u32All = static_cast<uint32_t>(pJson->integerValue);
    }
    else if (pJson->type == utils::JsonValueType::Object)
    {
        static const char* ValidKeys[] =
        {
            "globalCodeMotionXform",
            "schedulerFavorsMinVpgrs",
            "regAllocFavorsMinVgprs",
            "enableMergeChaining",
            "peepholeOptimizations",
            "cubeCoordinates",
            "factorMadToCommonMul",
            "valueNumberOptimizations",
            "bulkCodeMotion"
        };

        success &= CheckValidKeys(pJson, VK_ARRAY_SIZE(ValidKeys), ValidKeys);

        utils::Json* pItem = nullptr;

        pTuningOptions->minVgprOptions.u32All = 0;

        if ((pItem = utils::JsonGetValue(pJson, "globalCodeMotionXform")) != nullptr)
        {
            pTuningOptions->minVgprOptions.globalCodeMotionXform = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "schedulerFavorsMinVpgrs")) != nullptr)
        {
            pTuningOptions->minVgprOptions.schedulerFavorsMinVpgrs = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "regAllocFavorsMinVgprs")) != nullptr)
        {
            pTuningOptions->minVgprOptions.regAllocFavorsMinVgprs = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "enableMergeChaining")) != nullptr)
        {
            pTuningOptions->minVgprOptions.enableMergeChaining = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "peepholeOptimizations")) != nullptr)
        {
            pTuningOptions->minVgprOptions.peepholeOptimizations = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "cubeCoordinates")) != nullptr)
        {
            pTuningOptions->minVgprOptions.cubeCoordinates = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "factorMadToCommonMul")) != nullptr)
        {
            pTuningOptions->minVgprOptions.factorMadToCommonMul = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "valueNumberOptimizations")) != nullptr)
        {
            pTuningOptions->minVgprOptions.valueNumberOptimizations = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "bulkCodeMotion")) != nullptr)
        {
            pTuningOptions->minVgprOptions.bulkCodeMotion = pItem->booleanValue;
        }
    }
    else
    {
        success = false;
    }

    return success;
}

// =====================================================================================================================
static bool ParseJsonOptStrategyFlags(
    utils::Json*               pJson,
    Scpc::ShaderTuningOptions* pTuningOptions)
{
    bool success = true;

    if (pJson->type == utils::JsonValueType::Number)
    {
        pTuningOptions->flags.u32All = static_cast<uint32_t>(pJson->integerValue);
    }
    else if (pJson->type == utils::JsonValueType::Object)
    {
        static const char* ValidKeys[] =
        {
            "minimizeMemoryFootprint",
            "minimizeVGprs",
            "groupScoring",
            "livenessScheduling",
            "rematerializeInstructions",
            "useMoreD16",
            "unsafeMadMix",
            "unsafeConvertToF16",
            "removeNullParameterExports",
            "aggressiveHoist",
            "enableXnackSupport",
            "useNonIeeeFpInstructions",
            "anisoControlFiltering",
            "appendBufPerWaveAtomics",
            "ignoreConservativeDepth",
            "disableIdentityFmaskGen",
            "disableExportGrouping",
            "enableF16OverflowClamping"
        };

        success &= CheckValidKeys(pJson, VK_ARRAY_SIZE(ValidKeys), ValidKeys);

        utils::Json* pItem = nullptr;

        pTuningOptions->flags.u32All = 0;

        if ((pItem = utils::JsonGetValue(pJson, "minimizeMemoryFootprint")) != nullptr)
        {
            pTuningOptions->flags.minimizeMemoryFootprint = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "minimizeVGprs")) != nullptr)
        {
            pTuningOptions->flags.minimizeVGprs = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "groupScoring")) != nullptr)
        {
            pTuningOptions->flags.groupScoring = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "livenessScheduling")) != nullptr)
        {
            pTuningOptions->flags.livenessScheduling = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "rematerializeInstructions")) != nullptr)
        {
            pTuningOptions->flags.rematerializeInstructions = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "useMoreD16")) != nullptr)
        {
            pTuningOptions->flags.useMoreD16 = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "unsafeMadMix")) != nullptr)
        {
            pTuningOptions->flags.unsafeMadMix = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "unsafeConvertToF16")) != nullptr)
        {
            pTuningOptions->flags.unsafeConvertToF16 = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "removeNullParameterExports")) != nullptr)
        {
            pTuningOptions->flags.removeNullParameterExports = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "aggressiveHoist")) != nullptr)
        {
            pTuningOptions->flags.aggressiveHoist = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "enableXnackSupport")) != nullptr)
        {
            pTuningOptions->flags.enableXnackSupport = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "useNonIeeeFpInstructions")) != nullptr)
        {
            pTuningOptions->flags.useNonIeeeFpInstructions = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "anisoControlFiltering")) != nullptr)
        {
            pTuningOptions->flags.anisoControlFiltering = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "appendBufPerWaveAtomics")) != nullptr)
        {
            pTuningOptions->flags.appendBufPerWaveAtomics = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "ignoreConservativeDepth")) != nullptr)
        {
            pTuningOptions->flags.ignoreConservativeDepth = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "disableIdentityFmaskGen")) != nullptr)
        {
            pTuningOptions->flags.disableIdentityFmaskGen = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "disableExportGrouping")) != nullptr)
        {
            pTuningOptions->flags.disableExportGrouping = pItem->booleanValue;
        }

        if ((pItem = utils::JsonGetValue(pJson, "enableF16OverflowClamping")) != nullptr)
        {
            pTuningOptions->flags.enableF16OverflowClamping = pItem->booleanValue;
        }
    }
    else
    {
        success = false;
    }

    return success;
}

// =====================================================================================================================
static void ParseDwordArray(
    utils::Json* pItem,
    uint32_t     maxCount,
    uint32_t     defaultValue,
    uint32_t*    pArray)
{
    for (uint32_t i = 0; i < maxCount; ++i)
    {
        utils::Json* pElement = utils::JsonArrayElement(pItem, i);

        if (pElement != nullptr)
        {
            pArray[i] = static_cast<uint32_t>(pElement->integerValue);
        }
        else
        {
            pArray[i] = defaultValue;
        }
    }
}

// =====================================================================================================================
static bool ParseJsonProfileActionShader(
    utils::Json*         pJson,
    ShaderStage          shaderStage,
    ShaderProfileAction* pActions)
{
    bool success = true;
    utils::Json* pItem = nullptr;

    static const char* ValidKeys[] =
    {
        "optStrategyFlags",
        "minVgprOptions",
        "vgprLimit",
        "sgprLimit",
        "ldsSpillLimitDwords",
        "maxArraySizeForFastDynamicIndexing",
        "userDataSpillThreshold",
        "maxThreadGroupsPerComputeUnit",
#if PAL_DEVELOPER_BUILD
        "scOptions",
        "scOptionsMask",
        "scSetOption",
#endif
        "maxWavesPerCu",
        "cuEnableMask",
        "maxThreadGroupsPerCu",
        "trapPresent",
        "debugMode"
    };

    success &= CheckValidKeys(pJson, VK_ARRAY_SIZE(ValidKeys), ValidKeys);

    if ((pItem = utils::JsonGetValue(pJson, "vgprLimit")) != nullptr)
    {
        pActions->shaderCreate.apply.vgprLimit         = true;
        pActions->shaderCreate.tuningOptions.vgprLimit = static_cast<uint32_t>(pItem->integerValue);
    }

    if ((pItem = utils::JsonGetValue(pJson, "sgprLimit")) != nullptr)
    {
        pActions->shaderCreate.apply.sgprLimit         = true;
        pActions->shaderCreate.tuningOptions.sgprLimit = static_cast<uint32_t>(pItem->integerValue);
    }

    if ((pItem = utils::JsonGetValue(pJson, "ldsSpillLimitDwords")) != nullptr)
    {
        pActions->shaderCreate.apply.ldsSpillLimitDwords         = true;
        pActions->shaderCreate.tuningOptions.ldsSpillLimitDwords = static_cast<uint32_t>(pItem->integerValue);
    }

    if ((pItem = utils::JsonGetValue(pJson, "maxArraySizeForFastDynamicIndexing")) != nullptr)
    {
        pActions->shaderCreate.apply.maxArraySizeForFastDynamicIndexing         = true;
        pActions->shaderCreate.tuningOptions.maxArraySizeForFastDynamicIndexing = static_cast<uint32_t>(pItem->integerValue);
    }

    if ((pItem = utils::JsonGetValue(pJson, "userDataSpillThreshold")) != nullptr)
    {
        pActions->shaderCreate.apply.userDataSpillThreshold         = true;
        pActions->shaderCreate.tuningOptions.userDataSpillThreshold = static_cast<uint32_t>(pItem->integerValue);
    }

    if ((pItem = utils::JsonGetValue(pJson, "maxThreadGroupsPerComputeUnit")) != nullptr)
    {
        pActions->shaderCreate.apply.maxThreadGroupsPerComputeUnit         = true;
        pActions->shaderCreate.tuningOptions.maxThreadGroupsPerComputeUnit = static_cast<uint32_t>(pItem->integerValue);
    }

    if ((pItem = utils::JsonGetValue(pJson, "trapPresent")) != nullptr)
    {
        if (pItem->integerValue != 0)
        {
            pActions->shaderCreate.apply.trapPresent = 1;
        }
    }

    if ((pItem = utils::JsonGetValue(pJson, "debugMode")) != nullptr)
    {
        if (pItem->integerValue != 0)
        {
            pActions->shaderCreate.apply.debugMode = 1;
        }
    }

    if ((pItem = utils::JsonGetValue(pJson, "maxWavesPerCu")) != nullptr)
    {
        pActions->dynamicShaderInfo.apply.maxWavesPerCu = true;
        pActions->dynamicShaderInfo.maxWavesPerCu       = static_cast<uint32_t>(pItem->integerValue);
    }

    if ((pItem = utils::JsonGetValue(pJson, "cuEnableMask")) != nullptr)
    {
        if (shaderStage != ShaderStageCompute)
        {
            pActions->dynamicShaderInfo.apply.cuEnableMask = true;
            pActions->dynamicShaderInfo.cuEnableMask       = static_cast<uint32_t>(pItem->integerValue);
        }
        else
        {
            success = false;
        }
    }

    if ((pItem = utils::JsonGetValue(pJson, "maxThreadGroupsPerCu")) != nullptr)
    {
        if (shaderStage == ShaderStageCompute)
        {
            pActions->dynamicShaderInfo.apply.maxThreadGroupsPerCu = true;
            pActions->dynamicShaderInfo.maxThreadGroupsPerCu       = static_cast<uint32_t>(pItem->integerValue);
        }
        else
        {
            success = false;
        }
    }

    return success;
}

// =====================================================================================================================
static bool ParseJsonProfileEntryAction(
    utils::Json*           pJson,
    PipelineProfileAction* pAction)
{
    bool success = true;

    static const char* ValidKeys[] =
    {
        "lateAllocVsLimit",
        "vs",
        "hs",
        "ds",
        "gs",
        "ps",
        "cs"
    };

    success &= CheckValidKeys(pJson, VK_ARRAY_SIZE(ValidKeys), ValidKeys);

    utils::Json* pItem;

    if ((pItem = utils::JsonGetValue(pJson, "lateAllocVsLimit")) != nullptr)
    {
        pAction->createInfo.apply.lateAllocVsLimit = true;
        pAction->createInfo.lateAllocVsLimit       = static_cast<uint32_t>(pItem->integerValue);
    }

    if ((pItem = utils::JsonGetValue(pJson, "vs")) != nullptr)
    {
        success &= ParseJsonProfileActionShader(pItem, ShaderStageVertex, &pAction->shaders[ShaderStageVertex]);
    }

    if ((pItem = utils::JsonGetValue(pJson, "hs")) != nullptr)
    {
        success &= ParseJsonProfileActionShader(pItem, ShaderStageTessControl,
            &pAction->shaders[ShaderStageTessControl]);
    }

    if ((pItem = utils::JsonGetValue(pJson, "ds")) != nullptr)
    {
        success &= ParseJsonProfileActionShader(pItem, ShaderStageTessEvaluation,
            &pAction->shaders[ShaderStageTessEvaluation]);
    }

    if ((pItem = utils::JsonGetValue(pJson, "gs")) != nullptr)
    {
        success &= ParseJsonProfileActionShader(pItem, ShaderStageGeometry, &pAction->shaders[ShaderStageGeometry]);
    }

    if ((pItem = utils::JsonGetValue(pJson, "ps")) != nullptr)
    {
        success &= ParseJsonProfileActionShader(pItem, ShaderStageFragment, &pAction->shaders[ShaderStageFragment]);
    }

    if ((pItem = utils::JsonGetValue(pJson, "cs")) != nullptr)
    {
        success &= ParseJsonProfileActionShader(pItem, ShaderStageCompute, &pAction->shaders[ShaderStageCompute]);
    }

    return success;
}

// =====================================================================================================================
static bool ParseJsonProfilePatternShader(
    utils::Json*          pJson,
    ShaderStage           shaderStage,
    ShaderProfilePattern* pPattern)
{
    bool success = true;

    static const char* ValidKeys[] =
    {
        "stageActive",
        "stageInactive",
        "codeHash",
        "codeSizeLessThan"
    };

    success &= CheckValidKeys(pJson, VK_ARRAY_SIZE(ValidKeys), ValidKeys);

    utils::Json* pItem = nullptr;

    if ((pItem = utils::JsonGetValue(pJson, "stageActive")) != nullptr)
    {
        pPattern->match.stageActive = pItem->booleanValue;
    }

    if ((pItem = utils::JsonGetValue(pJson, "stageInactive")) != nullptr)
    {
        pPattern->match.stageInactive = pItem->booleanValue;
    }

    // The hash is a 128-bit value interpreted from a JSON hex string.  It should be split by a space into two
    // 64-bit sections, e.g.: { "codeHash" : "0x1234567812345678 1234567812345678" }.
    if ((pItem = utils::JsonGetValue(pJson, "codeHash")) != nullptr)
    {
        char* pLower64 = nullptr;

        pPattern->match.codeHash = true;

        pPattern->codeHash.upper = strtoull(pItem->pStringValue, &pLower64, 16);
        pPattern->codeHash.lower = strtoull(pLower64, nullptr, 16);
    }

    if ((pItem = utils::JsonGetValue(pJson, "codeSizeLessThan")) != nullptr)
    {
        pPattern->match.codeSizeLessThan = true;

        pPattern->codeSizeLessThanValue  = static_cast<size_t>(pItem->integerValue);
    }

    return success;
}

// =====================================================================================================================
static bool ParseJsonProfileEntryPattern(
    utils::Json*            pJson,
    PipelineProfilePattern* pPattern)
{
    bool success = true;

    static const char* ValidKeys[] =
    {
        "always",
        "vs",
        "hs",
        "ds",
        "gs",
        "ps",
        "cs"
    };

    success &= CheckValidKeys(pJson, VK_ARRAY_SIZE(ValidKeys), ValidKeys);

    utils::Json* pItem = nullptr;

    if ((pItem = utils::JsonGetValue(pJson, "always")) != nullptr)
    {
        pPattern->match.always = pItem->booleanValue;
    }

    if ((pItem = utils::JsonGetValue(pJson, "vs")) != nullptr)
    {
        success &= ParseJsonProfilePatternShader(pItem, ShaderStageVertex, &pPattern->shaders[ShaderStageVertex]);
    }

    if ((pItem = utils::JsonGetValue(pJson, "hs")) != nullptr)
    {
        success &= ParseJsonProfilePatternShader(pItem, ShaderStageTessControl,
                                                 &pPattern->shaders[ShaderStageTessControl]);
    }

    if ((pItem = utils::JsonGetValue(pJson, "ds")) != nullptr)
    {
        success &= ParseJsonProfilePatternShader(pItem, ShaderStageTessEvaluation,
                                                 &pPattern->shaders[ShaderStageTessEvaluation]);
    }

    if ((pItem = utils::JsonGetValue(pJson, "gs")) != nullptr)
    {
        success &= ParseJsonProfilePatternShader(pItem, ShaderStageGeometry, &pPattern->shaders[ShaderStageGeometry]);
    }

    if ((pItem = utils::JsonGetValue(pJson, "ps")) != nullptr)
    {
        success &= ParseJsonProfilePatternShader(pItem, ShaderStageFragment, &pPattern->shaders[ShaderStageFragment]);
    }

    if ((pItem = utils::JsonGetValue(pJson, "cs")) != nullptr)
    {
        success &= ParseJsonProfilePatternShader(pItem, ShaderStageCompute, &pPattern->shaders[ShaderStageCompute]);
    }

    return success;
}

// =====================================================================================================================
static bool ParseJsonProfileEntry(
    utils::Json*          pPatterns,
    utils::Json*          pActions,
    utils::Json*          pEntry,
    PipelineProfileEntry* pProfileEntry)
{
    bool success = true;

    static const char* ValidKeys[] =
    {
        "pattern",
        "action"
    };

    success &= CheckValidKeys(pEntry, VK_ARRAY_SIZE(ValidKeys), ValidKeys);

    PipelineProfileEntry entry = {};

    utils::Json* pPattern = utils::JsonGetValue(pEntry, "pattern");

    if (pPattern != nullptr)
    {
        if (pPattern->type == utils::JsonValueType::String)
        {
            pPattern = (pPatterns != nullptr) ? utils::JsonGetValue(pPatterns, pPattern->pStringValue) : nullptr;
        }
    }

    if (pPattern != nullptr && pPattern->type != utils::JsonValueType::Object)
    {
        pPattern = nullptr;
    }

    utils::Json* pAction = utils::JsonGetValue(pEntry, "action");

    if (pAction != nullptr)
    {
        if (pAction->type == utils::JsonValueType::String)
        {
            pAction = (pActions != nullptr) ? utils::JsonGetValue(pActions, pAction->pStringValue) : nullptr;
        }
    }

    if (pAction != nullptr && pAction->type != utils::JsonValueType::Object)
    {
        pAction = nullptr;
    }

    if (pPattern != nullptr && pAction != nullptr)
    {
        success &= ParseJsonProfileEntryPattern(pPattern, &pProfileEntry->pattern);
        success &= ParseJsonProfileEntryAction(pAction, &pProfileEntry->action);
    }
    else
    {
        success = false;
    }

    return success;
}

// =====================================================================================================================
static bool ParseJsonProfile(
    utils::Json*     pJson,
    PipelineProfile* pProfile)
{
/*  Example of the run-time profile:
    {
      "entries": [
        {
          "pattern": {
            "always": false,
            "vs": {
              "stageActive": true,
              "codeHash": "0x0 0x7B9BFA968C24EB11"
            }
          },
          "action": {
            "lateAllocVsLimit": 1000000,
            "vs": {
              "maxThreadGroupsPerComputeUnit": 10
            }
          }
        }
      ]
    }
*/

    bool success = true;

    if (pJson != nullptr)
    {
        utils::Json* pEntries  = utils::JsonGetValue(pJson, "entries");
        utils::Json* pPatterns = utils::JsonGetValue(pJson, "patterns");
        utils::Json* pActions  = utils::JsonGetValue(pJson, "actions");

        if (pEntries != nullptr)
        {
            for (utils::Json* pEntry = pEntries->pChild; (pEntry != nullptr) && success; pEntry = pEntry->pNext)
            {
                if (pProfile->entryCount < MaxPipelineProfileEntries)
                {
                    success &= ParseJsonProfileEntry(pPatterns, pActions, pEntry, &pProfile->entries[pProfile->entryCount++]);
                }
                else
                {
                    success = false;
                }
            }
        }
    }
    else
    {
        success = false;
    }

    return success;
}

// =====================================================================================================================
void ShaderOptimizer::RuntimeProfileParseError()
{
    VK_ASSERT(false && "Failed to parse runtime pipeline profile file");

    // Trigger an infinite loop if the panel setting is set to notify that a profile parsing failure has occurred
    // on release driver builds where asserts are not compiled in.
    if (m_settings.pipelineProfileHaltOnParseFailure)
    {
        while (true)
        {
        }
    }
}

// =====================================================================================================================
void ShaderOptimizer::BuildRuntimeProfile()
{
    memset(&m_runtimeProfile, 0, sizeof(m_runtimeProfile));

    utils::JsonSettings jsonSettings = utils::JsonMakeInstanceSettings(m_pDevice->VkInstance());
    utils::Json* pJson               = nullptr;

    if (m_settings.pipelineProfileRuntimeFile[0] != '\0')
    {
        Util::File jsonFile;

        if (jsonFile.Open(m_settings.pipelineProfileRuntimeFile, Util::FileAccessRead) == Pal::Result::Success)
        {
            size_t size = jsonFile.GetFileSize(m_settings.pipelineProfileRuntimeFile);

            void* pJsonBuffer = m_pDevice->VkInstance()->AllocMem(size, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

            if (pJsonBuffer != nullptr)
            {
                size_t bytesRead = 0;

                jsonFile.Read(pJsonBuffer, size, &bytesRead);

                if (bytesRead > 0)
                {
                    pJson = utils::JsonParse(jsonSettings, pJsonBuffer, bytesRead);

                    if (pJson != nullptr)
                    {
                        bool success = ParseJsonProfile(pJson, &m_runtimeProfile);

                        if (success == false)
                        {
                            // Failed to parse some part of the profile (e.g. unsupported/missing key name)
                            RuntimeProfileParseError();
                        }

                        utils::JsonDestroy(jsonSettings, pJson);
                    }
                    else
                    {
                        // Failed to parse JSON file entirely
                        RuntimeProfileParseError();
                    }
                }

                m_pDevice->VkInstance()->FreeMem(pJsonBuffer);
            }

            jsonFile.Close();
        }
    }
}
#endif // ICD_RUNTIME_APP_PROFILE

};

#endif // ICD_BUILD_APPPROFILE

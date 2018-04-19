/*     Copyright 2015-2018 Egor Yusov
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
*
*  In no event and under no legal theory, whether in tort (including negligence),
*  contract, or otherwise, unless required by applicable law (such as deliberate
*  and grossly negligent acts) or agreed to in writing, shall any Contributor be
*  liable for any damages, including any direct, indirect, special, incidental,
*  or consequential damages of any character arising as a result of this License or
*  out of the use or inability to use the software (including but not limited to damages
*  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
*  all other commercial damages or losses), even if such Contributor has been advised
*  of the possibility of such damages.
*/
#include <sstream>

#include "VulkanUtilities/VulkanCommandBuffer.h"

namespace VulkanUtilities
{

static VkPipelineStageFlags PipelineStageFromAccessFlags(VkAccessFlags AccessFlags)
{
    // 6.1.3
    VkPipelineStageFlags Stages = 0;

    while(AccessFlags != 0)
    {
        VkAccessFlagBits AccessFlag = static_cast<VkAccessFlagBits>( AccessFlags & (~(AccessFlags-1)));
        VERIFY_EXPR( AccessFlag != 0 && (AccessFlag & (AccessFlag-1)) == 0 );
        AccessFlags &= ~AccessFlag;

        static constexpr VkPipelineStageFlags ALL_GRAPHICS_SHADER_STAGES_BITS =
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT                  |
            VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT    |
            VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
            VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT                |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        // An application MUST NOT specify an access flag in a synchronization command if it does not include a 
        // pipeline stage in the corresponding stage mask that is able to perform accesses of that type.
        // A table that lists, for each access flag, which pipeline stages can perform that type of access is given in 6.1.3.
        switch(AccessFlag)
        {
            // Read access to an indirect command structure read as part of an indirect drawing or dispatch command
            case VK_ACCESS_INDIRECT_COMMAND_READ_BIT:
                Stages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
            break;

            // Read access to an index buffer as part of an indexed drawing command, bound by vkCmdBindIndexBuffer
            case VK_ACCESS_INDEX_READ_BIT:
                Stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            break;

            // Read access to a vertex buffer as part of a drawing command, bound by vkCmdBindVertexBuffers
            case VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:
                Stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            break;

            // Read access to a uniform buffer
            case VK_ACCESS_UNIFORM_READ_BIT:
                Stages |= ALL_GRAPHICS_SHADER_STAGES_BITS | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            break;

            // Read access to an input attachment within a render pass during fragment shading
            case VK_ACCESS_INPUT_ATTACHMENT_READ_BIT:
                Stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;

            // Read access to a storage buffer, uniform texel buffer, storage texel buffer, sampled image, or storage image
            case VK_ACCESS_SHADER_READ_BIT:
                Stages |= ALL_GRAPHICS_SHADER_STAGES_BITS | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            break;

            // Write access to a storage buffer, storage texel buffer, or storage image
            case VK_ACCESS_SHADER_WRITE_BIT:
                Stages |= ALL_GRAPHICS_SHADER_STAGES_BITS | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            break;

            // Read access to a color attachment, such as via blending, logic operations, or via certain subpass load operations
            case VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:
                Stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;

            // Write access to a color or resolve attachment during a render pass or via certain subpass load and store operations
            case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
                Stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;

            // Read access to a depth/stencil attachment, via depth or stencil operations or via certain subpass load operations
            case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT:
                Stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;

            // Write access to a depth/stencil attachment, via depth or stencil operations or via certain subpass load and store operations
            case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
                Stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;

            // Read access to an image or buffer in a copy operation
            case VK_ACCESS_TRANSFER_READ_BIT:
                Stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

            // Write access to an image or buffer in a clear or copy operation
            case VK_ACCESS_TRANSFER_WRITE_BIT:
                Stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

            // Read access by a host operation. Accesses of this type are not performed through a resource, but directly on memory
            case VK_ACCESS_HOST_READ_BIT:
                Stages |= VK_PIPELINE_STAGE_HOST_BIT;
            break;

            // Write access by a host operation. Accesses of this type are not performed through a resource, but directly on memory
            case VK_ACCESS_HOST_WRITE_BIT:
                Stages |= VK_PIPELINE_STAGE_HOST_BIT;
            break;

            // Read access via non-specific entities. When included in a destination access mask, makes all available writes 
            // visible to all future read accesses on entities known to the Vulkan device
            case VK_ACCESS_MEMORY_READ_BIT:
            break;

            // Write access via non-specific entities. hen included in a source access mask, all writes that are performed 
            // by entities known to the Vulkan device are made available. When included in a destination access mask, makes 
            // all available writes visible to all future write accesses on entities known to the Vulkan device.
            case VK_ACCESS_MEMORY_WRITE_BIT:
            break;

            default:
                UNEXPECTED("Unknown memory access flag");
        }
    }
    return Stages;
}

void VulkanCommandBuffer::TransitionImageLayout(VkCommandBuffer CmdBuffer,
                                                VkImage Image,
                                                VkImageAspectFlags AspectMask, 
                                                VkImageLayout OldLayout,
                                                VkImageLayout NewLayout,
                                                const VkImageSubresourceRange& SubresRange,
                                                VkPipelineStageFlags SrcStages, 
                                                VkPipelineStageFlags DestStages)
{
    VERIFY_EXPR(CmdBuffer != VK_NULL_HANDLE);

    VkImageMemoryBarrier ImgBarrier = {};
    ImgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ImgBarrier.pNext = nullptr;
    ImgBarrier.srcAccessMask = 0;
    ImgBarrier.dstAccessMask = 0;
    ImgBarrier.oldLayout = OldLayout;
    ImgBarrier.newLayout = NewLayout;
    ImgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // source queue family for a queue family ownership transfer.
    ImgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // destination queue family for a queue family ownership transfer.
    ImgBarrier.image = Image;
    ImgBarrier.subresourceRange.aspectMask = AspectMask;
    ImgBarrier.subresourceRange = SubresRange;

    switch (OldLayout) 
    {
        // does not support device access. This layout must only be used as the initialLayout member 
        // of VkImageCreateInfo or VkAttachmentDescription, or as the oldLayout in an image transition. 
        // When transitioning out of this layout, the contents of the memory are not guaranteed to be preserved (11.4)
        case VK_IMAGE_LAYOUT_UNDEFINED:
        break;

        // supports all types of device access
        case VK_IMAGE_LAYOUT_GENERAL:
            UNEXPECTED("General layout is not recommended");
        break;

        // must only be used as a color or resolve attachment in a VkFramebuffer (11.4)
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            ImgBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

        // must only be used as a depth/stencil attachment in a VkFramebuffer (11.4)
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            ImgBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

        // must only be used as a read-only depth/stencil attachment in a VkFramebuffer and/or as a read-only image in a shader (11.4)
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            ImgBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        break;

        // must only be used as a read-only image in a shader (which can be read as a sampled image, 
        // combined image/sampler and/or input attachment) (11.4)
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            ImgBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;

        //  must only be used as a source image of a transfer command (11.4)
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            ImgBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

        // must only be used as a destination image of a transfer command (11.4)
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            ImgBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

        // does not support device access. This layout must only be used as the initialLayout member
        // of VkImageCreateInfo or VkAttachmentDescription, or as the oldLayout in an image transition.
        // When transitioning out of this layout, the contents of the memory are preserved. (11.4)
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            ImgBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
            ImgBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
            ImgBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            ImgBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        break;

        default:
            UNEXPECTED("Unexpected image layout");
        break;
    }


    switch (NewLayout) 
    {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            UNEXPECTED("The new layout used in a transition must not be VK_IMAGE_LAYOUT_UNDEFINED. "
                       "This layout must only be used as the initialLayout member of VkImageCreateInfo " 
                       "or VkAttachmentDescription, or as the oldLayout in an image transition. (11.4)");
        break;

        case VK_IMAGE_LAYOUT_GENERAL:
            UNEXPECTED("General layout is not recommended due to inefficiency");
        break;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            ImgBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            ImgBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            ImgBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            ImgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            ImgBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            ImgBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

        case VK_IMAGE_LAYOUT_PREINITIALIZED:
           UNEXPECTED("The new layout used in a transition must not be VK_IMAGE_LAYOUT_PREINITIALIZED. "
                       "This layout must only be used as the initialLayout member of VkImageCreateInfo " 
                       "or VkAttachmentDescription, or as the oldLayout in an image transition. (11.4)");
        break;

        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
            ImgBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
            ImgBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        break;

        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            ImgBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        break;

        default:
            UNEXPECTED("Unexpected image layout");
        break;
    }

    if(SrcStages == 0)
        SrcStages = PipelineStageFromAccessFlags(ImgBarrier.srcAccessMask);

    if (DestStages == 0)
        DestStages = PipelineStageFromAccessFlags(ImgBarrier.dstAccessMask);

    vkCmdPipelineBarrier(CmdBuffer,
        SrcStages, 
        DestStages,
        0, // a bitmask specifying how execution and memory dependencies are formed
        0,       // memoryBarrierCount
        nullptr, // pMemoryBarriers
        0,       // bufferMemoryBarrierCount
        nullptr, // pBufferMemoryBarriers
        1,
        &ImgBarrier);
}

void VulkanCommandBuffer::FlushBarriers()
{

}

}
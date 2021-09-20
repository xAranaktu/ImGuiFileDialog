// Dear ImGui: standalone example application for Glfw + Vulkan
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

// Important note to the reader who wish to integrate imgui_impl_vulkan.cpp/.h in their own engine/app.
// - Common ImGui_ImplVulkan_XXX functions and structures are used to interface with imgui_impl_vulkan.cpp/.h.
//   You will use those if you want to use this rendering backend in your engine/app.
// - Helper ImGui_ImplVulkanH_XXX functions and structures are only used by this example (main.cpp) and by
//   the backend itself (imgui_impl_vulkan.cpp), but should PROBABLY NOT be used by your own engine/app code.
// Read comments in imgui_impl_vulkan.h.

#include <3rdparty/imgui/imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <3rdparty/imgui/imgui_internal.h>
#include <3rdparty/imgui/backends/imgui_impl_glfw.h>
#include "imgui_impl_vulkan_user_texture.h"

#ifndef USE_THUMBNAILS
#define STB_IMAGE_IMPLEMENTATION
#endif
#include <3rdparty/stb/stb_image.h>

#include <ImGuiFileDialog/ImGuiFileDialog.h>

#include <stdio.h>          // printf, fprintf
#include <stdlib.h>         // abort
#include <string>
#include <sstream>
#include <fstream>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "CustomFont.cpp"

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

//#define IMGUI_UNLIMITED_FRAME_RATE
#ifdef _DEBUG
#define IMGUI_VULKAN_DEBUG_REPORT
#endif

static VkAllocationCallbacks*   g_Allocator = NULL;
static VkInstance               g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
static VkDevice                 g_Device = VK_NULL_HANDLE;
static uint32_t                 g_QueueFamily = (uint32_t)-1;
static VkQueue                  g_Queue = VK_NULL_HANDLE;
static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

static ImGui_ImplVulkanH_Window g_MainWindowData;
static int                      g_MinImageCount = 2;
static bool                     g_SwapChainRebuild = false;

static void check_vk_result(VkResult err)
{
    if (err == 0)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

#ifdef IMGUI_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
    (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix; // Unused arguments
    fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
    return VK_FALSE;
}
#endif // IMGUI_VULKAN_DEBUG_REPORT

static void SetupVulkan(const char** extensions, uint32_t extensions_count)
{
    VkResult err;

    // Create Vulkan Instance
    {
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.enabledExtensionCount = extensions_count;
        create_info.ppEnabledExtensionNames = extensions;
#ifdef IMGUI_VULKAN_DEBUG_REPORT
        // Enabling validation layers
        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;

        // Enable debug report extension (we need additional storage, so we duplicate the user array to add our new extension to it)
        const char** extensions_ext = (const char**)malloc(sizeof(const char*) * (extensions_count + 1));
        memcpy(extensions_ext, extensions, extensions_count * sizeof(const char*));
        extensions_ext[extensions_count] = "VK_EXT_debug_report";
        create_info.enabledExtensionCount = extensions_count + 1;
        create_info.ppEnabledExtensionNames = extensions_ext;

        // validation features
        VkValidationFeatureEnableEXT enables[] = {
            VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
            VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
            VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
            VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT 
        };
        VkValidationFeaturesEXT features = {};
        features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = enables;
        create_info.pNext = &features;

        // Create Vulkan Instance
        err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
        check_vk_result(err);
        free(extensions_ext);

        // Get the function pointer (required for any extensions)
        auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
        IM_ASSERT(vkCreateDebugReportCallbackEXT != NULL);

        // Setup the debug report callback
        VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
        debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debug_report_ci.pfnCallback = debug_report;
        debug_report_ci.pUserData = NULL;
        err = vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
        check_vk_result(err);
#else
        // Create Vulkan Instance without any debug feature
        err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
        check_vk_result(err);
        IM_UNUSED(g_DebugReport);
#endif
    }

    // Select GPU
    {
        uint32_t gpu_count;
        err = vkEnumeratePhysicalDevices(g_Instance, &gpu_count, NULL);
        check_vk_result(err);
        IM_ASSERT(gpu_count > 0);

        VkPhysicalDevice* gpus = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * gpu_count);
        err = vkEnumeratePhysicalDevices(g_Instance, &gpu_count, gpus);
        check_vk_result(err);

        // If a number >1 of GPUs got reported, you should find the best fit GPU for your purpose
        // e.g. VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU if available, or with the greatest memory available, etc.
        // for sake of simplicity we'll just take the first one, assuming it has a graphics queue family.
        g_PhysicalDevice = gpus[0];
        free(gpus);
    }

    // Select graphics queue family
    {
        uint32_t count;
        vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, NULL);
        VkQueueFamilyProperties* queues = (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * count);
        vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, queues);
        for (uint32_t i = 0; i < count; i++)
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                g_QueueFamily = i;
                break;
            }
        free(queues);
        IM_ASSERT(g_QueueFamily != (uint32_t)-1);
    }

    // Create Logical Device (with 1 queue)
    {
        int device_extension_count = 1;
        const char* device_extensions[] = { "VK_KHR_swapchain" };
        const float queue_priority[] = { 1.0f };
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = g_QueueFamily;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;
        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = device_extension_count;
        create_info.ppEnabledExtensionNames = device_extensions;
        err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
        check_vk_result(err);
        vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
    }

    // Create Descriptor Pool
    {
        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
        check_vk_result(err);
    }
}

// All the ImGui_ImplVulkanH_XXX structures/functions are optional helpers used by the demo.
// Your real engine/app may not use them.
static void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)
{
    wd->Surface = surface;

    // Check for WSI support
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, wd->Surface, &res);
    if (res != VK_TRUE)
    {
        fprintf(stderr, "Error no WSI support on physical device 0\n");
        exit(-1);
    }

    // Select Surface Format
    const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

    // Select Present Mode
#ifdef IMGUI_UNLIMITED_FRAME_RATE
    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
#else
    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));
    //printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

    // Create SwapChain, RenderPass, Framebuffer, etc.
    IM_ASSERT(g_MinImageCount >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
}

static void CleanupVulkan()
{
    vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef IMGUI_VULKAN_DEBUG_REPORT
    // Remove the debug report callback
    auto vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
    vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // IMGUI_VULKAN_DEBUG_REPORT

    vkDestroyDevice(g_Device, g_Allocator);
    vkDestroyInstance(g_Instance, g_Allocator);
}

static void CleanupVulkanWindow()
{
    ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);
}

static void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
{
    VkResult err;

    VkSemaphore image_acquired_semaphore  = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
    {
        g_SwapChainRebuild = true;
        return;
    }
    check_vk_result(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    {
        err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);    // wait indefinitely instead of periodically checking
        check_vk_result(err);

        err = vkResetFences(g_Device, 1, &fd->Fence);
        check_vk_result(err);
    }
    {
        err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        check_vk_result(err);
    }
    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = wd->RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = wd->Width;
        info.renderArea.extent.height = wd->Height;
        info.clearValueCount = 1;
        info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Record dear imgui primitives into command buffer
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    // Submit command buffer
    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
        err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
        check_vk_result(err);
    }
}

static void FramePresent(ImGui_ImplVulkanH_Window* wd)
{
    if (g_SwapChainRebuild)
        return;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult err = vkQueuePresentKHR(g_Queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
    {
        g_SwapChainRebuild = true;
        return;
    }
    check_vk_result(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount; // Now we can use the next set of semaphores
}

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

struct VulkanImageObject
{
    VkDeviceMemory      imgMem = VK_NULL_HANDLE;
    VkImage             img = VK_NULL_HANDLE;
    VkDeviceMemory      bufMem = VK_NULL_HANDLE;
    VkBuffer            buf = VK_NULL_HANDLE;
    VkSampler           sam = VK_NULL_HANDLE;
    VkImageView         view = VK_NULL_HANDLE;
    VkDescriptorSet     descriptor = VK_NULL_HANDLE;
};

static VkCommandBuffer beginSingleTimeCommands(ImGui_ImplVulkan_InitInfo* v, VkCommandPool commandPool)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(v->Device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

static void endSingleTimeCommands(ImGui_ImplVulkan_InitInfo* v, VkCommandPool commandPool, VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(v->Queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(v->Queue);

    vkFreeCommandBuffers(v->Device, commandPool, 1, &commandBuffer);
}

static void DestroyTexture(ImGui_ImplVulkan_InitInfo* v, VulkanImageObject *image_object)
{
    if (image_object)
    {
        if (image_object->buf)
        {
            vkDestroyBuffer(v->Device, image_object->buf, v->Allocator);
            image_object->buf = VK_NULL_HANDLE;
        }
        if (image_object->bufMem)
        {
            vkFreeMemory(v->Device, image_object->bufMem, v->Allocator);
            image_object->bufMem = VK_NULL_HANDLE;
        }

        if (image_object->view)
        {
            vkDestroyImageView(v->Device, image_object->view, v->Allocator);
            image_object->view = VK_NULL_HANDLE;
        }
        if (image_object->img)
        {
            vkDestroyImage(v->Device, image_object->img, v->Allocator);
            image_object->img = VK_NULL_HANDLE;
        }
        if (image_object->imgMem)
        {
            vkFreeMemory(v->Device, image_object->imgMem, v->Allocator);
            image_object->imgMem = VK_NULL_HANDLE;
        }
        if (image_object->sam)
        {
            vkDestroySampler(v->Device, image_object->sam, v->Allocator);
            image_object->sam = VK_NULL_HANDLE;
        }

        if (image_object->descriptor)
        {
            ImGui_ImplVulkanH_Destroy_UserTexture_Descriptor(&image_object->descriptor);
            image_object->descriptor = VK_NULL_HANDLE;
        }
    }
}
static std::shared_ptr<VulkanImageObject> CreateTextureFromBuffer(ImGui_ImplVulkan_InitInfo* v, VkCommandBuffer command_buffer, uint8_t *buffer, int w, int h, int n)
{
    std::shared_ptr<VulkanImageObject> res = std::shared_ptr<VulkanImageObject>(new VulkanImageObject,
        [v](VulkanImageObject* obj)
        {
            DestroyTexture(v, obj);
            delete obj;
        }
    );

    VkResult err;

    size_t buffer_size = sizeof(char) * n * w * h;

    // Create the Image:
    {
        VkImageCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent.width = w;
        info.extent.height = h;
        info.extent.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        err = vkCreateImage(v->Device, &info, v->Allocator, &res->img);
        check_vk_result(err);
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(v->Device, res->img, &req);
        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = req.size;
        alloc_info.memoryTypeIndex = ImGui_ImplVulkan_MemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, req.memoryTypeBits);
        err = vkAllocateMemory(v->Device, &alloc_info, v->Allocator, &res->imgMem);
        check_vk_result(err);
        err = vkBindImageMemory(v->Device, res->img, res->imgMem, 0);
        check_vk_result(err);
    }

    // Create the Image Sampler :
    {
        VkSamplerCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.minLod = -1000;
        info.maxLod = 1000;
        info.maxAnisotropy = 1.0f;
        VkResult err = vkCreateSampler(v->Device, &info, v->Allocator, &res->sam);
        check_vk_result(err);
    }

    // Create the Image View:
    {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = res->img;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;
        err = vkCreateImageView(v->Device, &info, v->Allocator, &res->view);
        check_vk_result(err);
    }

    // create the descriptor. will be put in ImTextureID
    res->descriptor = ImGui_ImplVulkanH_Create_UserTexture_Descriptor(res->sam, res->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Create the Upload Buffer:
    {
        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = buffer_size;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        err = vkCreateBuffer(v->Device, &buffer_info, v->Allocator, &res->buf);
        check_vk_result(err);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(v->Device, res->buf, &req);
        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = req.size;
        alloc_info.memoryTypeIndex = ImGui_ImplVulkan_MemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, req.memoryTypeBits);
        err = vkAllocateMemory(v->Device, &alloc_info, v->Allocator, &res->bufMem);
        check_vk_result(err);
        err = vkBindBufferMemory(v->Device, res->buf, res->bufMem, 0);
        check_vk_result(err);
    }

    // Upload to Buffer:
    {
        char* map = NULL;
        err = vkMapMemory(v->Device, res->bufMem, 0, buffer_size, 0, (void**)(&map));
        check_vk_result(err);
        memcpy(map, buffer, buffer_size);
        VkMappedMemoryRange range[1] = {};
        range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range[0].memory = res->bufMem;
        range[0].size = buffer_size;
        err = vkFlushMappedMemoryRanges(v->Device, 1, range);
        if (err != VK_SUCCESS)
            printf("vkFlushMappedMemoryRanges issue");
        check_vk_result(err);
        vkUnmapMemory(v->Device, res->bufMem);
    }

    // Copy to Image:
    {
        VkImageMemoryBarrier copy_barrier[1] = {};
        copy_barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        copy_barrier[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        copy_barrier[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        copy_barrier[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copy_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        copy_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        copy_barrier[0].image = res->img;
        copy_barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_barrier[0].subresourceRange.levelCount = 1;
        copy_barrier[0].subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, copy_barrier);

        VkBufferImageCopy region = {};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = w;
        region.imageExtent.height = h;
        region.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(command_buffer, res->buf, res->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier use_barrier[1] = {};
        use_barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        use_barrier[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        use_barrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        use_barrier[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        use_barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        use_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        use_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        use_barrier[0].image = res->img;
        use_barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        use_barrier[0].subresourceRange.levelCount = 1;
        use_barrier[0].subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, use_barrier);
    }

    err = vkDeviceWaitIdle(v->Device);
    check_vk_result(err);

    return res;
}

static std::shared_ptr<VulkanImageObject> CreateTextureFromFile(ImGui_ImplVulkan_InitInfo* v, VkCommandBuffer command_buffer, const char* inFile, VkDescriptorSet *vOriginal = nullptr)
{
    std::shared_ptr<VulkanImageObject> res = nullptr;

    printf("file to load : %s\n", inFile);
    
    int w, h, chans;
    unsigned char* imgDatas = stbi_load(inFile, &w, &h, &chans, STBI_rgb_alpha);
    if (imgDatas && w && h)
    {
        res = CreateTextureFromBuffer(v, command_buffer, imgDatas, w, h, 4);

        stbi_image_free(imgDatas);
    }

    return res;
}

static bool canValidateDialog = false;

inline void InfosPane(const char* vFilter, IGFDUserDatas vUserDatas, bool* vCantContinue) // if vCantContinue is false, the user cant validate the dialog
{
    ImGui::TextColored(ImVec4(0, 1, 1, 1), "Infos Pane");

    ImGui::Text("Selected Filter : %s", vFilter);

    const char* userDatas = (const char*)vUserDatas;
    if (userDatas)
        ImGui::Text("User Datas : %s", userDatas);

    ImGui::Checkbox("if not checked you cant validate the dialog", &canValidateDialog);

    if (vCantContinue)
        *vCantContinue = canValidateDialog;
}

inline bool RadioButtonLabeled(const char* label, const char* help, bool active, bool disabled)
{
    using namespace ImGui;

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    float w = CalcItemWidth();
    if (w == window->ItemWidthDefault)	w = 0.0f; // no push item width
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, nullptr, true);
    ImVec2 bb_size = ImVec2(style.FramePadding.x * 2 - 1, style.FramePadding.y * 2 - 1) + label_size;
    bb_size.x = ImMax(w, bb_size.x);

    const ImRect check_bb(
        window->DC.CursorPos,
        window->DC.CursorPos + bb_size);
    ItemSize(check_bb, style.FramePadding.y);

    if (!ItemAdd(check_bb, id))
        return false;

    // check
    bool pressed = false;
    if (!disabled)
    {
        bool hovered, held;
        pressed = ButtonBehavior(check_bb, id, &hovered, &held);

        window->DrawList->AddRectFilled(check_bb.Min, check_bb.Max, GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), style.FrameRounding);
        if (active)
        {
            const ImU32 col = GetColorU32((hovered && held) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            window->DrawList->AddRectFilled(check_bb.Min, check_bb.Max, col, style.FrameRounding);
        }
    }

    // circle shadow + bg
    if (style.FrameBorderSize > 0.0f)
    {
        window->DrawList->AddRect(check_bb.Min + ImVec2(1, 1), check_bb.Max, GetColorU32(ImGuiCol_BorderShadow), style.FrameRounding);
        window->DrawList->AddRect(check_bb.Min, check_bb.Max, GetColorU32(ImGuiCol_Border), style.FrameRounding);
    }

    if (label_size.x > 0.0f)
    {
        RenderText(check_bb.GetCenter() - label_size * 0.5f, label);
    }

    if (help && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", help);

    return pressed;
}

template<typename T>
inline bool RadioButtonLabeled_BitWize(
    const char* vLabel, const char* vHelp, T* vContainer, T vFlag,
    bool vOneOrZeroAtTime = false, //only one selected at a time
    bool vAlwaysOne = true, // radio behavior, always one selected
    T vFlagsToTakeIntoAccount = (T)0,
    bool vDisableSelection = false,
    ImFont* vLabelFont = nullptr) // radio witl use only theses flags
{
    bool selected = (*vContainer) & vFlag;
    const bool res = RadioButtonLabeled(vLabel, vHelp, selected, vDisableSelection);
    if (res) {
        if (!selected) {
            if (vOneOrZeroAtTime) {
                if (vFlagsToTakeIntoAccount) {
                    if (vFlag & vFlagsToTakeIntoAccount) {
                        *vContainer = (T)(*vContainer & ~vFlagsToTakeIntoAccount); // remove these flags
                        *vContainer = (T)(*vContainer | vFlag); // add
                    }
                }
                else *vContainer = vFlag; // set
            }
            else {
                if (vFlagsToTakeIntoAccount) {
                    if (vFlag & vFlagsToTakeIntoAccount) {
                        *vContainer = (T)(*vContainer & ~vFlagsToTakeIntoAccount); // remove these flags
                        *vContainer = (T)(*vContainer | vFlag); // add
                    }
                }
                else *vContainer = (T)(*vContainer | vFlag); // add
            }
        }
        else {
            if (vOneOrZeroAtTime) {
                if (!vAlwaysOne) *vContainer = (T)(0); // remove all
            }
            else *vContainer = (T)(*vContainer & ~vFlag); // remove one
        }
    }
    return res;
}

int main(int, char**)
{
#ifdef _MSC_VER
    // active memory leak detector
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    setlocale(LC_ALL, ".UTF8");

    // Setup GLFW window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Dear ImGui GLFW+Vulkan example", NULL, NULL);

    // Setup Vulkan
    if (!glfwVulkanSupported())
    {
        printf("GLFW: Vulkan Not Supported\n");
        return 1;
    }
    uint32_t extensions_count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensions_count);
    SetupVulkan(extensions, extensions_count);

    // Create Window Surface
    VkSurfaceKHR surface;
    VkResult err = glfwCreateWindowSurface(g_Instance, window, g_Allocator, &surface);
    check_vk_result(err);

    // Create Framebuffers
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
    SetupVulkanWindow(wd, surface, w, h);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // this mode cause the filedialog to be simple click for open directory
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    //io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    //ImGuiStyle& style = ImGui::GetStyle();
    //if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    //{
    //    style.WindowRounding = 0.0f;
    //    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    //}

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = g_Instance;
    init_info.PhysicalDevice = g_PhysicalDevice;
    init_info.Device = g_Device;
    init_info.QueueFamily = g_QueueFamily;
    init_info.Queue = g_Queue;
    init_info.PipelineCache = g_PipelineCache;
    init_info.DescriptorPool = g_DescriptorPool;
    init_info.Allocator = g_Allocator;
    init_info.MinImageCount = g_MinImageCount;
    init_info.ImageCount = wd->ImageCount;
    init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info, wd->RenderPass);

    std::vector<std::shared_ptr<VulkanImageObject>> fileDialogAssets;

    VkCommandPool command_pool = wd->Frames[wd->FrameIndex].CommandPool;

#ifdef USE_THUMBNAILS
    ImGuiFileDialog::Instance()->SetCreateThumbnailCallback([&fileDialogAssets, &command_pool, &init_info](IGFD_Thumbnail_Info* vThumbnail_Info)
        {
            if (vThumbnail_Info &&
                vThumbnail_Info->isReadyToUpload &&
                vThumbnail_Info->textureFileDatas)
            {
                std::shared_ptr<VulkanImageObject> obj = nullptr;

                // Use any command queue
                auto cmd = beginSingleTimeCommands(&init_info, command_pool);
                if (cmd)
                {
                    obj = CreateTextureFromBuffer(
                        &init_info, cmd,
                        vThumbnail_Info->textureFileDatas,
                        vThumbnail_Info->textureWidth,
                        vThumbnail_Info->textureHeight,
                        vThumbnail_Info->textureChannels);
                    vThumbnail_Info->userDatas = (void*)obj.get();

                    endSingleTimeCommands(&init_info, command_pool, cmd);
                }

                fileDialogAssets.push_back(obj);

                vThumbnail_Info->textureID = (ImTextureID)&obj->descriptor;

                delete[] vThumbnail_Info->textureFileDatas;
                vThumbnail_Info->textureFileDatas = nullptr;
                vThumbnail_Info->isReadyToUpload = false;
                vThumbnail_Info->isReadyToDisplay = true;
            }
        });
    ImGuiFileDialog::Instance()->SetDestroyThumbnailCallback([&init_info](IGFD_Thumbnail_Info* vThumbnail_Info)
        {
            if (vThumbnail_Info)
            {
                if (vThumbnail_Info->userDatas)
                {
                    DestroyTexture(&init_info, (VulkanImageObject*)vThumbnail_Info->userDatas);
                }
            }
        });
#endif // USE_THUMBNAILS

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.txt' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    // load icon font file (CustomFont.cpp)
    ImGui::GetIO().Fonts->AddFontDefault();
    static const ImWchar icons_ranges[] = { ICON_MIN_IGFD, ICON_MAX_IGFD, 0 };
    ImFontConfig icons_config; icons_config.MergeMode = true; icons_config.PixelSnapH = true;
    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_IGFD, 15.0f, &icons_config, icons_ranges);

    // Upload Fonts
    {
        // Use any command queue
        VkCommandPool command_pool = wd->Frames[wd->FrameIndex].CommandPool;
        VkCommandBuffer command_buffer = wd->Frames[wd->FrameIndex].CommandBuffer;

        err = vkResetCommandPool(g_Device, command_pool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(command_buffer, &begin_info);
        check_vk_result(err);

        ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

        VkSubmitInfo end_info = {};
        end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        end_info.commandBufferCount = 1;
        end_info.pCommandBuffers = &command_buffer;
        err = vkEndCommandBuffer(command_buffer);
        check_vk_result(err);
        err = vkQueueSubmit(g_Queue, 1, &end_info, VK_NULL_HANDLE);
        check_vk_result(err);

        err = vkDeviceWaitIdle(g_Device);
        check_vk_result(err);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

    // user textures
    std::shared_ptr<VulkanImageObject> userImage1 = nullptr;
    std::shared_ptr<VulkanImageObject> userImage2 = nullptr;
    {
        // Use any command queue
        VkCommandPool command_pool = wd->Frames[wd->FrameIndex].CommandPool;
        VkCommandBuffer command_buffer = wd->Frames[wd->FrameIndex].CommandBuffer;

        auto cmd = beginSingleTimeCommands(&init_info, command_pool);
        if (cmd)
        {
            userImage1 = CreateTextureFromFile(&init_info, cmd, "img1.png", nullptr);
            userImage2 = CreateTextureFromFile(&init_info, cmd, "img2.png", nullptr);
            endSingleTimeCommands(&init_info, command_pool, cmd);
        }
        
        err = vkDeviceWaitIdle(g_Device);
        check_vk_result(err);
    }
   
    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // singleton acces
    ImGuiFileDialog::Instance()->SetExtentionInfos(".cpp", ImVec4(1.0f, 1.0f, 0.0f, 0.9f));
    ImGuiFileDialog::Instance()->SetExtentionInfos(".h", ImVec4(0.0f, 1.0f, 0.0f, 0.9f));
    ImGuiFileDialog::Instance()->SetExtentionInfos(".hpp", ImVec4(0.0f, 0.0f, 1.0f, 0.9f));
    ImGuiFileDialog::Instance()->SetExtentionInfos(".md", ImVec4(1.0f, 0.0f, 1.0f, 0.9f));
    ImGuiFileDialog::Instance()->SetExtentionInfos(".png", ImVec4(0.0f, 1.0f, 1.0f, 0.9f), ICON_IGFD_FILE_PIC); // add an icon for the filter type
    ImGuiFileDialog::Instance()->SetExtentionInfos(".gif", ImVec4(0.0f, 1.0f, 0.5f, 0.9f), "[GIF]"); // add an text for a filter type

    // just for show multi dialog instance behavior (here use for shwo directory query dialog)
    ImGuiFileDialog fileDialog2;
    fileDialog2.SetExtentionInfos(".cpp", ImVec4(1.0f, 1.0f, 0.0f, 0.9f));
    fileDialog2.SetExtentionInfos(".h", ImVec4(0.0f, 1.0f, 0.0f, 0.9f));
    fileDialog2.SetExtentionInfos(".hpp", ImVec4(0.0f, 0.0f, 1.0f, 0.9f));
    fileDialog2.SetExtentionInfos(".md", ImVec4(1.0f, 0.0f, 1.0f, 0.9f));
    fileDialog2.SetExtentionInfos(".png", ImVec4(0.0f, 1.0f, 1.0f, 0.9f), ICON_IGFD_FILE_PIC); // add an icon for the filter type
    fileDialog2.SetExtentionInfos(".gif", ImVec4(0.0f, 1.0f, 0.5f, 0.9f), "[GIF]"); // add an text for a filter type

    // c interface
    auto cfileDialog = IGFD_Create();
    IGFD_SetExtentionInfos(cfileDialog, ".cpp", ImVec4(1.0f, 1.0f, 0.0f, 0.9f), "");
    IGFD_SetExtentionInfos(cfileDialog, ".cpp", ImVec4(1.0f, 1.0f, 0.0f, 0.9f), "");
    IGFD_SetExtentionInfos(cfileDialog, ".h", ImVec4(0.0f, 1.0f, 0.0f, 0.9f), "");
    IGFD_SetExtentionInfos(cfileDialog, ".hpp", ImVec4(0.0f, 0.0f, 1.0f, 0.9f), "");
    IGFD_SetExtentionInfos(cfileDialog, ".md", ImVec4(1.0f, 0.0f, 1.0f, 0.9f), "");
    IGFD_SetExtentionInfos(cfileDialog, ".png", ImVec4(0.0f, 1.0f, 1.0f, 0.9f), ICON_IGFD_FILE_PIC); // add an icon for the filter type
    IGFD_SetExtentionInfos(cfileDialog, ".gif", ImVec4(0.0f, 1.0f, 0.5f, 0.9f), "[GIF]"); // add an text for a filter type

#ifdef USE_BOOKMARK
    // load bookmarks
    std::ifstream docFile_1("bookmarks_1.conf", std::ios::in);
    if (docFile_1.is_open())
    {
        std::stringstream strStream;
        strStream << docFile_1.rdbuf();//read the file
        ImGuiFileDialog::Instance()->DeserializeBookmarks(strStream.str());
        docFile_1.close();
    }

    std::ifstream docFile_2("bookmarks_2.conf", std::ios::in);
    if (docFile_2.is_open())
    {
        std::stringstream strStream;
        strStream << docFile_2.rdbuf();//read the file
        fileDialog2.DeserializeBookmarks(strStream.str());
        docFile_2.close();
    }

    // c interface
    std::ifstream docFile_c("bookmarks_c.conf", std::ios::in);
    if (docFile_c.is_open())
    {
        std::stringstream strStream;
        strStream << docFile_c.rdbuf();//read the file
        IGFD_DeserializeBookmarks(cfileDialog, strStream.str().c_str());
        docFile_c.close();
    }
#endif

    static std::string filePathName = "";
    static std::string filePath = "";
    static std::string filter = "";
    static std::string userDatas = "";
    static std::vector<std::pair<std::string, std::string>> selection = {};

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);

        // Resize swap chain?
        if (g_SwapChainRebuild)
        {
            if (display_w > 0 && display_h > 0)
            {
                ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
                ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, &g_MainWindowData, g_QueueFamily, g_Allocator, display_w, display_h, g_MinImageCount);
                g_MainWindowData.FrameIndex = 0;
                g_SwapChainRebuild = false;
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
        {
            ImGui::Begin("imGuiFileDialog Demo");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
            ImGui::Separator();

            ImGui::Text("imGuiFileDialog Demo %s : ", IMGUIFILEDIALOG_VERSION);
            ImGui::Indent();
            {
#ifdef USE_EXPLORATION_BY_KEYS
                static float flashingAttenuationInSeconds = 1.0f;
                if (ImGui::Button("R##resetflashlifetime"))
                {
                    flashingAttenuationInSeconds = 1.0f;
                    ImGuiFileDialog::Instance()->SetFlashingAttenuationInSeconds(flashingAttenuationInSeconds);
                    fileDialog2.SetFlashingAttenuationInSeconds(flashingAttenuationInSeconds);

                    // c interface
                    IGFD_SetFlashingAttenuationInSeconds(cfileDialog, flashingAttenuationInSeconds);
                }
                ImGui::SameLine();
                ImGui::PushItemWidth(200);
                if (ImGui::SliderFloat("Flash lifetime (s)", &flashingAttenuationInSeconds, 0.01f, 5.0f))
                {
                    ImGuiFileDialog::Instance()->SetFlashingAttenuationInSeconds(flashingAttenuationInSeconds);
                    fileDialog2.SetFlashingAttenuationInSeconds(flashingAttenuationInSeconds);

                    // c interface
                    IGFD_SetFlashingAttenuationInSeconds(cfileDialog, flashingAttenuationInSeconds);
                }
                ImGui::PopItemWidth();
#endif
                static bool _UseWindowContraints = true;
                ImGui::Separator();
                ImGui::Checkbox("Use file dialog constraint", &_UseWindowContraints);
                ImGui::Text("Constraints is used here for define min/max file dialog size");
                ImGui::Separator();
                static bool standardDialogMode = false;
                ImGui::Text("Open Mode : ");
                ImGui::SameLine();
                if (RadioButtonLabeled("Standard", "Open dialog in standard mode", standardDialogMode, false)) standardDialogMode = true;
                ImGui::SameLine();
                if (RadioButtonLabeled("Modal", "Open dialog in modal mode", !standardDialogMode, false)) standardDialogMode = false;

                static ImGuiFileDialogFlags flags = ImGuiFileDialogFlags_Default;
                ImGui::Text("ImGuiFileDialog Flags : ");
                ImGui::Indent();
                ImGui::Text("Commons :");
                RadioButtonLabeled_BitWize<ImGuiFileDialogFlags>("Overwrite", "Overwrite verifcation before dialog closing", &flags, ImGuiFileDialogFlags_ConfirmOverwrite);
                ImGui::SameLine();
                RadioButtonLabeled_BitWize<ImGuiFileDialogFlags>("Hide Hidden Files", "Hide Hidden Files", &flags, ImGuiFileDialogFlags_DontShowHiddenFiles);
                ImGui::SameLine();
                RadioButtonLabeled_BitWize<ImGuiFileDialogFlags>("Disable Directory Creation", "Disable Directory Creation button in dialog", &flags, ImGuiFileDialogFlags_DisableCreateDirectoryButton);

                ImGui::Text("Hide Column by default : (saved in imgui.ini, \n\tso defined when the inmgui.ini is not existing)");
                RadioButtonLabeled_BitWize<ImGuiFileDialogFlags>("Hide Column Type", "Hide Column file type by default", &flags, ImGuiFileDialogFlags_HideColumnType);
                ImGui::SameLine();
                RadioButtonLabeled_BitWize<ImGuiFileDialogFlags>("Hide Column Size", "Hide Column file Size by default", &flags, ImGuiFileDialogFlags_HideColumnSize);
                ImGui::SameLine();
                RadioButtonLabeled_BitWize<ImGuiFileDialogFlags>("Hide Column Date", "Hide Column file Date by default", &flags, ImGuiFileDialogFlags_HideColumnDate);
                ImGui::Unindent();

                ImGui::Text("Singleton acces :");
                if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open File Dialog"))
                {
                    const char* filters = ".*,.cpp,.h,.hpp";
                    if (standardDialogMode)
                        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, ".", "", 1, nullptr, flags);
                    else
                        ImGuiFileDialog::Instance()->OpenModal("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, ".", "", 1, nullptr, flags);
                }
                if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open File Dialog with collections of filters"))
                {
                    const char* filters = "Source files (*.cpp *.h *.hpp){.cpp,.h,.hpp},Image files (*.png *.gif *.jpg *.jpeg){.png,.gif,.jpg,.jpeg},.md";
                    if (standardDialogMode)
                        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, ".", "", 1, nullptr, flags);
                    else
                        ImGuiFileDialog::Instance()->OpenModal("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, ".", "", 1, nullptr, flags);
                }
                if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open File Dialog with selection of 5 items"))
                {
                    const char* filters = ".*,.cpp,.h,.hpp";
                    if (standardDialogMode)
                        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, ".", "", 5, nullptr, flags);
                    else
                        ImGuiFileDialog::Instance()->OpenModal("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, ".", "", 5, nullptr, flags);
                }
                if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open File Dialog with infinite selection"))
                {
                    const char* filters = ".*,.cpp,.h,.hpp";
                    if (standardDialogMode)
                        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, ".", "", 0, nullptr, flags);
                    else
                        ImGuiFileDialog::Instance()->OpenModal("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, ".", "", 0, nullptr, flags);
                }
                if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open File Dialog with last file path name"))
                {
                    const char* filters = ".*,.cpp,.h,.hpp";
                    if (standardDialogMode)
                        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, filePathName, 1, nullptr, flags);
                    else
                        ImGuiFileDialog::Instance()->OpenModal("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", filters, filePathName, 1, nullptr, flags);
                }
                if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open All file types with filter .*"))
                {
                    if (standardDialogMode)
                        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", ".*", ".", "", 1, nullptr, flags);
                    else
                        ImGuiFileDialog::Instance()->OpenModal("ChooseFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a File", ".*", ".", "", 1, nullptr, flags);
                }
                if (ImGui::Button(ICON_IGFD_SAVE " Save File Dialog with a custom pane"))
                {
                    const char* filters = "C++ File (*.cpp){.cpp}";
                    if (standardDialogMode)
                        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey",
                            ICON_IGFD_SAVE " Choose a File", filters,
                            ".", "", std::bind(&InfosPane, std::placeholders::_1, std::placeholders::_2,
                                std::placeholders::_3), 350, 1, IGFDUserDatas("SaveFile"), flags);
                    else
                        ImGuiFileDialog::Instance()->OpenModal("ChooseFileDlgKey",
                            ICON_IGFD_SAVE " Choose a File", filters,
                            ".", "", std::bind(&InfosPane, std::placeholders::_1, std::placeholders::_2,
                                std::placeholders::_3), 350, 1, IGFDUserDatas("SaveFile"), flags);
                }
                if (ImGui::Button(ICON_IGFD_SAVE " Save File Dialog with Confirm Dialog For Overwrite File if exist"))
                {
                    const char* filters = "C/C++ File (*.c *.cpp){.c,.cpp}, Header File (*.h){.h}";
                    if (standardDialogMode)
                        ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", ICON_IGFD_SAVE " Choose a File", filters, ".", "", 1, IGFDUserDatas("SaveFile"), ImGuiFileDialogFlags_ConfirmOverwrite);
                    else
                        ImGuiFileDialog::Instance()->OpenModal("ChooseFileDlgKey", ICON_IGFD_SAVE " Choose a File", filters, ".", "", 1, IGFDUserDatas("SaveFile"), ImGuiFileDialogFlags_ConfirmOverwrite);
                }

                ImGui::Text("Other Instance (multi dialog demo) :");
                if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open Directory Dialog"))
                {
                    // let filters be null for open directory chooser
                    if (standardDialogMode)
                        fileDialog2.OpenDialog("ChooseDirDlgKey",
                            ICON_IGFD_FOLDER_OPEN " Choose a Directory", nullptr, ".", 1, nullptr, flags);
                    else
                        fileDialog2.OpenModal("ChooseDirDlgKey",
                            ICON_IGFD_FOLDER_OPEN " Choose a Directory", nullptr, ".", 1, nullptr, flags);
                }
                if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open Directory Dialog with selection of 5 items"))
                {
                    // set filters be null for open directory chooser
                    if (standardDialogMode)
                        fileDialog2.OpenDialog("ChooseDirDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a Directory", nullptr, ".", "", 5, nullptr, flags);
                    else
                        fileDialog2.OpenModal("ChooseDirDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a Directory", nullptr, ".", "", 5, nullptr, flags);
                }

                ImGui::Separator();

                /////////////////////////////////////////////////////////////////
                // C Interface
                /////////////////////////////////////////////////////////////////
                ImGui::Text("C Instance demo :");
                if (ImGui::Button("C " ICON_IGFD_SAVE " Save File Dialog with a custom pane"))
                {
                    const char* filters = "C++ File (*.cpp){.cpp}";
                    if (standardDialogMode)
                        IGFD_OpenPaneDialog(cfileDialog, "ChooseFileDlgKey",
                            ICON_IGFD_SAVE " Choose a File", filters,
                            ".", "", &InfosPane, 350, 1, (void*)("SaveFile"), flags);
                    else
                        IGFD_OpenPaneModal(cfileDialog, "ChooseFileDlgKey",
                            ICON_IGFD_SAVE " Choose a File", filters,
                            ".", "", &InfosPane, 350, 1, (void*)("SaveFile"), flags);
                }
                /////////////////////////////////////////////////////////////////
                /////////////////////////////////////////////////////////////////

                ImVec2 minSize = ImVec2(0, 0);
                ImVec2 maxSize = ImVec2(FLT_MAX, FLT_MAX);

                if (_UseWindowContraints)
                {
                    maxSize = ImVec2((float)display_w, (float)display_h) * 0.7f;
                    minSize = maxSize * 0.25f;
                }

                // you can define your flags and min/max window size (theses three settings ae defined by default :
                // flags => ImGuiWindowFlags_NoCollapse
                // minSize => 0,0
                // maxSize => FLT_MAX, FLT_MAX (defined is float.h)

                if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey",
                    ImGuiWindowFlags_NoCollapse, minSize, maxSize))
                {
                    if (ImGuiFileDialog::Instance()->IsOk())
                    {
                        filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                        filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
                        filter = ImGuiFileDialog::Instance()->GetCurrentFilter();
                        // here convert from string because a string was passed as a userDatas, but it can be what you want
                        if (ImGuiFileDialog::Instance()->GetUserDatas())
                            userDatas = std::string((const char*)ImGuiFileDialog::Instance()->GetUserDatas());
                        auto sel = ImGuiFileDialog::Instance()->GetSelection(); // multiselection
                        selection.clear();
                        for (auto s : sel)
                        {
                            selection.emplace_back(s.first, s.second);
                        }
                        // action
                    }
                    ImGuiFileDialog::Instance()->Close();
                }

                if (fileDialog2.Display("ChooseDirDlgKey",
                    ImGuiWindowFlags_NoCollapse, minSize, maxSize))
                {
                    if (fileDialog2.IsOk())
                    {
                        filePathName = fileDialog2.GetFilePathName();
                        filePath = fileDialog2.GetCurrentPath();
                        filter = fileDialog2.GetCurrentFilter();
                        // here convert from string because a string was passed as a userDatas, but it can be what you want
                        if (fileDialog2.GetUserDatas())
                            userDatas = std::string((const char*)fileDialog2.GetUserDatas());
                        auto sel = fileDialog2.GetSelection(); // multiselection
                        selection.clear();
                        for (auto s : sel)
                        {
                            selection.emplace_back(s.first, s.second);
                        }
                        // action
                    }
                    fileDialog2.Close();
                }

                /////////////////////////////////////////////////////////////////
                // C Interface
                /////////////////////////////////////////////////////////////////
                if (IGFD_DisplayDialog(cfileDialog, "ChooseFileDlgKey",
                    ImGuiWindowFlags_NoCollapse, minSize, maxSize))
                {
                    if (IGFD_IsOk(cfileDialog))
                    {
                        char* cfilePathName = IGFD_GetFilePathName(cfileDialog);
                        if (cfilePathName) filePathName = cfilePathName;
                        char* cfilePath = IGFD_GetCurrentPath(cfileDialog);
                        if (cfilePath) filePath = cfilePath;
                        char* cfilter = IGFD_GetCurrentFilter(cfileDialog);
                        if (cfilter) filter = cfilter;
                        // here convert from string because a string was passed as a userDatas, but it can be what you want
                        void* cdatas = IGFD_GetUserDatas(cfileDialog);
                        if (cdatas)	userDatas = (const char*)cdatas;
                        IGFD_Selection csel = IGFD_GetSelection(cfileDialog); // multiselection

                        selection.clear();
                        for (size_t i = 0; i < csel.count; i++)
                        {
                            std::string _fileName = csel.table[i].fileName;
                            std::string _filePathName = csel.table[i].filePathName;
                            selection.emplace_back(_fileName, _filePathName);
                        }

                        // destroy
                        if (cfilePathName) delete[] cfilePathName;
                        if (cfilePath) delete[] cfilePath;
                        if (cfilter) delete[] cfilter;
                        IGFD_Selection_DestroyContent(&csel);
                    }
                    IGFD_CloseDialog(cfileDialog);
                }
                /////////////////////////////////////////////////////////////////
                /////////////////////////////////////////////////////////////////
                /////////////////////////////////////////////////////////////////

                ImGui::Separator();

                ImGui::Text("ImGuiFileDialog Return's :\n");
                ImGui::Indent();
                {
                    ImGui::Text("GetFilePathName() : %s", filePathName.c_str());
                    ImGui::Text("GetFilePath() : %s", filePath.c_str());
                    ImGui::Text("GetCurrentFilter() : %s", filter.c_str());
                    ImGui::Text("GetUserDatas() (was a std::string in this sample) : %s", userDatas.c_str());
                    ImGui::Text("GetSelection() : ");
                    ImGui::Indent();
                    {
                        static int selected = false;
                        if (ImGui::BeginTable("##GetSelection", 2,
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY))
                        {
                            ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                            ImGui::TableSetupColumn("File Name", ImGuiTableColumnFlags_WidthStretch, -1, 0);
                            ImGui::TableSetupColumn("File Path name", ImGuiTableColumnFlags_WidthFixed, -1, 1);
                            ImGui::TableHeadersRow();

                            ImGuiListClipper clipper;
                            clipper.Begin((int)selection.size(), ImGui::GetTextLineHeightWithSpacing());
                            while (clipper.Step())
                            {
                                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                                {
                                    const auto& sel = selection[i];
                                    ImGui::TableNextRow();
                                    if (ImGui::TableSetColumnIndex(0)) // first column
                                    {
                                        ImGuiSelectableFlags selectableFlags = ImGuiSelectableFlags_AllowDoubleClick;
                                        selectableFlags |= ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;
                                        if (ImGui::Selectable(sel.first.c_str(), i == selected, selectableFlags)) selected = i;
                                    }
                                    if (ImGui::TableSetColumnIndex(1)) // second column
                                    {
                                        ImGui::Text("%s", sel.second.c_str());
                                    }
                                }
                            }
                            clipper.End();

                            ImGui::EndTable();
                        }
                    }
                    ImGui::Unindent();
                }
                ImGui::Unindent();
            }
            ImGui::Unindent();

            ImGui::Separator();
            ImGui::Text("Window mode :");
            ImGui::Separator();

            ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        // 3. Show a simple window with user textures
        {
            ImGui::Begin("User Textures !");

            bool res = false;
            ImGui::BeginGroup();
            ImGui::Text("User Texture 1");
            if (userImage1.use_count() && userImage1->buf)
            {
                res = ImGui::ImageButton((ImTextureID)&userImage1->descriptor, ImVec2(150, 150));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Open a Texture");
            }
            else
            {
                res = ImGui::Button("Open a\nTexture\nfile##1", ImVec2(150, 150));
            }

            ImGui::EndGroup();

            if (res)
            {
                ImGuiFileDialog::Instance()->OpenModal("OpenTextureFile", "Open Texture File", "Images {.jpg,.png}", "", 1, IGFD::UserDatas("UserTexture1"));
            }

            ImGui::SameLine();

            res = false;
            ImGui::BeginGroup();
            ImGui::Text("User Texture 2");
            if (userImage2.use_count() && userImage2->buf)
            {
                res = ImGui::ImageButton((ImTextureID)&userImage2->descriptor, ImVec2(150, 150));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Open a Texture");
            }
            else
            {
                res = ImGui::Button("Open a\nTexture\nfile##2", ImVec2(150, 150));
            }

            ImGui::EndGroup();
            
            if (res)
            {
                ImGuiFileDialog::Instance()->OpenModal("OpenTextureFile", "Open Texture File", "Images {.jpg,.png}", "", 1, IGFD::UserDatas("UserTexture2"));
            }

            ImGui::End();
        }

        // 4. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        std::string fileToLoad;
        std::string textureInput;
        if (ImGuiFileDialog::Instance()->Display("OpenTextureFile"))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                fileToLoad = ImGuiFileDialog::Instance()->GetFilePathName();
                auto userDatas = ImGuiFileDialog::Instance()->GetUserDatas();
                if (userDatas)
                {
                    textureInput = std::string((const char*)userDatas);
                }
            }

            ImGuiFileDialog::Instance()->Close();
        }

        // Rendering
        ImGui::Render();
        ImDrawData* main_draw_data = ImGui::GetDrawData();
        const bool main_is_minimized = (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <= 0.0f);
        wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
        wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
        wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
        wd->ClearValue.color.float32[3] = clear_color.w;
        if (!main_is_minimized)
            FrameRender(wd, main_draw_data);

        // Update and Render additional Platform Windows
        //if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        //{
        //    ImGui::UpdatePlatformWindows();
        //    ImGui::RenderPlatformWindowsDefault();
        //}

        // Present Main Platform Window
        if (!main_is_minimized)
            FramePresent(wd);

        if (!fileToLoad.empty() && !textureInput.empty())
        {
            auto fd = wd->Frames[wd->FrameIndex];
            err = vkWaitForFences(g_Device, 1, &fd.Fence, VK_TRUE, UINT64_MAX);    // wait indefinitely instead of periodically checking
            check_vk_result(err);

            if (textureInput == "UserTexture1")
            {
                if (userImage1.use_count())
                {
                    DestroyTexture(&init_info, userImage1.get());
                }
            }
            else if (textureInput == "UserTexture2")
            {
                if (userImage2.use_count() && userImage2->buf)
                {
                    DestroyTexture(&init_info, userImage2.get());
                }
            }

            err = vkDeviceWaitIdle(g_Device);
            check_vk_result(err);

            // Use any command queue
            VkCommandPool command_pool = wd->Frames[wd->FrameIndex].CommandPool;
            auto cmd = beginSingleTimeCommands(&init_info, command_pool);
            if (cmd)
            {
                if (textureInput == "UserTexture1")
                {
                    userImage1 = CreateTextureFromFile(&init_info, cmd, fileToLoad.c_str());
                }
                else if (textureInput == "UserTexture2")
                {
                    userImage2 = CreateTextureFromFile(&init_info, cmd, fileToLoad.c_str());
                }
                endSingleTimeCommands(&init_info, command_pool, cmd);
            }

            err = vkDeviceWaitIdle(g_Device);
            check_vk_result(err);
        }

#ifdef USE_THUMBNAILS
        err = vkDeviceWaitIdle(g_Device);
        check_vk_result(err);
        ImGuiFileDialog::Instance()->ManageGPUThumbnails();
#endif
    }

    err = vkDeviceWaitIdle(g_Device);
    check_vk_result(err);

    userImage1.reset();
    userImage2.reset();
    fileDialogAssets.clear();

    // Cleanup
    err = vkDeviceWaitIdle(g_Device);
    check_vk_result(err);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    CleanupVulkanWindow();
    CleanupVulkan();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

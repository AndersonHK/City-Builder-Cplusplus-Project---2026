#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_6.h>
#include <vulkan/vulkan.h>

#include <vector>

#include "RendererColor.h"

// Internal Vulkan render targets keep scene color in FP16 until presentation.
constexpr VkFormat kVulkanSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// Vulkan world and preview depth images intentionally use 32-bit float depth.
constexpr VkFormat kVulkanWorldDepthFormat = VK_FORMAT_D32_SFLOAT;

struct VulkanSwapchainFormatSelection {
    bool supported;
    RendererOutputMode outputMode;
    VkFormat format;
    VkColorSpaceKHR colorSpace;
    int preferenceRank;

    VulkanSwapchainFormatSelection();
};

// DXGI monitor data used to log and reason about Windows HDR output support.
struct VulkanDxgiMonitorInfo {
    bool valid;
    unsigned int bitsPerColor;
    float minLuminance;
    float maxLuminance;
    float maxFullFrameLuminance;
    DXGI_COLOR_SPACE_TYPE colorSpace;

    VulkanDxgiMonitorInfo();
};

// Selects swapchain format/color-space in the renderer preference order:
// FP16 scRGB, HDR10 PQ 10-bit, then SDR only when explicitly allowed.
VulkanSwapchainFormatSelection VulkanChooseSwapchainFormat(const std::vector<VkSurfaceFormatKHR>& supportedFormats, bool allowSdrFallback);

// Queries the DXGI output containing the current window center for HDR logs.
VulkanDxgiMonitorInfo VulkanQueryDxgiMonitorForWindowCenter(HWND windowHandle);

// Stable names for startup logs and tests without pulling in extra utilities.
const char* VulkanFormatName(VkFormat format);
const char* VulkanColorSpaceName(VkColorSpaceKHR colorSpace);

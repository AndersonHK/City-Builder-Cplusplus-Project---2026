#include "VulkanRendererSupport.h"

#include <cstddef>

namespace {
struct VulkanSwapchainFormatCandidate {
    RendererOutputMode outputMode;
    VkFormat format;
    VkColorSpaceKHR colorSpace;
};

constexpr VulkanSwapchainFormatCandidate kHdrSwapchainCandidates[] = {
    {RendererOutputMode::HdrScRgbFp16, VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
    {RendererOutputMode::Hdr10Rgb10A2, VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT}
};

constexpr VulkanSwapchainFormatCandidate kSdrSwapchainCandidate = {
    RendererOutputMode::SdrSrgb8,
    VK_FORMAT_B8G8R8A8_SRGB,
    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
};

bool VulkanSurfaceFormatMatches(const VkSurfaceFormatKHR& supportedFormat, const VulkanSwapchainFormatCandidate& candidate) {
    if (supportedFormat.colorSpace != candidate.colorSpace) {
        return false;
    }

    return supportedFormat.format == candidate.format || supportedFormat.format == VK_FORMAT_UNDEFINED;
}

VulkanSwapchainFormatSelection VulkanSelectionFromCandidate(const VulkanSwapchainFormatCandidate& candidate, int rank) {
    VulkanSwapchainFormatSelection selection;
    selection.supported = true;
    selection.outputMode = candidate.outputMode;
    selection.format = candidate.format;
    selection.colorSpace = candidate.colorSpace;
    selection.preferenceRank = rank;
    return selection;
}

bool PointInsideRect(const RECT& rect, LONG x, LONG y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

bool FillMonitorInfoFromOutput(HWND windowHandle, IDXGIOutput6* output, VulkanDxgiMonitorInfo& monitorInfo) {
    if (output == 0) {
        return false;
    }

    DXGI_OUTPUT_DESC1 desc;
    if (FAILED(output->GetDesc1(&desc))) {
        return false;
    }

    RECT windowRect;
    if (!GetWindowRect(windowHandle, &windowRect)) {
        return false;
    }

    const LONG centerX = windowRect.left + (windowRect.right - windowRect.left) / 2;
    const LONG centerY = windowRect.top + (windowRect.bottom - windowRect.top) / 2;
    if (!PointInsideRect(desc.DesktopCoordinates, centerX, centerY)) {
        return false;
    }

    monitorInfo.valid = true;
    monitorInfo.bitsPerColor = desc.BitsPerColor;
    monitorInfo.minLuminance = desc.MinLuminance;
    monitorInfo.maxLuminance = desc.MaxLuminance;
    monitorInfo.maxFullFrameLuminance = desc.MaxFullFrameLuminance;
    monitorInfo.colorSpace = desc.ColorSpace;
    return true;
}
}

VulkanSwapchainFormatSelection::VulkanSwapchainFormatSelection()
    : supported(false),
      outputMode(RendererOutputMode::SdrSrgb8),
      format(VK_FORMAT_UNDEFINED),
      colorSpace(VK_COLOR_SPACE_SRGB_NONLINEAR_KHR),
      preferenceRank(-1) {
}

VulkanDxgiMonitorInfo::VulkanDxgiMonitorInfo()
    : valid(false),
      bitsPerColor(0u),
      minLuminance(0.0f),
      maxLuminance(0.0f),
      maxFullFrameLuminance(0.0f),
      colorSpace(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
}

VulkanSwapchainFormatSelection VulkanChooseSwapchainFormat(const std::vector<VkSurfaceFormatKHR>& supportedFormats, bool allowSdrFallback) {
    int rank = 0;
    std::size_t candidateIndex = 0u;
    for (; candidateIndex < sizeof(kHdrSwapchainCandidates) / sizeof(kHdrSwapchainCandidates[0]); ++candidateIndex, ++rank) {
        const VulkanSwapchainFormatCandidate& candidate = kHdrSwapchainCandidates[candidateIndex];
        std::size_t formatIndex = 0u;
        for (; formatIndex < supportedFormats.size(); ++formatIndex) {
            if (VulkanSurfaceFormatMatches(supportedFormats[formatIndex], candidate)) {
                return VulkanSelectionFromCandidate(candidate, rank);
            }
        }
    }

    if (allowSdrFallback) {
        std::size_t formatIndex = 0u;
        for (; formatIndex < supportedFormats.size(); ++formatIndex) {
            if (VulkanSurfaceFormatMatches(supportedFormats[formatIndex], kSdrSwapchainCandidate)) {
                return VulkanSelectionFromCandidate(kSdrSwapchainCandidate, rank);
            }
        }
    }

    return VulkanSwapchainFormatSelection();
}

VulkanDxgiMonitorInfo VulkanQueryDxgiMonitorForWindowCenter(HWND windowHandle) {
    VulkanDxgiMonitorInfo monitorInfo;
    if (windowHandle == 0) {
        return monitorInfo;
    }

    IDXGIFactory1* factory = 0;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        return monitorInfo;
    }

    for (UINT adapterIndex = 0u; ; ++adapterIndex) {
        IDXGIAdapter1* adapter = 0;
        const HRESULT adapterResult = factory->EnumAdapters1(adapterIndex, &adapter);
        if (adapterResult == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(adapterResult)) {
            if (adapter != 0) {
                adapter->Release();
            }
            continue;
        }

        for (UINT outputIndex = 0u; ; ++outputIndex) {
            IDXGIOutput* output = 0;
            const HRESULT outputResult = adapter->EnumOutputs(outputIndex, &output);
            if (outputResult == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(outputResult)) {
                if (output != 0) {
                    output->Release();
                }
                continue;
            }

            IDXGIOutput6* output6 = 0;
            if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&output6)))) {
                const bool matched = FillMonitorInfoFromOutput(windowHandle, output6, monitorInfo);
                output6->Release();
                output->Release();
                if (matched) {
                    adapter->Release();
                    factory->Release();
                    return monitorInfo;
                }
            } else {
                output->Release();
            }
        }

        adapter->Release();
    }

    factory->Release();
    return monitorInfo;
}

const char* VulkanFormatName(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return "VK_FORMAT_R16G16B16A16_SFLOAT";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
    case VK_FORMAT_B8G8R8A8_SRGB:
        return "VK_FORMAT_B8G8R8A8_SRGB";
    default:
        return "VK_FORMAT_UNKNOWN";
    }
}

const char* VulkanColorSpaceName(VkColorSpaceKHR colorSpace) {
    switch (colorSpace) {
    case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
        return "VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT";
    case VK_COLOR_SPACE_HDR10_ST2084_EXT:
        return "VK_COLOR_SPACE_HDR10_ST2084_EXT";
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
        return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
    default:
        return "VK_COLOR_SPACE_UNKNOWN";
    }
}

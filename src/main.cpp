#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <optional>
#include <limits>
#include <array>
#include <stdexcept>

#ifndef VKH_VALIDATION
#define VKH_VALIDATION 0
#endif

#define VK_CHECK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { \
    std::fprintf(stderr, "Vulkan error %d at %s:%d\n", _r, __FILE__, __LINE__); std::exit(1);} } while(0)

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    std::fprintf(stderr, "[VK] %s\n", data->pMessage);
    return VK_FALSE;
}

struct VulkanApp {
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT dbg = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t qFamGraphics = 0;
    VkQueue queueGraphics = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkColorSpaceKHR swapColorSpace{};
    VkExtent2D swapExtent{};
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdBufs;

    VkSemaphore semImageAvailable = VK_NULL_HANDLE;
    VkSemaphore semRenderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;

    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

    void initWindow() {
        if (!glfwInit()) throw std::runtime_error("GLFW init failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(800, 600, "Vulkan Hello (clear blue)", nullptr, nullptr);
        if (!window) throw std::runtime_error("Window creation failed");
    }

    void initVulkan() {
        createInstance();
        setupDebug();
        createSurface();
        pickPhysicalDevice();
        createDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createFramebuffers();
        createCommandPool();
        allocateCommandBuffers();
        recordCommandBuffers();
        createSyncObjects();
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
        }
        vkDeviceWaitIdle(device);
    }

    void cleanupSwapchain() {
        for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        framebuffers.clear();
        if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr), renderPass = VK_NULL_HANDLE;
        for (auto v : swapViews) vkDestroyImageView(device, v, nullptr);
        swapViews.clear();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr), swapchain = VK_NULL_HANDLE;
    }

    void cleanup() {
        vkDeviceWaitIdle(device);
        vkDestroySemaphore(device, semImageAvailable, nullptr);
        vkDestroySemaphore(device, semRenderFinished, nullptr);
        vkDestroyFence(device, inFlight, nullptr);

        vkDestroyCommandPool(device, cmdPool, nullptr);
        cleanupSwapchain();

        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);

#if VKH_VALIDATION
        auto pfnDestroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (pfnDestroyMessenger) pfnDestroyMessenger(instance, dbg, nullptr);
#endif

        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void recreateSwapchain() {
        int w=0,h=0;
        glfwGetFramebufferSize(window, &w, &h);
        while (w == 0 || h == 0) {
            glfwGetFramebufferSize(window, &w, &h);
            glfwWaitEvents();
        }
        vkDeviceWaitIdle(device);
        cleanupSwapchain();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createFramebuffers();
        recordCommandBuffers();
    }

    void createInstance() {
        uint32_t extCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
        std::vector<const char*> exts(glfwExts, glfwExts + extCount);
#if VKH_VALIDATION
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "two-d-sim";
        app.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();

#if VKH_VALIDATION
        const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = layers;
#endif
        VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));
    }

    void setupDebug() {
#if VKH_VALIDATION
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = debugCallback;

        auto pfnCreateMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (pfnCreateMessenger) VK_CHECK(pfnCreateMessenger(instance, &info, nullptr, &dbg));
#endif
    }

    void createSurface() {
        VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));
    }

    void pickPhysicalDevice() {
        uint32_t n=0; VK_CHECK(vkEnumeratePhysicalDevices(instance, &n, nullptr));
        if (n==0) throw std::runtime_error("No Vulkan devices");
        std::vector<VkPhysicalDevice> devs(n);
        VK_CHECK(vkEnumeratePhysicalDevices(instance, &n, devs.data()));

        // Choose first device that supports graphics + present on same queue family
        for (auto d: devs) {
            uint32_t qCount=0; vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> qProps(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, qProps.data());
            for (uint32_t i=0;i<qCount;++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present);
                if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    phys = d; qFamGraphics = i;
                    return;
                }
            }
        }
        // Fallback: allow separate present queue (kept simple: just fail if not same)
        throw std::runtime_error("No device with graphics+present on same queue family");
    }

    void createDevice() {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = qFamGraphics;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;

        const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        VkPhysicalDeviceFeatures feats{};
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = devExts;
        dci.pEnabledFeatures = &feats;

        VK_CHECK(vkCreateDevice(phys, &dci, nullptr, &device));
        vkGetDeviceQueue(device, qFamGraphics, 0, &queueGraphics);
    }

    struct SwapSupport {
        VkSurfaceCapabilitiesKHR caps{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> modes;
    };

    SwapSupport querySwapSupport() {
        SwapSupport s{};
        uint32_t n=0;
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &s.caps));
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &n, nullptr));
        s.formats.resize(n);
        if (n) VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &n, s.formats.data()));
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &n, nullptr));
        s.modes.resize(n);
        if (n) VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &n, s.modes.data()));
        return s;
    }

    void createSwapchain() {
        auto ss = querySwapSupport();

        VkSurfaceFormatKHR chosenFmt = ss.formats.empty()
            ? VkSurfaceFormatKHR{swapFormat, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
            : ss.formats[0];

        // Prefer SRGB BGRA if available
        for (auto& f: ss.formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosenFmt = f; break; }
        }
        swapFormat = chosenFmt.format; swapColorSpace = chosenFmt.colorSpace;

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available
        // Try MAILBOX for low-latency if supported
        for (auto m: ss.modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = m; break; }

        // Choose extent (window size)
        if (ss.caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            swapExtent = ss.caps.currentExtent;
        } else {
            int w,h; glfwGetFramebufferSize(window, &w, &h);
            swapExtent = { (uint32_t)w, (uint32_t)h };
            swapExtent.width = std::max(ss.caps.minImageExtent.width,
                                  std::min(ss.caps.maxImageExtent.width, swapExtent.width));
            swapExtent.height = std::max(ss.caps.minImageExtent.height,
                                   std::min(ss.caps.maxImageExtent.height, swapExtent.height));
        }

        uint32_t desired = ss.caps.minImageCount + 1;
        if (ss.caps.maxImageCount && desired > ss.caps.maxImageCount) desired = ss.caps.maxImageCount;

        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface;
        ci.minImageCount = desired;
        ci.imageFormat = swapFormat;
        ci.imageColorSpace = swapColorSpace;
        ci.imageExtent = swapExtent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = ss.caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = presentMode;
        ci.clipped = VK_TRUE;

        VK_CHECK(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain));

        uint32_t n=0; VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr));
        swapImages.resize(n);
        VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &n, swapImages.data()));
    }

    void createImageViews() {
        swapViews.resize(swapImages.size());
        for (size_t i=0;i<swapImages.size();++i) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = swapImages[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = swapFormat;
            vi.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            VK_CHECK(vkCreateImageView(device, &vi, nullptr, &swapViews[i]));
        }
    }

    void createRenderPass() {
        VkAttachmentDescription color{};
        color.format = swapFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colorRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;

        VK_CHECK(vkCreateRenderPass(device, &rp, nullptr, &renderPass));
    }

    void createFramebuffers() {
        framebuffers.resize(swapViews.size());
        for (size_t i=0;i<swapViews.size();++i) {
            VkImageView atts[] = { swapViews[i] };
            VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbi.renderPass = renderPass;
            fbi.attachmentCount = 1;
            fbi.pAttachments = atts;
            fbi.width = swapExtent.width;
            fbi.height = swapExtent.height;
            fbi.layers = 1;
            VK_CHECK(vkCreateFramebuffer(device, &fbi, nullptr, &framebuffers[i]));
        }
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.queueFamilyIndex = qFamGraphics;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(device, &ci, nullptr, &cmdPool));
    }

    void allocateCommandBuffers() {
        cmdBufs.resize(framebuffers.size());
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = (uint32_t)cmdBufs.size();
        VK_CHECK(vkAllocateCommandBuffers(device, &ai, cmdBufs.data()));
    }

    void recordCommandBuffers() {
        for (size_t i=0;i<cmdBufs.size();++i) {
            VkCommandBuffer cb = cmdBufs[i];
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_CHECK(vkBeginCommandBuffer(cb, &bi));

            VkClearValue clear{};
            clear.color = { {0.1f, 0.2f, 0.8f, 1.0f} }; // nice blue

            VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rp.renderPass = renderPass;
            rp.framebuffer = framebuffers[i];
            rp.renderArea.offset = {0,0};
            rp.renderArea.extent = swapExtent;
            rp.clearValueCount = 1;
            rp.pClearValues = &clear;

            vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
            // nothing else, just clear
            vkCmdEndRenderPass(cb);

            VK_CHECK(vkEndCommandBuffer(cb));
        }
    }

    void createSyncObjects() {
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &semImageAvailable));
        VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &semRenderFinished));
        VK_CHECK(vkCreateFence(device, &fi, nullptr, &inFlight));
    }

    void drawFrame() {
        VK_CHECK(vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_C(1'000'000'000)));
        VK_CHECK(vkResetFences(device, 1, &inFlight));

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device, swapchain, UINT64_C(1'000'000'000),
                                             semImageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
        VK_CHECK(acq);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &semImageAvailable;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmdBufs[imageIndex];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &semRenderFinished;

        VK_CHECK(vkQueueSubmit(queueGraphics, 1, &submit, inFlight));

        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &semRenderFinished;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;

        VkResult pr = vkQueuePresentKHR(queueGraphics, &present);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain();
        } else {
            VK_CHECK(pr);
        }
    }
};

int main() {
    try {
        VulkanApp app;
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}

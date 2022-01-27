// MIT License
//
// Copyright(c) 2022 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "pch.h"

#include "layer.h"
#include "log.h"

namespace {

    // 2 views to process, one per eye.
    constexpr uint32_t ViewCount = 2;

    using namespace passthrough;
    using namespace passthrough::log;

    using namespace xr::math;

    class GraphicsResources {
      public:
        GraphicsResources(OpenXrApi& openXR, XrSystemId systemId, ID3D11Device* device)
            : m_openXR(openXR), m_systemId(systemId), m_d3d11Device(device) {
            m_d3d11Device->GetImmediateContext(&m_d3d11DeviceContext);
        }

        GraphicsResources(OpenXrApi& openXR,
                          XrSystemId systemId,
                          ID3D12Device* device,
                          ID3D12CommandQueue* commandQueue)
            : m_openXR(openXR), m_systemId(systemId), m_d3d12Device(device), m_d3d12CommandQueue(commandQueue) {
            // Create resources for interop.
            D3D_FEATURE_LEVEL featureLevel = {D3D_FEATURE_LEVEL_11_1};
            CHECK_HRCMD(D3D11On12CreateDevice(m_d3d12Device.Get(),
                                              D3D11_CREATE_DEVICE_SINGLETHREADED,
                                              &featureLevel,
                                              1,
                                              reinterpret_cast<IUnknown**>(m_d3d12CommandQueue.GetAddressOf()),
                                              1,
                                              0,
                                              &m_d3d11Device,
                                              &m_d3d11DeviceContext,
                                              nullptr));
            CHECK_HRCMD(m_d3d11Device->QueryInterface(__uuidof(ID3D11On12Device),
                                                      reinterpret_cast<void**>(m_d3d11on12Device.GetAddressOf())));

            // Create a fence so we can wait for pending work upon shutdown.
            CHECK_HRCMD(m_d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_d3d12Fence)));
        }

        ~GraphicsResources() {
            if (m_d3d12Device) {
                // Wait for all resources to be safe to destroy.
                m_d3d12CommandQueue->Signal(m_d3d12Fence.Get(), 1);
                if (m_d3d12Fence->GetCompletedValue() < 1) {
                    HANDLE eventHandle = CreateEventEx(nullptr, L"Flush D3D12 Fence", 0, EVENT_ALL_ACCESS);
                    CHECK_HRCMD(m_d3d12Fence->SetEventOnCompletion(1, eventHandle));
                    WaitForSingleObject(eventHandle, INFINITE);
                    CloseHandle(eventHandle);
                }
            }

            m_passthroughLayerRenderTarget.clear();
            m_passthroughLayerTexture.clear();
            if (m_passthroughLayerSwapchain != XR_NULL_HANDLE) {
                m_openXR.xrDestroySwapchain(m_passthroughLayerSwapchain);
            }

            if (m_viewSpace != XR_NULL_HANDLE) {
                m_openXR.xrDestroySpace(m_viewSpace);
            }
        }

        void connect(XrSession session) {
            m_session = session;

            {
                XrReferenceSpaceCreateInfo createInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO, nullptr};
                createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
                createInfo.poseInReferenceSpace = Pose::Identity();
                CHECK_XRCMD(m_openXR.xrCreateReferenceSpace(m_session, &createInfo, &m_viewSpace));
            }

            // TODO: Connect to the camera service.

            // Allocate a swapchain for the camera layer.
            createSwapchain();

            // TODO: Allocate resources for drawing the camera layer.

            m_isConnected = true;
        }

        bool drawPassthroughLayer(XrCompositionLayerProjection& layer,
                                  XrTime displayTime,
                                  const XrCompositionLayerProjection* proj0) {
            assert(layer.viewCount == ViewCount);

            XrView projViews[ViewCount] = {{XR_TYPE_VIEW, nullptr}, {XR_TYPE_VIEW, nullptr}};
            if (!proj0) {
                XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO, nullptr};
                locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                locateInfo.space = m_viewSpace;
                locateInfo.displayTime = displayTime;

                XrViewState state{XR_TYPE_VIEW_STATE, nullptr};
                uint32_t viewCount;
                CHECK_XRCMD(m_openXR.xrLocateViews(m_session, &locateInfo, &state, ViewCount, &viewCount, projViews));
                if (!Pose::IsPoseValid(state.viewStateFlags)) {
                    return false;
                }
            }

            beginSwapchainContext();

            // TODO: Import the texture from the camera service.

            // TODO: Draw the camera layer.
            beginDrawContext();
            {
                float clearColor[] = {0, 1, 0, 0};
                m_currentContext->ClearRenderTargetView(m_passthroughLayerRenderTarget[m_swapchainImageIndex].Get(),
                                                        clearColor);
            }
            endDrawContext();

            endSwapchainContext();

            XrCompositionLayerProjectionView* views = const_cast<XrCompositionLayerProjectionView*>(layer.views);
            for (uint32_t i = 0; i < layer.viewCount; i++) {
                if (proj0) {
                    views[i].fov = proj0->views[i].fov;
                    views[i].pose = proj0->views[i].pose;
                } else {
                    views[i].fov = projViews[i].fov;
                    views[i].pose = projViews[i].pose;
                }
                views[i].subImage.swapchain = m_passthroughLayerSwapchain;
                views[i].subImage.imageArrayIndex = i;
                views[i].subImage.imageRect.offset.x = views[i].subImage.imageRect.offset.y = 0;
                views[i].subImage.imageRect.extent.width = m_passthroughLayerSwapchainInfo.width;
                views[i].subImage.imageRect.extent.height = m_passthroughLayerSwapchainInfo.height;
            }

            layer.space = proj0 ? proj0->space : m_viewSpace;
            layer.layerFlags = 0;

            return true;
        }

        bool isConnected() const {
            return m_isConnected;
        }

      private:
        void createSwapchain() {
            // Determine what properties out swapchain must have.
            ZeroMemory(&m_passthroughLayerSwapchainInfo, sizeof(m_passthroughLayerSwapchainInfo));
            {
                uint32_t formatCount;
                CHECK_XRCMD(m_openXR.xrEnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr));
                std::vector<int64_t> formats(formatCount);
                CHECK_XRCMD(m_openXR.xrEnumerateSwapchainFormats(m_session, formatCount, &formatCount, formats.data()));

                m_passthroughLayerSwapchainInfo.format = formats[0];
            }
            {
                uint32_t viewCount;
                XrViewConfigurationView views[ViewCount] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr},
                                                            {XR_TYPE_VIEW_CONFIGURATION_VIEW, nullptr}};
                CHECK_XRCMD(m_openXR.xrEnumerateViewConfigurationViews(m_openXR.GetXrInstance(),
                                                                       m_systemId,
                                                                       XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                                       ViewCount,
                                                                       &viewCount,
                                                                       views));

                m_passthroughLayerSwapchainInfo.width = views[0].recommendedImageRectWidth;
                m_passthroughLayerSwapchainInfo.height = views[0].recommendedImageRectHeight;
            }

            m_passthroughLayerSwapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
            m_passthroughLayerSwapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            m_passthroughLayerSwapchainInfo.arraySize = 2;
            m_passthroughLayerSwapchainInfo.mipCount = 1;
            m_passthroughLayerSwapchainInfo.faceCount = 1;
            m_passthroughLayerSwapchainInfo.sampleCount = 1;

            // Create and import our swapchain images.
            CHECK_XRCMD(
                m_openXR.xrCreateSwapchain(m_session, &m_passthroughLayerSwapchainInfo, &m_passthroughLayerSwapchain));

            uint32_t imageCount;
            CHECK_XRCMD(m_openXR.xrEnumerateSwapchainImages(m_passthroughLayerSwapchain, 0, &imageCount, nullptr));
            if (!m_d3d12Device) {
                std::vector<XrSwapchainImageD3D11KHR> images(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR, nullptr});
                CHECK_XRCMD(
                    m_openXR.xrEnumerateSwapchainImages(m_passthroughLayerSwapchain,
                                                        imageCount,
                                                        &imageCount,
                                                        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));
                for (uint32_t i = 0; i < imageCount; i++) {
                    m_passthroughLayerTexture.push_back(images[i].texture);
                }
            } else {
                std::vector<XrSwapchainImageD3D12KHR> images(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR, nullptr});
                CHECK_XRCMD(
                    m_openXR.xrEnumerateSwapchainImages(m_passthroughLayerSwapchain,
                                                        imageCount,
                                                        &imageCount,
                                                        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));
                D3D11_RESOURCE_FLAGS flags;
                ZeroMemory(&flags, sizeof(flags));
                flags.BindFlags = D3D11_BIND_RENDER_TARGET;
                for (uint32_t i = 0; i < imageCount; i++) {
                    ComPtr<ID3D11Texture2D> interopTexture;

                    // Create the interop texture.
                    m_d3d11on12Device->CreateWrappedResource(images[i].texture,
                                                             &flags,
                                                             D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                             D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                             IID_PPV_ARGS(&interopTexture));
                    m_passthroughLayerTexture.push_back(interopTexture);
                }
            }

            // Create render target views.
            for (uint32_t i = 0; i < m_passthroughLayerTexture.size(); i++) {
                D3D11_RENDER_TARGET_VIEW_DESC desc;
                ZeroMemory(&desc, sizeof(desc));
                desc.Format = (DXGI_FORMAT)m_passthroughLayerSwapchainInfo.format;
                desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                desc.Texture2DArray.ArraySize = 2;
                desc.Texture2DArray.MipSlice = D3D11CalcSubresource(0, 0, m_passthroughLayerSwapchainInfo.mipCount);

                ComPtr<ID3D11RenderTargetView> rtv;
                CHECK_HRCMD(m_d3d11Device->CreateRenderTargetView(m_passthroughLayerTexture[i].Get(), &desc, &rtv));
                m_passthroughLayerRenderTarget.push_back(rtv);
            }
        }

        void beginSwapchainContext() {
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, nullptr};
            CHECK_XRCMD(
                m_openXR.xrAcquireSwapchainImage(m_passthroughLayerSwapchain, &acquireInfo, &m_swapchainImageIndex));

            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr};
            waitInfo.timeout = XR_INFINITE_DURATION;
            CHECK_XRCMD(m_openXR.xrWaitSwapchainImage(m_passthroughLayerSwapchain, &waitInfo));
        }

        void endSwapchainContext() {
            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, nullptr};
            CHECK_XRCMD(m_openXR.xrReleaseSwapchainImage(m_passthroughLayerSwapchain, &releaseInfo));
        }

        void beginDrawContext() {
            const bool isPureD3D11 = !m_d3d12Device;

            if (isPureD3D11) {
                // With D3D11, Use a deferred context so we can use the context saving feature.
                CHECK_HRCMD(m_d3d11Device->CreateDeferredContext(0, &m_currentContext));
                m_currentContext->ClearState();
            } else {
                ID3D11Resource* interopResource[] = {m_passthroughLayerTexture[m_swapchainImageIndex].Get()};
                m_d3d11on12Device->AcquireWrappedResources(interopResource, 1);

                m_currentContext = m_d3d11DeviceContext;
            }

            CD3D11_VIEWPORT viewport(0.0f,
                                     0.0f,
                                     (float)m_passthroughLayerSwapchainInfo.width,
                                     (float)m_passthroughLayerSwapchainInfo.height);
            m_currentContext->RSSetViewports(1, &viewport);
            ID3D11RenderTargetView* rtv[] = {m_passthroughLayerRenderTarget[m_swapchainImageIndex].Get()};
            m_currentContext->OMSetRenderTargets(1, rtv, nullptr);
        }

        void endDrawContext() {
            const bool isPureD3D11 = !m_d3d12Device;

            if (isPureD3D11) {
                // Dispatch the deferred context.
                ComPtr<ID3D11CommandList> commandList;
                CHECK_HRCMD(m_currentContext->FinishCommandList(FALSE, commandList.GetAddressOf()));

                m_d3d11DeviceContext->ExecuteCommandList(commandList.Get(), TRUE);
            } else {
                ID3D11Resource* interopResource[] = {m_passthroughLayerTexture[m_swapchainImageIndex].Get()};
                m_d3d11on12Device->ReleaseWrappedResources(interopResource, 1);

                // Flush to the D3D12 command queue.
                m_currentContext->Flush();
            }

            m_currentContext = nullptr;
        }

        OpenXrApi& m_openXR;
        const XrSystemId m_systemId;
        ComPtr<ID3D11Device> m_d3d11Device;
        ComPtr<ID3D11DeviceContext> m_d3d11DeviceContext;
        ComPtr<ID3D12Device> m_d3d12Device;
        ComPtr<ID3D12CommandQueue> m_d3d12CommandQueue;
        ComPtr<ID3D12Fence> m_d3d12Fence;
        ComPtr<ID3D11On12Device> m_d3d11on12Device;

        XrSession m_session;
        bool m_isConnected{false};

        ComPtr<ID3D11DeviceContext> m_currentContext;

        XrSwapchainCreateInfo m_passthroughLayerSwapchainInfo;
        XrSwapchain m_passthroughLayerSwapchain{XR_NULL_HANDLE};
        uint32_t m_swapchainImageIndex;
        std::vector<ComPtr<ID3D11Texture2D>> m_passthroughCameraTexture;
        std::vector<ComPtr<ID3D11Texture2D>> m_passthroughLayerTexture;
        std::vector<ComPtr<ID3D11RenderTargetView>> m_passthroughLayerRenderTarget;

        XrSpace m_viewSpace{XR_NULL_HANDLE};
    };

    class OpenXrLayer : public passthrough::OpenXrApi {
      public:
        OpenXrLayer() = default;
        ~OpenXrLayer() override = default;

        XrResult xrCreateInstance(const XrInstanceCreateInfo* createInfo) override {
            // Needed to resolve the requested function pointers.
            OpenXrApi::xrCreateInstance(createInfo);

            // Dump the OpenXR runtime information to help debugging customer issues.
            XrInstanceProperties instanceProperties = {XR_TYPE_INSTANCE_PROPERTIES, nullptr};
            CHECK_XRCMD(xrGetInstanceProperties(GetXrInstance(), &instanceProperties));
            const auto runtimeName = fmt::format("{} {}.{}.{}",
                                                 instanceProperties.runtimeName,
                                                 XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
                                                 XR_VERSION_MINOR(instanceProperties.runtimeVersion),
                                                 XR_VERSION_PATCH(instanceProperties.runtimeVersion));
            Log("Using OpenXR runtime %s\n", runtimeName.c_str());

            return XR_SUCCESS;
        }

        XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) override {
            const XrResult result = OpenXrApi::xrGetSystem(instance, getInfo, systemId);
            if (XR_SUCCEEDED(result) && getInfo->formFactor == XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
                // Remember the XrSystemId to use.
                m_vrSystemId = *systemId;
            }

            return result;
        }

        XrResult xrEnumerateEnvironmentBlendModes(XrInstance instance,
                                                  XrSystemId systemId,
                                                  XrViewConfigurationType viewConfigurationType,
                                                  uint32_t environmentBlendModeCapacityInput,
                                                  uint32_t* environmentBlendModeCountOutput,
                                                  XrEnvironmentBlendMode* environmentBlendModes) override {
            XrResult result = OpenXrApi::xrEnumerateEnvironmentBlendModes(instance,
                                                                          systemId,
                                                                          viewConfigurationType,
                                                                          environmentBlendModeCapacityInput,
                                                                          environmentBlendModeCountOutput,
                                                                          environmentBlendModes);
            if (XR_SUCCEEDED(result) && isVrSystem(systemId) &&
                viewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
                // Advertise XR_ENVIRONMENT_BLEND_MODE_ADDITIVE.
                if (environmentBlendModes) {
                    if (environmentBlendModeCapacityInput >= *environmentBlendModeCountOutput + 1) {
                        environmentBlendModes[*environmentBlendModeCountOutput] = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
                    } else {
                        result = XR_ERROR_SIZE_INSUFFICIENT;
                    }
                }
                *environmentBlendModeCountOutput = *environmentBlendModeCountOutput + 1;
            }

            return result;
        }

        XrResult xrCreateSession(XrInstance instance,
                                 const XrSessionCreateInfo* createInfo,
                                 XrSession* session) override {
            const XrResult result = OpenXrApi::xrCreateSession(instance, createInfo, session);
            if (XR_SUCCEEDED(result) && isVrSystem(createInfo->systemId)) {
                // Get the graphics device.
                const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(createInfo->next);
                while (entry) {
                    if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
                        const XrGraphicsBindingD3D11KHR* d3dBindings =
                            reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(entry);
                        m_graphicsResources =
                            std::make_unique<GraphicsResources>(*this, m_vrSystemId, d3dBindings->device);
                        break;
                    } else if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
                        const XrGraphicsBindingD3D12KHR* d3dBindings =
                            reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(entry);
                        m_graphicsResources = std::make_unique<GraphicsResources>(
                            *this, m_vrSystemId, d3dBindings->device, d3dBindings->queue);
                        break;
                    }

                    entry = entry->next;
                }

                if (!m_graphicsResources) {
                    Log("Unsupported graphics runtime.\n");
                }

                // Remember the XrSession to use.
                m_vrSession = *session;
            }

            return result;
        }

        XrResult xrDestroySession(XrSession session) override {
            const XrResult result = OpenXrApi::xrDestroySession(session);
            if (XR_SUCCEEDED(result) && isVrSession(session) && m_graphicsResources) {
                m_graphicsResources.reset();
                m_vrSession = XR_NULL_HANDLE;
            }

            return result;
        }

        XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) override {
            if (!isVrSession(session) || !m_graphicsResources ||
                !(frameEndInfo->environmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE ||
                  frameEndInfo->environmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND)) {
                return OpenXrApi::xrEndFrame(session, frameEndInfo);
            }

            // If this is the first frame and we are going to use passthrough, initialize the resources needed.
            if (!m_graphicsResources->isConnected()) {
                m_graphicsResources->connect(m_vrSession);
            }

            const XrCompositionLayerProjection* proj0 = nullptr;
            for (uint32_t i = 0; i < frameEndInfo->layerCount; i++) {
                if (frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && !proj0) {
                    proj0 = reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[i]);
                }

                // TODO: This is not compliant. We must make copies of the struct to patch them.
                ((XrCompositionLayerBaseHeader*)frameEndInfo->layers[i])->layerFlags =
                    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            }

            // Because the frame info is passed const, we are going to need to reconstruct a writable version of it
            // to add our extra layer.
            XrFrameEndInfo chainFrameEndInfo = *frameEndInfo;
            std::vector<const XrCompositionLayerBaseHeader*> layers;

            XrCompositionLayerProjection passthroughLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION, nullptr};
            XrCompositionLayerProjectionView passthroughLayerViews[ViewCount]{
                {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW, nullptr},
                {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW, nullptr}};

            if (m_graphicsResources->isConnected()) {
                passthroughLayer.viewCount = ViewCount;
                passthroughLayer.views = passthroughLayerViews;

                // Draw the camera layer.
                if (m_graphicsResources->drawPassthroughLayer(passthroughLayer, frameEndInfo->displayTime, proj0)) {
                    // Add the camera layer to the composition.
                    layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&passthroughLayer));
                }

                for (uint32_t i = 0; i < frameEndInfo->layerCount; i++) {
                    layers.push_back(frameEndInfo->layers[i]);
                }
                chainFrameEndInfo.layerCount = (uint32_t)layers.size();
                chainFrameEndInfo.layers = layers.data();

                // Restore the supported blending mode.
                chainFrameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            }

            return OpenXrApi::xrEndFrame(session, &chainFrameEndInfo);
        }

      private:
        bool isVrSystem(XrSystemId systemId) const {
            return systemId == m_vrSystemId;
        }

        bool isVrSession(XrSession session) const {
            return session == m_vrSession;
        }

        XrSystemId m_vrSystemId{XR_NULL_SYSTEM_ID};
        XrSession m_vrSession{XR_NULL_HANDLE};

        std::unique_ptr<GraphicsResources> m_graphicsResources;
    };

    std::unique_ptr<OpenXrLayer> g_instance = nullptr;

} // namespace

namespace passthrough {
    OpenXrApi* GetInstance() {
        if (!g_instance) {
            g_instance = std::make_unique<OpenXrLayer>();
        }
        return g_instance.get();
    }

    void ResetInstance() {
        g_instance.reset();
    }

} // namespace passthrough

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

// This file includes many code snippets from https://github.com/catid/XRmonitors
// Copyright (c) 2020, Christopher A. Taylor
// Copyright 2019 Augmented Perception Corporation

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
    using namespace DirectX;

    // These values are taken as-is from XRmonitors\XRmonitorsHologram\CameraCalibration.hpp
    struct HeadsetCameraCalibration {
        float K1 = -0.65f;
        float K2 = 0.f;
        float Scale = 1.9f;
        float OffsetX = 0.241f;
        float OffsetY = -0.178f;
        float RightOffsetY = 0.f;
        float EyeCantX = -0.391003f;
        float EyeCantY = -0.504997f;
        float EyeCantZ = 0.012f;
    };

    struct VertexPositionTexture {
        XMFLOAT3 position;
        XMFLOAT2 textureCoordinate;
    };

    struct ModelViewProjectionConstantBuffer {
        XMFLOAT4X4 modelViewProjection;
    };

    struct ColorAdjustmentConstantBuffer {
        XMFLOAT4 colorAdjustment;
    };

    // This code is adapted from XRmonitors\XRmonitorsHologram\CameraRenderer.cpp
    const std::string_view VertexShaderSource = R"_(
struct Vertex {
    float3 pos : POSITION;
    float2 tex : TEXCOORD0;
};

struct PSVertex {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

cbuffer ModelViewProjectionConstantBuffer : register(b0) {
    float4x4 modelViewProjection;
};

PSVertex vsMain(Vertex input) {
    PSVertex output;
    output.pos = mul(float4(input.pos, 1), modelViewProjection);

    // Place it behind everything else
    output.pos.z = 0.9999f * output.pos.w;

    output.tex = input.tex;
    return output;
}
)_";

    // This code is adapted from XRmonitors\XRmonitorsHologram\CameraRenderer.cpp
    const std::string_view PixelShaderSource = R"_(
struct PSVertex {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

cbuffer ColorAdjustmentConstantBuffer : register(b0) {
    float4 colorAdjustment;
};

SamplerState textureSampler : register(s0);
Texture2D cameraTexture : register(t0);

float4 psMain(PSVertex input) : SV_TARGET {
    float4 color = cameraTexture.Sample(textureSampler, input.Tex);
    return float4(
        color.r * colorAdjustment.r,
        color.r * colorAdjustment.g,
        color.r * colorAdjustment.b,
        1.0);
}
)_";

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

            for (uint32_t i = 0; i < ViewCount; i++) {
                m_passthroughLayerRenderTarget[i].clear();
            }
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

            // Connect to the camera service.
            m_cameraClient = createCameraClientWrapper();

            // Allocate a swapchain for the camera layer.
            createSwapchain();

            // Allocate resources for drawing the camera layer.
            createDrawingResources();

            m_isConnected = true;
        }

        bool drawPassthroughLayer(XrCompositionLayerProjection& layer,
                                  XrTime displayTime,
                                  const XrCompositionLayerProjection* proj0) {
            assert(layer.viewCount == ViewCount);

            // Check if we have a camera image.
            core::CameraFrame cameraFrame;
            const bool frameAcquired = m_cameraClient->AcquireNextFrame(cameraFrame);
            if (!m_passthroughCameraTexture && (!frameAcquired || cameraFrame.Width == 0)) {
                if (frameAcquired) {
                    m_cameraClient->ReleaseFrame();
                }

                // We don't even have a previous image to show.
                return false;
            }

            XrView projViews[ViewCount] = {{XR_TYPE_VIEW, nullptr}, {XR_TYPE_VIEW, nullptr}};
            NearFar nearFar{0.001f, 100.f};
            if (proj0) {
                const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(proj0->views[0].next);
                while (entry) {
                    if (entry->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR) {
                        const XrCompositionLayerDepthInfoKHR* depth =
                            reinterpret_cast<const XrCompositionLayerDepthInfoKHR*>(entry);
                        nearFar.Near = depth->nearZ;
                        nearFar.Far = depth->farZ;
                        break;
                    }
                    entry = entry->next;
                }
            } else {
                XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO, nullptr};
                locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                locateInfo.space = m_viewSpace;
                locateInfo.displayTime = displayTime;

                XrViewState state{XR_TYPE_VIEW_STATE, nullptr};
                uint32_t viewCount;
                CHECK_XRCMD(m_openXR.xrLocateViews(m_session, &locateInfo, &state, ViewCount, &viewCount, projViews));
                if (!Pose::IsPoseValid(state.viewStateFlags)) {
                    m_cameraClient->ReleaseFrame();
                    return false;
                }
            }

            // Draw the camera layer.
            beginSwapchainContext();
            beginDrawContext();

            // Import the texture from the camera service.
            // TODO: Workaround to bad image. We will just show the previous image.
            if (cameraFrame.Width > 0) {
                ensurePassthroughCameraResources(cameraFrame);
                updatePassthroughCameraTexture(cameraFrame);
            }
            m_cameraClient->ReleaseFrame();

            // Setup the common rendering state.
            m_currentContext->IASetInputLayout(m_inputLayout.Get());
            m_currentContext->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
            m_currentContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_currentContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
            m_currentContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
            {
                ID3D11Buffer* cbs[] = {m_colorAdjustmentConstantBuffer.Get()};
                m_currentContext->PSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);
            }
            {
                ID3D11SamplerState* samplers[] = {m_sampler.Get()};
                m_currentContext->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);
            };
            {
                ID3D11ShaderResourceView* srvs[] = {m_passthroughCameraResourceView.Get()};
                m_currentContext->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
            };

            for (uint32_t eye = 0; eye < ViewCount; eye++) {
                // Update the viewer's projection.
                {
                    ModelViewProjectionConstantBuffer modelViewProjection;
                    updateModelViewProjection(modelViewProjection,
                                              eye,
                                              proj0 ? proj0->views[eye].pose : projViews[eye].pose,
                                              proj0 ? proj0->views[eye].fov : projViews[eye].fov,
                                              nearFar);
                    m_d3d11DeviceContext->UpdateSubresource(
                        m_modelViewProjectionConstantBuffer[eye].Get(), 0, nullptr, &modelViewProjection, 0, 0);
                }

                // Setup per-eye rendering state.
                {
                    ID3D11RenderTargetView* rtv[] = {m_passthroughLayerRenderTarget[eye][m_swapchainImageIndex].Get()};
                    m_currentContext->OMSetRenderTargets(1, rtv, nullptr);
                }
                {
                    ID3D11Buffer* vbs[] = {m_vertexBuffer[eye].Get()};
                    const UINT strides[] = {sizeof(VertexPositionTexture)};
                    const UINT offsets[] = {0};
                    m_currentContext->IASetVertexBuffers(0, ARRAYSIZE(vbs), vbs, strides, offsets);
                }
                {
                    ID3D11Buffer* cbs[] = {m_modelViewProjectionConstantBuffer[eye].Get()};
                    m_currentContext->VSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);
                }

                // Draw the screen.
                m_currentContext->DrawIndexed(m_indexBufferNumIndices, 0, 0);
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
            m_passthroughLayerSwapchainInfo.arraySize = ViewCount;
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
            for (uint32_t eye = 0; eye < ViewCount; eye++) {
                for (uint32_t i = 0; i < m_passthroughLayerTexture.size(); i++) {
                    D3D11_RENDER_TARGET_VIEW_DESC desc;
                    ZeroMemory(&desc, sizeof(desc));
                    desc.Format = (DXGI_FORMAT)m_passthroughLayerSwapchainInfo.format;
                    desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                    desc.Texture2DArray.ArraySize = 1;
                    desc.Texture2DArray.FirstArraySlice = eye;
                    desc.Texture2DArray.MipSlice = D3D11CalcSubresource(0, 0, m_passthroughLayerSwapchainInfo.mipCount);

                    ComPtr<ID3D11RenderTargetView> rtv;
                    CHECK_HRCMD(m_d3d11Device->CreateRenderTargetView(m_passthroughLayerTexture[i].Get(), &desc, &rtv));
                    m_passthroughLayerRenderTarget[eye].push_back(rtv);
                }
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

        void createDrawingResources() {
            {
                ComPtr<ID3DBlob> errors;
                ComPtr<ID3DBlob> vsBytes;
                HRESULT hr = D3DCompile(VertexShaderSource.data(),
                                        VertexShaderSource.length(),
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        "vsMain",
                                        "vs_5_0",
                                        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS,
                                        0,
                                        &vsBytes,
                                        &errors);
                if (FAILED(hr)) {
                    if (errors) {
                        Log("%s", (char*)errors->GetBufferPointer());
                    }
                    CHECK_HRESULT(hr, "Failed to compile shader");
                }
                CHECK_HRCMD(m_d3d11Device->CreateVertexShader(
                    vsBytes->GetBufferPointer(), vsBytes->GetBufferSize(), nullptr, &m_vertexShader));

                const D3D11_INPUT_ELEMENT_DESC desc[] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                };

                CHECK_HRCMD(m_d3d11Device->CreateInputLayout(
                    desc, ARRAYSIZE(desc), vsBytes->GetBufferPointer(), vsBytes->GetBufferSize(), &m_inputLayout));
            }
            {
                ComPtr<ID3DBlob> errors;
                ComPtr<ID3DBlob> psBytes;
                HRESULT hr = D3DCompile(PixelShaderSource.data(),
                                        PixelShaderSource.length(),
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        "psMain",
                                        "ps_5_0",
                                        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS,
                                        0,
                                        &psBytes,
                                        &errors);
                if (FAILED(hr)) {
                    if (errors) {
                        Log("%s", (char*)errors->GetBufferPointer());
                    }
                    CHECK_HRESULT(hr, "Failed to compile shader");
                }
                CHECK_HRCMD(m_d3d11Device->CreatePixelShader(
                    psBytes->GetBufferPointer(), psBytes->GetBufferSize(), nullptr, &m_pixelShader));
            }
            {
                D3D11_SAMPLER_DESC desc;
                ZeroMemory(&desc, sizeof(desc));
                desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
                desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.MaxAnisotropy = 1;
                desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
                CHECK_HRCMD(m_d3d11Device->CreateSamplerState(&desc, &m_sampler));
            }
            {
                std::vector<VertexPositionTexture> vertices[ViewCount];
                std::vector<uint16_t> indices;

                generateMesh(m_passthroughCameraCalibrations.K1, m_passthroughCameraCalibrations.K2, vertices, indices);

                D3D11_BUFFER_DESC desc;
                ZeroMemory(&desc, sizeof(desc));
                desc.Usage = D3D11_USAGE_IMMUTABLE;

                D3D11_SUBRESOURCE_DATA data;
                ZeroMemory(&data, sizeof(data));
                desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                for (uint32_t eye = 0; eye < ViewCount; eye++) {
                    desc.ByteWidth = (UINT)vertices[eye].size() * sizeof(VertexPositionTexture);
                    data.pSysMem = vertices[eye].data();
                    CHECK_HRCMD(m_d3d11Device->CreateBuffer(&desc, &data, &m_vertexBuffer[eye]));
                }

                desc.ByteWidth = (UINT)indices.size() * sizeof(uint16_t);
                desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
                data.pSysMem = indices.data();
                CHECK_HRCMD(m_d3d11Device->CreateBuffer(&desc, &data, &m_indexBuffer));

                m_indexBufferNumIndices = (UINT)indices.size();
            }
            {
                D3D11_BUFFER_DESC desc;
                ZeroMemory(&desc, sizeof(desc));
                desc.ByteWidth = (UINT)sizeof(ModelViewProjectionConstantBuffer);
                desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                for (uint32_t eye = 0; eye < ViewCount; eye++) {
                    CHECK_HRCMD(m_d3d11Device->CreateBuffer(&desc, nullptr, &m_modelViewProjectionConstantBuffer[eye]));
                }
            }
            {
                D3D11_BUFFER_DESC desc;
                ZeroMemory(&desc, sizeof(desc));
                desc.ByteWidth = (UINT)sizeof(ColorAdjustmentConstantBuffer);
                desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

                ColorAdjustmentConstantBuffer colorAdjustment;
                colorAdjustment.colorAdjustment.x = 1.f;
                colorAdjustment.colorAdjustment.y = 1.f;
                colorAdjustment.colorAdjustment.z = 1.f;
                colorAdjustment.colorAdjustment.w = 1.f;

                D3D11_SUBRESOURCE_DATA initialData;
                ZeroMemory(&initialData, sizeof(initialData));
                initialData.pSysMem = &colorAdjustment;

                CHECK_HRCMD(m_d3d11Device->CreateBuffer(&desc, &initialData, &m_colorAdjustmentConstantBuffer));
            }
        }

        void ensurePassthroughCameraResources(core::CameraFrame& frame) {
            if (!m_passthroughCameraTexture || m_passthroughCameraTextureDesc.Width != frame.Width ||
                m_passthroughCameraTextureDesc.Height != frame.Height) {
                ZeroMemory(&m_passthroughCameraTextureDesc, sizeof(m_passthroughCameraTextureDesc));
                m_passthroughCameraTextureDesc.Format = DXGI_FORMAT_R8_UNORM;
                m_passthroughCameraTextureDesc.Width = frame.Width;
                m_passthroughCameraTextureDesc.Height = frame.Height;
                m_passthroughCameraTextureDesc.ArraySize = 1;
                m_passthroughCameraTextureDesc.MipLevels = 1;
                m_passthroughCameraTextureDesc.SampleDesc.Count = 1;
                m_passthroughCameraTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                m_passthroughCameraTexture = nullptr;
                CHECK_HRCMD(m_d3d11Device->CreateTexture2D(
                    &m_passthroughCameraTextureDesc, nullptr, &m_passthroughCameraTexture));

                D3D11_TEXTURE2D_DESC stagingTextureDesc = m_passthroughCameraTextureDesc;
                stagingTextureDesc.BindFlags = 0;
                stagingTextureDesc.Usage = D3D11_USAGE_STAGING;
                stagingTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

                m_passthroughCameraStagingTexture = nullptr;
                CHECK_HRCMD(
                    m_d3d11Device->CreateTexture2D(&stagingTextureDesc, nullptr, &m_passthroughCameraStagingTexture));

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
                ZeroMemory(&srvDesc, sizeof(srvDesc));
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Format = m_passthroughCameraTextureDesc.Format;
                srvDesc.Texture2D.MipLevels = 1;

                m_passthroughCameraResourceView = nullptr;
                CHECK_HRCMD(m_d3d11Device->CreateShaderResourceView(
                    m_passthroughCameraTexture.Get(), &srvDesc, &m_passthroughCameraResourceView));
            }
        }

        void updatePassthroughCameraTexture(core::CameraFrame& frame) {
            // This code is taken nearly as-is from XRmonitors\XRmonitorsHologram\CameraImager.cpp
            const UINT subresourceIndex = D3D11CalcSubresource(0, 0, 1);

            D3D11_MAPPED_SUBRESOURCE subresource;
            ZeroMemory(&subresource, sizeof(subresource));

            CHECK_HRCMD(m_d3d11DeviceContext->Map(
                m_passthroughCameraStagingTexture.Get(), subresourceIndex, D3D11_MAP_WRITE, 0, &subresource));

            uint8_t* dest = reinterpret_cast<uint8_t*>(subresource.pData);
            const unsigned pitch = subresource.RowPitch;

            // HACK: Remove 32 byte tags from the image
            const unsigned offset = 23264 + 1312 - 32;
            unsigned nextTagOffset = offset - 1312 + 32;

            unsigned brightSumCount = 0;
            int brightSum = 0;

            auto image = frame.CameraImage;
            for (unsigned i = 0; i < frame.Height; ++i) {
                if (nextTagOffset < frame.Width) {
                    memcpy(dest, image, nextTagOffset);
                    image += 32;
                    memcpy(dest + nextTagOffset, image + nextTagOffset, frame.Width - nextTagOffset);
                    nextTagOffset = offset - (frame.Width - nextTagOffset);
                } else {
                    memcpy(dest, image, frame.Width);
                    nextTagOffset -= frame.Width;

                    for (unsigned j = 0; j < frame.Width; j += 32) {
                        brightSum += image[j];
                    }
                    ++brightSumCount;
                }
                image += frame.Width;
                dest += pitch;
            }

            m_d3d11DeviceContext->Unmap(m_passthroughCameraStagingTexture.Get(), subresourceIndex);

            // TODO: Reject bad images. We will just show the previous image.
            int brightAvg = brightSum / brightSumCount * 20;
            if (brightAvg < m_lastAcceptedBright / 4 && m_frameSkipped < 7) {
                m_frameSkipped++;
                return;
            }
            m_lastAcceptedBright = brightAvg;
            m_frameSkipped = 0;

            m_d3d11DeviceContext->CopyResource(m_passthroughCameraTexture.Get(),
                                               m_passthroughCameraStagingTexture.Get());
        }

        void updateModelViewProjection(ModelViewProjectionConstantBuffer& modelViewProjection,
                                       uint32_t eyeIndex,
                                       const XrPosef eyePose,
                                       const XrFovf fov,
                                       const NearFar& nearFar) {
            // This code is adapted from XRmonitors\XRmonitorsHologram\CameraRenderer.cpp
            const XMMATRIX modelScale = XMMatrixScaling(m_passthroughCameraCalibrations.Scale,
                                                        m_passthroughCameraCalibrations.Scale,
                                                        m_passthroughCameraCalibrations.Scale);

            const XMMATRIX translateMatrix[ViewCount] = {
                XMMatrixTranslation(-m_passthroughCameraCalibrations.OffsetX,
                                    m_passthroughCameraCalibrations.OffsetY -
                                        m_passthroughCameraCalibrations.RightOffsetY,
                                    0.f),
                XMMatrixTranslation(m_passthroughCameraCalibrations.OffsetX,
                                    m_passthroughCameraCalibrations.OffsetY +
                                        m_passthroughCameraCalibrations.RightOffsetY,
                                    0.f),
            };

            const XMMATRIX rotateMatrix[ViewCount] = {
                XMMatrixRotationRollPitchYaw(m_passthroughCameraCalibrations.EyeCantX,
                                             -m_passthroughCameraCalibrations.EyeCantY,
                                             -m_passthroughCameraCalibrations.EyeCantZ),
                XMMatrixRotationRollPitchYaw(m_passthroughCameraCalibrations.EyeCantX,
                                             m_passthroughCameraCalibrations.EyeCantY,
                                             m_passthroughCameraCalibrations.EyeCantZ),
            };

            const XMMATRIX modelOrientation = XMMatrixRotationQuaternion(LoadXrQuaternion(eyePose.orientation));
            const XMMATRIX modelTranslation =
                XMMatrixTranslation(eyePose.position.x, eyePose.position.y, eyePose.position.z);
            const XMMATRIX distTranslation = XMMatrixTranslation(0.f, 0.f, -1.f);

            const XMMATRIX transform = XMMatrixMultiply(
                rotateMatrix[eyeIndex],
                XMMatrixMultiply(
                    translateMatrix[eyeIndex],
                    XMMatrixMultiply(
                        modelScale,
                        XMMatrixMultiply(distTranslation, XMMatrixMultiply(modelOrientation, modelTranslation)))));

            const XMVECTOR position = LoadXrVector3(eyePose.position);
            XMVECTOR orientation = XMVector4Normalize(LoadXrQuaternion(eyePose.orientation));

            const float vq = 0.0002f;

            const uint32_t rv = wellons_triple32(m_nextJitterSeed += 1337);
            const float xr = vq * (uint32_t)(rv & 0xffff) / 65536.f;
            const float yr = vq * (uint32_t)(rv >> 16) / 65536.f;

            const float xm = fmodf(orientation.m128_f32[0], vq);
            const float ym = fmodf(orientation.m128_f32[1], vq);

            orientation.m128_f32[0] = orientation.m128_f32[0] - xm + xr;
            orientation.m128_f32[1] = orientation.m128_f32[1] - ym + yr;

            const XMVECTOR invertOrientation = XMQuaternionConjugate(orientation);
            const XMVECTOR invertPosition = XMVector3Rotate(-position, invertOrientation);

            const XMMATRIX spaceToView = XMMatrixAffineTransformation(g_XMOne,           // scale
                                                                      g_XMZero,          // rotation origin
                                                                      invertOrientation, // rotation
                                                                      invertPosition);   // translation

            const XMMATRIX projectionMatrix = ComposeProjectionMatrix(fov, nearFar);
            const XMMATRIX viewProjectionMatrix = XMMatrixMultiply(spaceToView, projectionMatrix);

            XMStoreFloat4x4(&modelViewProjection.modelViewProjection,
                            XMMatrixTranspose(transform * viewProjectionMatrix));
        }

        static void
        generateMesh(float k1, float k2, std::vector<VertexPositionTexture>* vertices, std::vector<uint16_t>& indices) {
            // This code is taken nearly as-is from XRmonitors\XRmonitorsHologram\CameraRenderer.cpp
            for (uint32_t eye = 0; eye < ViewCount; eye++) {
                vertices[eye].clear();
            }
            indices.clear();

            const float aspect = 640.f / 480.f;
            const unsigned width = 20;
            const unsigned pitch = width + 1;
            const unsigned height = 20;
            const float dx = 1.f / width;
            const float dy = 1.f / height;

            unsigned lr_index = 0;

            const float inv_height = 1.f / height;
            const float inv_width = 1.f / width;

            for (unsigned y = 0; y <= height; ++y) {
                float yf = y * inv_height;
                float v_y = yf - 0.5f;
                float v = 1.f - yf;

                for (unsigned x = 0; x <= width; ++x) {
                    float xf = x * inv_width;
                    float v_x = xf - 0.5f;
                    float u = xf;

                    float s, t;
                    warpVertex(k1, k2, v_x * aspect, v_y, s, t);

                    VertexPositionTexture vertex;
                    vertex.position.x = s;
                    vertex.position.y = t;
                    vertex.position.z = 0.f;

                    const float border = 0.005f;

                    vertex.textureCoordinate.x = u * (0.5f - border);
                    vertex.textureCoordinate.y = v;

                    vertices[0].push_back(vertex);

                    vertex.textureCoordinate.x = (0.5f + border) + u * (0.5f - border);
                    vertex.textureCoordinate.y = v;

                    vertices[1].push_back(vertex);

                    if (x > 0 && y > 0) {
                        indices.push_back(lr_index - pitch);
                        indices.push_back(lr_index - pitch - 1);
                        indices.push_back(lr_index - 1);

                        indices.push_back(lr_index);
                        indices.push_back(lr_index - pitch);
                        indices.push_back(lr_index - 1);
                    }

                    ++lr_index;
                }
            }
        }

        static void warpVertex(float k1, float k2, float u, float v, float& s, float& t) {
            // This code is taken as-is from XRmonitors\XRmonitorsHologram\CameraRenderer.cpp
            const float r_sqr = u * u + v * v;
            const float r_sqr2 = r_sqr * r_sqr;
            const float k_inv = 1.f / (1.f + k1 * r_sqr + k2 * r_sqr2);

            s = u * k_inv;
            t = v * k_inv;
        }

        static uint32_t wellons_triple32(uint32_t x) {
            // This code is taken as-is from XRmonitors\core\include\core_bit_math.hpp
            x ^= x >> 17;
            x *= UINT32_C(0xed5ad4bb);
            x ^= x >> 11;
            x *= UINT32_C(0xac4c1b51);
            x ^= x >> 15;
            x *= UINT32_C(0x31848bab);
            x ^= x >> 14;
            return x;
        }

        OpenXrApi& m_openXR;
        const XrSystemId m_systemId;

        // Direct3D device resources.
        ComPtr<ID3D11Device> m_d3d11Device;
        ComPtr<ID3D11DeviceContext> m_d3d11DeviceContext;
        ComPtr<ID3D12Device> m_d3d12Device;
        ComPtr<ID3D12CommandQueue> m_d3d12CommandQueue;
        ComPtr<ID3D12Fence> m_d3d12Fence;
        ComPtr<ID3D11On12Device> m_d3d11on12Device;
        ComPtr<ID3D11DeviceContext> m_currentContext;

        // Swapchain resources.
        XrSwapchainCreateInfo m_passthroughLayerSwapchainInfo;
        XrSwapchain m_passthroughLayerSwapchain{XR_NULL_HANDLE};
        uint32_t m_swapchainImageIndex;
        std::vector<ComPtr<ID3D11Texture2D>> m_passthroughLayerTexture;
        std::vector<ComPtr<ID3D11RenderTargetView>> m_passthroughLayerRenderTarget[ViewCount];

        // Camera service resources.
        std::unique_ptr<ICameraClientWrapper> m_cameraClient;
        D3D11_TEXTURE2D_DESC m_passthroughCameraTextureDesc;
        ComPtr<ID3D11Texture2D> m_passthroughCameraTexture;
        ComPtr<ID3D11Texture2D> m_passthroughCameraStagingTexture;
        HeadsetCameraCalibration m_passthroughCameraCalibrations;
        int m_lastAcceptedBright{0};
        uint32_t m_frameSkipped{0};
        uint32_t m_nextJitterSeed{0};

        // Drawing resources.
        ComPtr<ID3D11InputLayout> m_inputLayout;
        ComPtr<ID3D11VertexShader> m_vertexShader;
        ComPtr<ID3D11PixelShader> m_pixelShader;
        ComPtr<ID3D11SamplerState> m_sampler;
        ComPtr<ID3D11Buffer> m_vertexBuffer[ViewCount];
        ComPtr<ID3D11Buffer> m_indexBuffer;
        ComPtr<ID3D11Buffer> m_modelViewProjectionConstantBuffer[ViewCount];
        ComPtr<ID3D11Buffer> m_colorAdjustmentConstantBuffer;
        UINT m_indexBufferNumIndices;

        ComPtr<ID3D11ShaderResourceView> m_passthroughCameraResourceView;

        // Misc OpenXR resources.
        XrSpace m_viewSpace{XR_NULL_HANDLE};

        XrSession m_session;
        bool m_isConnected{false};
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

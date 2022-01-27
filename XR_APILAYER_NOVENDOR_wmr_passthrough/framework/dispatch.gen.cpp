// *********** THIS FILE IS GENERATED - DO NOT EDIT ***********
// MIT License
//
// Copyright(c) 2021-2022 Matthieu Bucchianeri
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

#include "pch.h"

#include <layer.h>

#include "dispatch.h"
#include "log.h"

#ifndef LAYER_NAMESPACE
#error Must define LAYER_NAMESPACE
#endif

using namespace LAYER_NAMESPACE::log;

namespace LAYER_NAMESPACE
{

	// Auto-generated wrappers for the requested APIs.

	XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId)
	{
		DebugLog("--> xrGetSystem\n");

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrGetSystem(instance, getInfo, systemId);
		}
		catch (std::exception exc)
		{
			Log("%s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}

		DebugLog("<-- xrGetSystem %d\n", result);

		return result;
	}

	XrResult xrEnumerateEnvironmentBlendModes(XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType, uint32_t environmentBlendModeCapacityInput, uint32_t* environmentBlendModeCountOutput, XrEnvironmentBlendMode* environmentBlendModes)
	{
		DebugLog("--> xrEnumerateEnvironmentBlendModes\n");

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigurationType, environmentBlendModeCapacityInput, environmentBlendModeCountOutput, environmentBlendModes);
		}
		catch (std::exception exc)
		{
			Log("%s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}

		DebugLog("<-- xrEnumerateEnvironmentBlendModes %d\n", result);

		return result;
	}

	XrResult xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session)
	{
		DebugLog("--> xrCreateSession\n");

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrCreateSession(instance, createInfo, session);
		}
		catch (std::exception exc)
		{
			Log("%s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}

		DebugLog("<-- xrCreateSession %d\n", result);

		return result;
	}

	XrResult xrDestroySession(XrSession session)
	{
		DebugLog("--> xrDestroySession\n");

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrDestroySession(session);
		}
		catch (std::exception exc)
		{
			Log("%s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}

		DebugLog("<-- xrDestroySession %d\n", result);

		return result;
	}

	XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo)
	{
		DebugLog("--> xrEndFrame\n");

		XrResult result;
		try
		{
			result = LAYER_NAMESPACE::GetInstance()->xrEndFrame(session, frameEndInfo);
		}
		catch (std::exception exc)
		{
			Log("%s\n", exc.what());
			result = XR_ERROR_RUNTIME_FAILURE;
		}

		DebugLog("<-- xrEndFrame %d\n", result);

		return result;
	}


	// Auto-generated dispatcher handler.
	XrResult OpenXrApi::xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function)
	{
		XrResult result = m_xrGetInstanceProcAddr(instance, name, function);

		if (XR_SUCCEEDED(result))
		{
			const std::string apiName(name);

			if (apiName == "xrDestroyInstance")
			{
				m_xrDestroyInstance = reinterpret_cast<PFN_xrDestroyInstance>(*function);
				*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrDestroyInstance);
			}
			else if (apiName == "xrGetSystem")
			{
				m_xrGetSystem = reinterpret_cast<PFN_xrGetSystem>(*function);
				*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrGetSystem);
			}
			else if (apiName == "xrEnumerateEnvironmentBlendModes")
			{
				m_xrEnumerateEnvironmentBlendModes = reinterpret_cast<PFN_xrEnumerateEnvironmentBlendModes>(*function);
				*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrEnumerateEnvironmentBlendModes);
			}
			else if (apiName == "xrCreateSession")
			{
				m_xrCreateSession = reinterpret_cast<PFN_xrCreateSession>(*function);
				*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrCreateSession);
			}
			else if (apiName == "xrDestroySession")
			{
				m_xrDestroySession = reinterpret_cast<PFN_xrDestroySession>(*function);
				*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrDestroySession);
			}
			else if (apiName == "xrEndFrame")
			{
				m_xrEndFrame = reinterpret_cast<PFN_xrEndFrame>(*function);
				*function = reinterpret_cast<PFN_xrVoidFunction>(LAYER_NAMESPACE::xrEndFrame);
			}

		}

		return result;
	}

	// Auto-generated create instance handler.
	XrResult OpenXrApi::xrCreateInstance(const XrInstanceCreateInfo* createInfo)
    {
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrGetInstanceProperties", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrGetInstanceProperties))))
		{
			throw new std::runtime_error("Failed to resolve xrGetInstanceProperties");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrCreateReferenceSpace", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrCreateReferenceSpace))))
		{
			throw new std::runtime_error("Failed to resolve xrCreateReferenceSpace");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrDestroySpace", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrDestroySpace))))
		{
			throw new std::runtime_error("Failed to resolve xrDestroySpace");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrEnumerateViewConfigurationViews", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrEnumerateViewConfigurationViews))))
		{
			throw new std::runtime_error("Failed to resolve xrEnumerateViewConfigurationViews");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrEnumerateSwapchainFormats", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrEnumerateSwapchainFormats))))
		{
			throw new std::runtime_error("Failed to resolve xrEnumerateSwapchainFormats");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrCreateSwapchain", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrCreateSwapchain))))
		{
			throw new std::runtime_error("Failed to resolve xrCreateSwapchain");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrDestroySwapchain", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrDestroySwapchain))))
		{
			throw new std::runtime_error("Failed to resolve xrDestroySwapchain");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrEnumerateSwapchainImages", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrEnumerateSwapchainImages))))
		{
			throw new std::runtime_error("Failed to resolve xrEnumerateSwapchainImages");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrAcquireSwapchainImage", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrAcquireSwapchainImage))))
		{
			throw new std::runtime_error("Failed to resolve xrAcquireSwapchainImage");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrWaitSwapchainImage", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrWaitSwapchainImage))))
		{
			throw new std::runtime_error("Failed to resolve xrWaitSwapchainImage");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrReleaseSwapchainImage", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrReleaseSwapchainImage))))
		{
			throw new std::runtime_error("Failed to resolve xrReleaseSwapchainImage");
		}
		if (XR_FAILED(m_xrGetInstanceProcAddr(m_instance, "xrLocateViews", reinterpret_cast<PFN_xrVoidFunction*>(&m_xrLocateViews))))
		{
			throw new std::runtime_error("Failed to resolve xrLocateViews");
		}
		m_applicationName = createInfo->applicationInfo.applicationName;
		return XR_SUCCESS;
	}

} // namespace LAYER_NAMESPACE


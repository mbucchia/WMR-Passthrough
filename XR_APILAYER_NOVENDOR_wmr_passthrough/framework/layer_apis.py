# The list of OpenXR functions our layer will override.
override_functions = [
    "xrGetSystem",
    "xrEnumerateEnvironmentBlendModes",
    "xrCreateSession",
    "xrDestroySession",
    "xrEndFrame"
]

# The list of OpenXR functions our layer will use from the runtime.
# Might repeat entries from override_functions above.
requested_functions = [
    "xrGetInstanceProperties",
    "xrEnumerateSwapchainFormats",
    "xrEnumerateViewConfigurationViews",
    "xrCreateReferenceSpace",
    "xrDestroySpace",
    "xrCreateSwapchain",
    "xrDestroySwapchain",
    "xrEnumerateSwapchainImages",
    "xrAcquireSwapchainImage",
    "xrWaitSwapchainImage",
    "xrReleaseSwapchainImage",
    "xrLocateViews"
]

# Windows Mixed Reality Passthrough for OpenXR

An OpenXR API layer for Windows Mixed Reality to tranparently add video passthrough capabilities to compatible OpenXR applications.

This is achieved by offering the `XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND` environment blend mode to the application, then inserting a projection layer behind all the layers submitted by the application.

DISCLAIMER: This software is distributed as-is, without any warranties or conditions of any kind. Use at your own risks.

## Limitations

This is very experimental code that is not yet ready for production.

- It works with Windows Mixed Reality headsets that embark 2 cameras only, such as the Acer AH-101 or the HP Reverb (1st generation)
  - It currently **does not work** with the HP Reverb G2
- Latency is considerable (measurement TBD)

It has been successully tested with:

- [StereoKit](https://stereokit.net/) applications
- Unity applications

## Prerequisites

1) Install the [XRmonitors](https://github.com/catid/XRmonitors/releases) application. This application hosts the camera server that enables reading images from the headset's cameras.

2) Try out XRmonitors to ensure that everything works properly (you can use virtual monitors in VR while seeing your surroundings, which is pretty cool!).

**NOTE:**: Please read and honor the [License](https://github.com/catid/XRmonitors/blob/master/LICENSE.md) of the project, specifically:

> This software is free for individual use but not for business use with more than 50 employees. Please contact me for licensing at support@xrmonitors.com for now.

## Usage

1) Build the `WMR-Passthrough.sln` project.

2) Set the environment variable `XR_ENABLE_API_LAYERS` to the value `XR_APILAYER_NOVENDOR_wmr_passthrough`.

3) Set the environment variable `XR_API_LAYER_PATH` to point to the folder where your API layer is built (typically `WMR-Passthrough\bin\x64\Release`).

4) Run your application!

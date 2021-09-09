# (WIP) camera to ncnn to native window end-2-end

This is a sample ncnn android project, it depends on ncnn library only: https://github.com/Tencent/ncnn

## feature

GPU is a suitable device for image processing and machine learning workflow. For realtime processing, we shall try best to eliminate all possible overheads involing the inefficient CPU-GPU data copy. `VK_ANDROID_external_memory_android_hardware_buffer` is a new vulkan extension that allows us to access the on-device camera captured frame pixels on Android platform. This project propose a sample project to emerge an easy to use high-level api for Android vulkan and ncnn interop. In addition, testing and documentation is a MUST to keep maintainability.

(WIP) The vulkan android hardwarebuffer is still work in progress in this fork: https://github.com/yyangoO/ncnn


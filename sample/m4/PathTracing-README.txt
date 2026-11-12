RenderLab path tracing experiment

Requirements: Windows x64, current Microsoft Visual C++ x64 runtime and a DX12 or Vulkan
GPU/driver with ray tracing pipeline, ray query and acceleration structure support.

From this directory, launch a window in PowerShell:
  .\renderlab_player.exe --backend dx12 --project project/PathTracingGallery.rlproject --pipeline pathtracing

Replace dx12 with vulkan to use Vulkan. With the render window focused, WASD moves the
camera, Q/E move vertically, and the arrow keys turn. The image progressively
accumulates while the camera is still. Use --frames 120 to exit automatically. For an offline screenshot:
  .\renderlab_player.exe --backend dx12 --headless --project project/PathTracingGallery.rlproject --pipeline pathtracing --width 640 --height 360 --benchmark 256 --warmup-frames 0 --output results

The screenshot is results/dx12/scene.bmp. First launch builds a local project/Library
shader cache. The renderer supports opaque built-in Lit/Unlit triangles and a maximum
of four path segments. Spatial display denoising is on by default; add --denoise off
to view the unfiltered accumulation. Denoising can blur details and suppress real
highlights. Transparent/masked surfaces, textured-emitter light sampling and dynamic
geometry are not supported. Noise, fixed-offset artifacts and finite-path bias remain.
The output is an experimental reference, not a finished editor.

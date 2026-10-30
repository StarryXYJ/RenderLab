using System.Runtime.InteropServices;

namespace RenderLab.Editor.Interop;

[StructLayout(LayoutKind.Sequential)]
internal struct ViewportConfig
{
    public uint Size, Version;
    public ulong RequestId;
    public uint Pipeline, Submission, RecordingThreads, AsyncCompute, MemoryAlias, View;
    public uint Profiling, TargetFps, Continuous, Flags, ShadowResolution;
    public float Exposure, CameraSpeed;
    public float LightX, LightY, LightZ, LightR, LightG, LightB, LightIntensity;
    public float ShadowBias, ShadowNormalBias, EnvironmentIntensity, EnvironmentRotation;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ViewportConfigState
{
    public uint Size, Version;
    public ulong RequestedId, AppliedId, RejectedId, SupersededId;
    public uint Status, Ready;
    public ViewportConfig Requested, Applied;
}

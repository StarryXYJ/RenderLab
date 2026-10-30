using System.ComponentModel;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RenderLab.Editor.Interop;
using RenderLab.Editor.Services;

namespace RenderLab.Editor.ViewModels;

public sealed record CapabilityChoice(string Name, bool Available = true, string Reason = "");

public sealed partial class RenderSettingsViewModel : ObservableObject, IDisposable
{
    private readonly NativeViewportService viewport;
    private bool reflecting;
    private string seenCapabilities = "";
    private static readonly HashSet<string> Editable = [nameof(Submission), nameof(RecordingThreads), nameof(AsyncCompute), nameof(MemoryAlias),
        nameof(View), nameof(Profiling), nameof(TargetFps), nameof(Continuous), nameof(Reflections), nameof(History), nameof(Denoise), nameof(Temporal),
        nameof(Shadows), nameof(ShadowResolution), nameof(CameraSpeed), nameof(LightX), nameof(LightY), nameof(LightZ), nameof(LightR), nameof(LightG),
        nameof(LightB), nameof(LightIntensity), nameof(ShadowBias), nameof(ShadowNormalBias), nameof(EnvironmentIntensity), nameof(EnvironmentRotation)];
    public IReadOnlyList<string> Views { get; } = ["Final", "HDR", "Shadow", "Environment", "BRDF", "Reflection raw", "Reflection history", "Reflection denoised"];
    public IReadOnlyList<int> ShadowResolutions { get; } = [128, 256, 512, 1024, 2048];
    [ObservableProperty] private IReadOnlyList<CapabilityChoice> pipelines = [new("Forward"), new("Deferred"), new("RayTracing")];
    [ObservableProperty] private IReadOnlyList<CapabilityChoice> submissions = [new("Direct"), new("Indexed Direct"), new("CPU Indirect"), new("GPU Culled")];
    [ObservableProperty] private bool ready;
    [ObservableProperty] private string status = "Initializing configuration…";
    [ObservableProperty] private string appliedSummary = "Waiting for the first submitted frame…";
    [ObservableProperty] private string capabilityText = "Waiting for device capabilities…";
    [ObservableProperty] private string error = "";
    [ObservableProperty] private bool asyncSupported;
    [ObservableProperty] private int submission, view;
    [ObservableProperty] private int recordingThreads = 1, targetFps = 60;
    [ObservableProperty] private int shadowResolution = 1024;
    [ObservableProperty] private bool asyncCompute, memoryAlias, profiling, continuous;
    [ObservableProperty] private bool reflections = true, history = true, denoise = true, temporal = true, shadows;
    [ObservableProperty] private double cameraSpeed = 3;
    [ObservableProperty] private double lightX, lightY, lightZ = 1, lightR = 1, lightG = 1, lightB = 1, lightIntensity;
    [ObservableProperty] private double shadowBias, shadowNormalBias, environmentIntensity = 1, environmentRotation;
    public RenderSettingsViewModel(NativeViewportService viewport)
    {
        this.viewport = viewport;
        viewport.ConfigurationUpdated += Reflect;
    }
    protected override void OnPropertyChanged(PropertyChangedEventArgs args)
    {
        base.OnPropertyChanged(args);
        if (!reflecting && Ready && args.PropertyName is { } name && Editable.Contains(name))
        {
            Execute(() => viewport.Submit(BuildConfiguration()));
        }
    }
    internal ViewportConfig BuildConfiguration()
    {
        var c = viewport.EditableConfig;
        c.Submission = (uint)Submission; c.View = (uint)View; c.RecordingThreads = (uint)RecordingThreads;
        c.AsyncCompute = AsyncCompute ? 1u : 0; c.MemoryAlias = MemoryAlias ? 1u : 0;
        c.Profiling = Profiling ? 1u : 0; c.TargetFps = (uint)TargetFps; c.Continuous = Continuous ? 1u : 0;
        c.Flags = (c.Flags & 32u) | (Reflections ? 1u : 0) | (History ? 2u : 0) | (Denoise ? 4u : 0) | (Temporal ? 8u : 0) | (Shadows ? 16u : 0);
        c.ShadowResolution = (uint)ShadowResolution; c.CameraSpeed = (float)CameraSpeed;
        c.LightX = (float)LightX; c.LightY = (float)LightY; c.LightZ = (float)LightZ;
        c.LightR = (float)LightR; c.LightG = (float)LightG; c.LightB = (float)LightB; c.LightIntensity = (float)LightIntensity;
        c.ShadowBias = (float)ShadowBias; c.ShadowNormalBias = (float)ShadowNormalBias;
        c.EnvironmentIntensity = (float)EnvironmentIntensity; c.EnvironmentRotation = (float)EnvironmentRotation;
        return c;
    }
    private void Execute(Action action)
    {
        try { action(); Error = ""; }
        catch (Exception error)
        {
            Reflect(); Error = error.Message;
            Program.Logging?.Logger.Warning(error, "Runtime configuration was not accepted");
        }
    }
    private void Reflect()
    {
        reflecting = true;
        try
        {
            var state = viewport.Configuration;
            Ready = state.Ready != 0;
            if (!Ready) { Status = "Viewport closed / initializing"; AppliedSummary = "No active submitted frame"; return; }
            var c = viewport.EditableConfig;
            Status = state.Status switch
            {
                2 => $"Applied #{state.AppliedId}",
                3 => $"Rejected #{state.RejectedId} · applied #{state.AppliedId}",
                _ => $"Pending #{state.RequestedId} · applied #{state.AppliedId} (resume or step if paused)"
            };
            Error = viewport.ConfigurationError;
            AppliedSummary = state.Applied.Size == 112
                ? $"Actual #{state.AppliedId}: {Pipelines[(int)state.Applied.Pipeline - 1].Name} / {Submissions[(int)state.Applied.Submission].Name}\n" +
                  $"Exposure {state.Applied.Exposure:F2} · {state.Applied.RecordingThreads} threads · {(state.Applied.TargetFps == 0 ? "uncapped" : $"{state.Applied.TargetFps} FPS cap")}"
                : "Waiting for the first submitted frame…";
            Submission = (int)c.Submission; View = (int)c.View; RecordingThreads = (int)c.RecordingThreads;
            AsyncCompute = c.AsyncCompute != 0; MemoryAlias = c.MemoryAlias != 0; Profiling = c.Profiling != 0;
            TargetFps = (int)c.TargetFps; Continuous = c.Continuous != 0;
            Reflections = (c.Flags & 1) != 0; History = (c.Flags & 2) != 0; Denoise = (c.Flags & 4) != 0;
            Temporal = (c.Flags & 8) != 0; Shadows = (c.Flags & 16) != 0;
            ShadowResolution = (int)c.ShadowResolution; CameraSpeed = c.CameraSpeed;
            LightX = c.LightX; LightY = c.LightY; LightZ = c.LightZ;
            LightR = c.LightR; LightG = c.LightG; LightB = c.LightB; LightIntensity = c.LightIntensity;
            ShadowBias = c.ShadowBias; ShadowNormalBias = c.ShadowNormalBias;
            EnvironmentIntensity = c.EnvironmentIntensity; EnvironmentRotation = c.EnvironmentRotation;
            if (seenCapabilities != viewport.Capabilities && viewport.Capabilities.Length > 0)
            {
                seenCapabilities = viewport.Capabilities;
                using var json = JsonDocument.Parse(seenCapabilities);
                var root = json.RootElement;
                if (root.GetProperty("schemaVersion").GetInt32() != 1) { throw new InvalidDataException("Unsupported device capability schema."); }
                AsyncSupported = root.GetProperty("asyncCompute").GetBoolean();
                CapabilityChoice[] Choices(string key, string[] names) => root.GetProperty(key).EnumerateArray().Select((entry, index) =>
                    new CapabilityChoice(names[index], entry.GetProperty("available").GetBoolean(), entry.GetProperty("reason").GetString() ?? "")).ToArray();
                Pipelines = Choices("pipelines", ["Forward", "Deferred", "RayTracing"]);
                Submissions = Choices("submissions", ["Direct", "Indexed Direct", "CPU Indirect", "GPU Culled"]);
                CapabilityText = $"{root.GetProperty("adapter").GetString()}\nDriver {root.GetProperty("driver").GetString()}\n" +
                    $"Indexed: {root.GetProperty("instanceCapacity").GetInt32()} instances / {root.GetProperty("textureCapacity").GetInt32()} textures\n" +
                    string.Join("\n", Pipelines.Concat(Submissions).Where(choice => !choice.Available).Select(choice => $"{choice.Name}: {choice.Reason}")) +
                    (AsyncSupported ? "" : "\nAsync compute queue unavailable");
            }
        }
        finally { reflecting = false; }
    }
    [RelayCommand] private void Reset() => Execute(viewport.ResetConfiguration);
    [RelayCommand] private void SavePreset() => Execute(viewport.SavePreset);
    [RelayCommand] private void LoadPreset() => Execute(viewport.LoadPreset);
    public void Dispose() => viewport.ConfigurationUpdated -= Reflect;
}

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RenderLab.Editor.Services;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Interop;
using RenderLab.Editor.Diagnostics;
using System.Diagnostics;

namespace RenderLab.Editor.ViewModels;

public sealed partial class MainWindowViewModel : ObservableObject, IDisposable
{
    private readonly NativeViewportService viewport;
    private bool restoringSettings;
    private readonly RefreshGate refresh = new();
    public string Title { get; }
    public string BackendName { get; }
    public IReadOnlyList<string> Pipelines { get; } = ["Forward", "Deferred", "Ray Tracing"];
    public string PauseLabel => IsPaused ? "Resume viewport" : "Pause viewport";

    [ObservableProperty]
    private int selectedPipelineIndex = 1;

    [ObservableProperty]
    private double exposure;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(PauseLabel))]
    private bool isPaused;

    [ObservableProperty]
    private string statusText = "Starting native viewport…";

    [ObservableProperty]
    private string detailsText = "NativeControlHost / render thread";

    [ObservableProperty]
    private string errorText = "";

    [ObservableProperty]
    private string performanceText = "Waiting for frame timings…";

    public MainWindowViewModel(NativeViewportService viewport, EditorOptions options)
    {
        this.viewport = viewport;
        SelectedPipelineIndex = (int)options.Pipeline - 1;
        Title = $"RenderLab · Avalonia Host · {options.Backend.ToUpperInvariant()}";
        BackendName = options.Backend == "dx12" ? "Direct3D 12" : "Vulkan";
        viewport.StatsUpdated += UpdateStats;
        viewport.Failed += ShowFailure;
    }

    partial void OnSelectedPipelineIndexChanged(int oldValue, int newValue)
    {
        if (restoringSettings)
        {
            return;
        }
        try
        {
            viewport.Configure((uint)(newValue + 1), (float)Exposure);
            ErrorText = "";
        }
        catch (Exception error)
        {
            restoringSettings = true;
            SelectedPipelineIndex = oldValue;
            restoringSettings = false;
            ShowFailure(error);
        }
    }

    partial void OnExposureChanged(double oldValue, double newValue)
    {
        if (restoringSettings)
        {
            return;
        }
        try
        {
            viewport.Configure((uint)(SelectedPipelineIndex + 1), (float)newValue);
            ErrorText = "";
        }
        catch (Exception error)
        {
            restoringSettings = true;
            Exposure = oldValue;
            restoringSettings = false;
            ShowFailure(error);
        }
    }

    partial void OnIsPausedChanged(bool value) => viewport.IsPaused = value;

    [RelayCommand]
    private void TogglePause() => IsPaused = !IsPaused;

    [RelayCommand]
    private void ResetCamera() => Execute(() => viewport.Configure(viewport.Pipeline, viewport.Exposure, true));

    [RelayCommand]
    private void RecreateViewport() => Execute(viewport.RequestRecreate);

    private void Execute(Action operation)
    {
        try
        {
            operation();
            ErrorText = "";
        }
        catch (Exception error)
        {
            ShowFailure(error);
        }
    }

    private void ShowFailure(Exception error) => ErrorText = error.Message;

    private void UpdateStats(ViewportStats stats)
    {
        if (!refresh.TryPublish(Stopwatch.GetTimestamp() / (double)Stopwatch.Frequency))
        {
            return;
        }
        var summary = viewport.Timings.Summarize();
        var gpu = summary.GpuP95Ms is { } gpuMs ? $"{gpuMs:F2} ms" : stats.ProfilingEnabled == 0 ? "off" : stats.GpuFrame > 0 ? $"{stats.GpuNs / 1e6:F2} ms (frame {stats.GpuFrame})" : "pending";
        var activity = stats.Activity switch { 0 => "Starting", 2 => "Idle", 3 => "Paused", 4 => "Resizing", _ => $"{summary.Fps:F1} FPS" };
        StatusText = $"{activity} · active frame P95 {summary.FrameP95Ms:F1} ms · {stats.Width} × {stats.Height} px · frame {stats.Frames} · RT {stats.ReflectionSamples} spp";
        PerformanceText = $"Active frame P95\nNative: {summary.NativeP95Ms:F2} ms\nFence: {summary.FenceWaitP95Ms:F2} ms\nAcquire: {summary.AcquireP95Ms:F2} ms\nRender: {summary.RenderP95Ms:F2} ms\nCPU prepare: {summary.ScenePrepareP95Ms:F2} ms\nPresent: {summary.PresentP95Ms:F2} ms\nGPU passes: {gpu}\nValidation: {(stats.ValidationEnabled != 0 ? "on" : "off")}";
        DetailsText = $"NativeControlHost\nC ABI v{viewport.AbiVersion} / render thread\nUI: {stats.UiThreadId} · render: {stats.RenderThreadId}\n\nClient: {stats.Width} × {stats.Height} px\nScale: {stats.Dpi / 96.0:F2}\nResizes: {stats.Resizes}\nDropped samples: {stats.DroppedSamples}\nNative: {stats.NativeBytes / 1048576.0:F1} MiB\n\nFocus: {stats.Focused}\nCapture: {stats.Captured}";
    }

    public void Dispose()
    {
        viewport.StatsUpdated -= UpdateStats;
        viewport.Failed -= ShowFailure;
    }
}

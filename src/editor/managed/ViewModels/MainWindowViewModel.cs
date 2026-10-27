using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RenderLab.Editor.Host.Services;

namespace RenderLab.Editor.Host.ViewModels;

public sealed partial class MainWindowViewModel : ObservableObject, IDisposable
{
    private readonly NativeViewportService viewport;
    private bool restoringSettings;
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
    private string detailsText = "NativeControlHost / UI STA";

    [ObservableProperty]
    private string errorText = "";

    public MainWindowViewModel(NativeViewportService viewport, HostOptions options)
    {
        this.viewport = viewport;
        Title = $"RenderLab · Avalonia Host · {options.Backend.ToUpperInvariant()}";
        BackendName = options.Backend == "dx12" ? "Direct3D 12" : "Vulkan";
        viewport.FrameCompleted += UpdateStats;
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
        if (stats.Frames % 8 != 0 && stats.PausedTicks == 0)
        {
            return;
        }
        StatusText = $"{stats.Width} × {stats.Height} px   ·   DPI {stats.Dpi}   ·   frame {stats.Frames}   ·   RT {stats.ReflectionSamples} spp";
        DetailsText = $"NativeControlHost\nC ABI v{viewport.AbiVersion} / UI STA\n\nClient: {stats.Width} × {stats.Height} px\nScale: {stats.Dpi / 96.0:F2}\nResizes: {stats.Resizes}\nPaused ticks: {stats.PausedTicks}\nNative: {stats.NativeBytes / 1048576.0:F1} MiB\n\nFocus: {stats.Focused}\nCapture: {stats.Captured}";
    }

    public void Dispose()
    {
        viewport.FrameCompleted -= UpdateStats;
        viewport.Failed -= ShowFailure;
    }
}

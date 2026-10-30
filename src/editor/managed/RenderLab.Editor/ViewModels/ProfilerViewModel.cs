using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RenderLab.Editor.Diagnostics;
using RenderLab.Editor.Services;

namespace RenderLab.Editor.ViewModels;

public sealed partial class ProfilerViewModel(NativeViewportService service, RenderGraphViewModel graphModel) : ObservableObject
{
    private readonly List<ProfilerSnapshot> history = [];
    private bool reflecting;
    private ulong historySession, historyConfig;
    internal ProfilerSnapshot? Current { get; private set; }
    internal int HistoryCount => history.Count;
    internal long HistoryBytes => history.Sum(snapshot => snapshot.Graph.Json.Length * 2L);
    internal FrameSample[] Curve { get; private set; } = [];
    internal event Action? CurveChanged;
    internal event Action? GraphRequested;
    public ObservableCollection<ProfilerPassRow> Passes { get; } = [];
    [ObservableProperty] private string summary = "Waiting for a completed diagnostic frame…", details = "", statistics = "", exportPath = "";
    [ObservableProperty] private bool frozen, continuous, gpuReadback;
    [ObservableProperty] private int selectedFrame = -1;
    [ObservableProperty] private string[] frameChoices = [];
    partial void OnContinuousChanged(bool value) => Configure(c => { c.Continuous = value ? 1u : 0u; return c; });
    partial void OnGpuReadbackChanged(bool value) => Configure(c => { c.Flags = value ? c.Flags | 32u : c.Flags & ~32u; return c; });
    partial void OnSelectedFrameChanged(int value)
    {
        if (!reflecting && Frozen && value >= 0 && value < history.Count) { Display(history[value]); }
    }
    private void Configure(Func<Interop.ViewportConfig, Interop.ViewportConfig> change)
    {
        if (reflecting) { return; }
        try { service.Submit(change(service.EditableConfig)); }
        catch (Exception error) { Details = error.Message; }
    }
    internal void Refresh()
    {
        if (Frozen) { return; }
        try
        {
            if (historySession != service.Session || historyConfig != service.Stats.ConfigRevision)
            {
                history.Clear(); Current = null; Passes.Clear(); FrameChoices = [];
                historySession = service.Session; historyConfig = service.Stats.ConfigRevision;
                Summary = "Waiting for this session/configuration to complete…";
                Details = "No completed snapshot for this source yet.";
            }
            reflecting = true;
            Continuous = service.EditableConfig.Continuous != 0; GpuReadback = (service.EditableConfig.Flags & 32) != 0;
            reflecting = false;
            var json = service.ReadSnapshot();
            if (json is not null)
            {
                var next = ProfilerSnapshot.Parse(json);
                if (next.Graph.Session == service.Session && next.Graph.Config == service.Stats.ConfigRevision &&
                    (history.Count == 0 || next.Graph.Frame > history[^1].Graph.Frame))
                {
                    Observe(next);
                }
            }
            Curve = service.Timings.Snapshot();
            var intervals = Curve.Where(sample => sample.IntervalMs.HasValue).Select(sample => sample.IntervalMs!.Value).Order().ToArray();
            double P(double percentile) => intervals.Length == 0 ? 0 : intervals[(int)Math.Ceiling(intervals.Length * percentile) - 1];
            var activity = service.Stats.Activity;
            Statistics = $"{(activity == 2 ? "Idle" : activity == 3 ? "Paused" : intervals.Length == 0 ? "FPS pending" : $"{1000 / intervals.Average():F1} FPS")} · {Curve.Length} completed-frame samples · dropped {service.Stats.DroppedSamples}\n" +
                $"Active intervals P50/P95/P99 {P(.50):F2} / {P(.95):F2} / {P(.99):F2} ms · metadata sampled ≤4 Hz / {history.Count} frames (64 / 8 MiB budget)";
            CurveChanged?.Invoke();
            service.RequestSnapshot();
        }
        catch (Exception error) { reflecting = false; Details = error.Message; }
    }
    private void Display(ProfilerSnapshot snapshot)
    {
        Current = snapshot; Summary = snapshot.Summary; Details = snapshot.Details;
        Passes.Clear(); foreach (var row in snapshot.Passes) { Passes.Add(row); }
    }
    internal void Observe(ProfilerSnapshot snapshot)
    {
        history.Add(snapshot);
        while (history.Count > 64 || HistoryBytes > 8 * 1024 * 1024) { history.RemoveAt(0); }
        reflecting = true;
        FrameChoices = history.Select(value => $"Frame {value.Graph.Frame} · config {value.Graph.Config}").ToArray();
        SelectedFrame = history.Count - 1;
        reflecting = false;
        Display(snapshot);
    }
    [RelayCommand]
    private void Clear()
    {
        history.Clear(); FrameChoices = []; Passes.Clear(); Current = null; service.Timings.Reset(); Curve = []; CurveChanged?.Invoke();
    }
    [RelayCommand]
    private void ShowGraph()
    {
        if (Current is not null) { graphModel.Inspect(Current.Graph); GraphRequested?.Invoke(); }
    }
    [RelayCommand]
    private void Export()
    {
        if (Current is not { } current) { return; }
        try { ExportPath = service.ExportSnapshot($"profile-{current.Graph.Session}-{current.Graph.Frame}", current.Graph.Json); }
        catch (Exception error) { ExportPath = error.Message; }
    }
}

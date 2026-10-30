using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RenderLab.Editor.Diagnostics;
using RenderLab.Editor.Services;

namespace RenderLab.Editor.ViewModels;

public sealed partial class RenderGraphViewModel(NativeViewportService service) : ObservableObject
{
    internal GraphSnapshot? Snapshot { get; private set; }
    private bool manualPending;
    internal GraphPass[] VisiblePasses => Snapshot?.Passes.Where(p => (ShowCulled || p.Live) &&
        (Matches(p.Name) || p.Accesses.Any(a => Snapshot.Resources.Any(r => r.Id == a.Resource && Matches(r.Name))))).Take(128).ToArray() ?? [];
    internal GraphResource[] VisibleResources
    {
        get
        {
            var passes = VisiblePasses;
            return Snapshot?.Resources.Where(r => Matches(r.Name) || passes.Any(p => Matches(p.Name) && p.Accesses.Any(a => a.Resource == r.Id))).Take(256).ToArray() ?? [];
        }
    }
    private bool Matches(string value) => string.IsNullOrWhiteSpace(Search) || value.Contains(Search, StringComparison.OrdinalIgnoreCase);
    [ObservableProperty] private string search = "";
    [ObservableProperty] private bool frozen, showCulled;
    [ObservableProperty] private double zoom = 1;
    [ObservableProperty] private string status = "Waiting for a submitted graph (resume or step if paused).";
    [ObservableProperty] private string details = "Select a pass/resource cell. R = read, W = write, RW = preserving write. Metadata only; no RT pixels are copied.";
    [ObservableProperty] private string exportPath = "";
    internal int SelectedPass { get; private set; } = -1;
    internal event Action? CanvasChanged;
    internal void Inspect(GraphSnapshot snapshot)
    {
        Frozen = true; Snapshot = snapshot;
        Status = $"Frozen from Profiler · {snapshot.Backend} · session {snapshot.Session} · frame {snapshot.Frame} · config {snapshot.Config} · topology {snapshot.Topology}";
        CanvasChanged?.Invoke();
    }
    internal void Refresh(bool request = true)
    {
        if (Frozen && !manualPending && request) { return; }
        if (!Frozen && Snapshot is { } old && (old.Session != service.Session || old.Config != service.Stats.ConfigRevision))
        { Snapshot = null; Status = "Waiting for the current session/configuration graph."; CanvasChanged?.Invoke(); }
        try
        {
            var json = service.ReadSnapshot();
            if (json is not null)
            {
                var next = GraphSnapshot.Parse(json);
                if (next.Session == service.Session && next.Config == service.Stats.ConfigRevision &&
                    (!manualPending || next.Frame != Snapshot?.Frame))
                {
                    Snapshot = next;
                    manualPending = false;
                    Status = $"{next.Backend} · session {next.Session} · frame {next.Frame} · config {next.Config} · topology {next.Topology}\n" +
                        $"{next.Passes.Count(p => p.Live)}/{next.Passes.Length} passes · {next.Resources.Length} resources · view limit 128 × 256 (search to refine)";
                    CanvasChanged?.Invoke();
                }
            }
            if (request && !Frozen) { service.RequestSnapshot(); }
        }
        catch (Exception error) { Status = error.Message; }
    }
    partial void OnSearchChanged(string value) => CanvasChanged?.Invoke();
    partial void OnShowCulledChanged(bool value) => CanvasChanged?.Invoke();
    partial void OnZoomChanged(double value) => CanvasChanged?.Invoke();
    [RelayCommand] private void RefreshNow() { manualPending = true; service.RequestSnapshot(); }
    [RelayCommand]
    private void Export()
    {
        if (Snapshot is not { } snapshot) { return; }
        try { ExportPath = service.ExportSnapshot($"graph-{snapshot.Session}-{snapshot.Frame}", snapshot.Json); }
        catch (Exception error) { ExportPath = error.Message; }
    }
    internal void Select(GraphPass pass, GraphResource? resource)
    {
        SelectedPass = pass.Id;
        var snapshot = Snapshot!;
        var edges = snapshot.Edges.Where(e => e.Before == pass.Id || e.After == pass.Id);
        var labels = snapshot.Passes.ToDictionary(p => p.Id, p => p.Name);
        var access = resource is null ? null : pass.Accesses.FirstOrDefault(a => a.Resource == resource.Id);
        Details = $"{pass.Name} · {pass.Kind} · {pass.Queue} · chunk {pass.Chunk?.ToString() ?? "N/A"}\n{pass.Source}\n{pass.Reason}\n" +
            (resource is null ? "" : $"\n{resource.Name}: {resource.Description}\nimport={resource.Imported}, export={resource.Exported}, versions={resource.Versions}, lifetime=[{resource.First}, {resource.Last}], alias={resource.Alias?.ToString() ?? "none"}, logical={resource.EstimatedBytes} B\n") +
            (access is null ? "" : $"{(access.Read ? "R" : "")}{(access.Write ? "W" : "")} · {access.UseName} · v{access.Input} → v{access.Output}\n{access.Range}\n") +
            string.Join("\n", edges.Select(e => $"{labels[e.Before]} → {labels[e.After]} : {e.Reason}"));
        CanvasChanged?.Invoke();
    }
}

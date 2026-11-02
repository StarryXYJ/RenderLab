using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RenderLab.Editor.Diagnostics;

namespace RenderLab.Editor.ViewModels;

public sealed partial class LogViewModel : ObservableObject
{
    private readonly LogBuffer buffer;
    private long revision = -1;
    public IReadOnlyList<string> Levels { get; } = ["All", "Warning + Error", "Error"];
    [ObservableProperty] private int levelIndex;
    [ObservableProperty] private string search = "";
    [ObservableProperty] private string source = "";
    [ObservableProperty] private bool autoScroll = true;
    [ObservableProperty] private IReadOnlyList<LogRow> rows = Array.Empty<LogRow>();
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasSelection))]
    [NotifyPropertyChangedFor(nameof(SelectedDetails))]
    private LogRow? selected;
    public bool HasSelection => Selected is not null;
    public string SelectedDetails => Selected is { } row ? row.Display + "\n\n" + row.Details : "";
    [ObservableProperty] private string counters = "";
    public LogViewModel(LogBuffer buffer) => this.buffer = buffer;
    partial void OnLevelIndexChanged(int value) => Refresh(true);
    partial void OnSearchChanged(string value) => Refresh(true);
    partial void OnSourceChanged(string value) => Refresh(true);
    internal void Refresh(bool force = false)
    {
        if (!force && revision == buffer.Revision) { return; }
        revision = buffer.Revision;
        var counts = buffer.Counts;
        Counters = $"Warnings {counts.Warnings} · Errors {counts.Errors} · Evicted {counts.Dropped}";
        var selectedId = Selected?.Id;
        Rows = buffer.Snapshot().Where(row => (LevelIndex == 0 || row.Level >=
                (LevelIndex == 1 ? Serilog.Events.LogEventLevel.Warning : Serilog.Events.LogEventLevel.Error)) &&
            row.Message.Contains(Search, StringComparison.OrdinalIgnoreCase) &&
            row.Source.Contains(Source, StringComparison.OrdinalIgnoreCase)).ToArray();
        Selected = Rows.FirstOrDefault(row => row.Id == selectedId);
    }
    [RelayCommand] private void Clear() { buffer.Clear(); Refresh(true); }
}

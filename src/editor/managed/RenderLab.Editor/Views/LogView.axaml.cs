using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Input.Platform;
using Avalonia.Threading;
using System.Diagnostics;
using RenderLab.Editor.Diagnostics;
using RenderLab.Editor.ViewModels;

namespace RenderLab.Editor.Views;

internal partial class LogView : UserControl
{
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(250) };
    public LogView()
    {
        InitializeComponent();
        timer.Tick += (_, _) =>
        {
            if (DataContext is LogViewModel model)
            {
                model.Refresh();
                if (model.AutoScroll && model.Rows.Count > 0) { LogList.ScrollIntoView(model.Rows[^1]); }
            }
        };
        AttachedToVisualTree += (_, _) => timer.Start();
        DetachedFromVisualTree += (_, _) => timer.Stop();
    }
    private async void CopyClicked(object? sender, RoutedEventArgs args)
    {
        if (DataContext is LogViewModel model && TopLevel.GetTopLevel(this)?.Clipboard is { } clipboard)
        {
            try { await clipboard.SetTextAsync(model.Selected is { } row ? row.Display + "\n" + row.Details : string.Join("\n", model.Rows.Select(r => r.Display))); }
            catch (Exception error) { Program.Logging?.Logger.Warning(error, "Copying log selection failed"); }
        }
    }
    private void DirectoryClicked(object? sender, RoutedEventArgs args)
    {
        try { Process.Start(new ProcessStartInfo(Program.Logging!.DirectoryPath) { UseShellExecute = true }); }
        catch (Exception error) { Program.Logging?.Logger.Warning(error, "Opening log directory failed"); }
    }
}

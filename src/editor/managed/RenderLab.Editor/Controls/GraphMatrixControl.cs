using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using RenderLab.Editor.ViewModels;
using System.Globalization;

namespace RenderLab.Editor.Controls;

/// <summary>Scrollable access matrix and selected-pass dependencies from the actual plan.</summary>
internal sealed class GraphMatrixControl : Control
{
    internal RenderGraphViewModel? Model { get; set; }
    private const double Header = 110, Row = 30, Label = 225, Cell = 44;
    internal void Refresh()
    {
        Width = Math.Max(700, Label + (Model?.VisibleResources.Length ?? 0) * Cell) * (Model?.Zoom ?? 1);
        var edgeCount = Model?.Snapshot?.Edges.Count(edge => edge.Before == Model.SelectedPass || edge.After == Model.SelectedPass) ?? 0;
        var lifetimeCount = Model?.VisibleResources.Count(resource => resource.First is not null) ?? 0;
        Height = (Header + (Model?.VisiblePasses.Length ?? 0) * Row + 90 +
            (Model?.SelectedPass >= 0 ? 50 + Math.Min(6, edgeCount) * 25 : 0) +
            Math.Min(16, lifetimeCount) * 23) * (Model?.Zoom ?? 1);
        InvalidateVisual();
    }
    private static void Text(DrawingContext context, string value, Point point, IBrush? brush = null, double size = 12)
    {
        context.DrawText(new FormattedText(value, CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
            Typeface.Default, size, brush ?? new SolidColorBrush(Color.Parse("#D6D6D6"))), point);
    }
    public override void Render(DrawingContext context)
    {
        base.Render(context);
        if (Model?.Snapshot is not { } snapshot) { return; }
        var passes = Model.VisiblePasses; var resources = Model.VisibleResources;
        using var transform = context.PushTransform(Matrix.CreateScale(Model.Zoom, Model.Zoom));
        Text(context, "RESOURCE ACCESS", new(10, 10), Brushes.Gray, 10);
        for (var column = 0; column < resources.Length; ++column)
        {
            var resource = resources[column];
            Text(context, $"R{resource.Id}", new(Label + column * Cell + 5, 35));
            var label = resource.Name.Length > 9 ? resource.Name[..9] : resource.Name;
            var x = Label + column * Cell;
            using (context.PushTransform(Matrix.CreateTranslation(-x, -90) * Matrix.CreateRotation(-Math.PI / 4) * Matrix.CreateTranslation(x, 90)))
            { Text(context, label, new(Label + column * Cell, 90), Brushes.Silver, 10); }
        }
        for (var row = 0; row < passes.Length; ++row)
        {
            var pass = passes[row]; var y = Header + row * Row;
            IBrush background = pass.Id == Model.SelectedPass ? new SolidColorBrush(Color.Parse("#354940")) : row % 2 == 0 ? new SolidColorBrush(Color.Parse("#292929")) : Brushes.Transparent;
            context.DrawRectangle(background, null, new Rect(0, y, Label + resources.Length * Cell, Row));
            var title = pass.Name.Length > 27 ? pass.Name[..27] + "…" : pass.Name;
            Text(context, $"{pass.Id}: {title}", new(8, y + 5), pass.Live ? Brushes.Gainsboro : Brushes.Gray);
            for (var column = 0; column < resources.Length; ++column)
            {
                var access = pass.Accesses.FirstOrDefault(a => a.Resource == resources[column].Id);
                if (access is null) { continue; }
                var x = Label + column * Cell;
                var color = access.Read && access.Write ? "#365B51" : access.Write ? "#655331" : "#344B5C";
                context.DrawRectangle(new SolidColorBrush(Color.Parse(color)), null, new Rect(x + 2, y + 3, Cell - 4, Row - 6), 3, 3);
                Text(context, access.Read && access.Write ? "RW" : access.Write ? "W" : "R", new(x + 10, y + 5));
            }
        }
        var top = Header + passes.Length * Row + 20;
        var edges = snapshot.Edges.Where(e => e.Before == Model.SelectedPass || e.After == Model.SelectedPass).Take(6).ToArray();
        var names = snapshot.Passes.ToDictionary(p => p.Id, p => p.Name);
        if (Model.SelectedPass >= 0) { Text(context, "DEPENDENCIES", new(10, top), Brushes.Gray, 10); }
        for (var index = 0; index < edges.Length; ++index)
        {
            var edge = edges[index]; var y = top + 30 + index * 25;
            Text(context, names[edge.Before], new(12, y));
            context.DrawLine(new Pen(Brushes.CadetBlue, 1), new(230, y + 10), new(285, y + 10));
            context.DrawLine(new Pen(Brushes.CadetBlue, 1), new(278, y + 5), new(285, y + 10));
            Text(context, names[edge.After] + " · " + edge.Reason, new(295, y));
        }
        if (Model.SelectedPass >= 0) { top += 50 + edges.Length * 25; }
        Text(context, "RESOURCE LIFETIMES", new(10, top), Brushes.Gray, 10);
        var scheduleLength = Math.Max(1, snapshot.Passes.Count(p => p.Live));
        var lifetimes = resources.Where(r => r.First is not null).Take(16).ToArray();
        for (var index = 0; index < lifetimes.Length; ++index)
        {
            var resource = lifetimes[index]; var y = top + 28 + index * 23;
            Text(context, $"R{resource.Id} [{resource.First}, {resource.Last}] alias {resource.Alias?.ToString() ?? "—"}", new(8, y));
            var first = resource.First!.Value;
            context.DrawRectangle(new SolidColorBrush(Color.Parse(resource.Alias.HasValue ? "#518F7C" : "#536D80")), null,
                new Rect(Label + first * 450.0 / scheduleLength, y + 2, Math.Max(4, (resource.Last - first + 1) * 450.0 / scheduleLength), 15));
        }
    }
    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        if (Model is null) { return; }
        var point = e.GetPosition(this) / Model.Zoom;
        var row = (int)Math.Floor((point.Y - Header) / Row);
        var column = (int)Math.Floor((point.X - Label) / Cell);
        var passes = Model.VisiblePasses; var resources = Model.VisibleResources;
        if (row >= 0 && row < passes.Length)
        { Model.Select(passes[row], column >= 0 && column < resources.Length ? resources[column] : null); }
        else if (Model.Snapshot is { } snapshot)
        {
            var index = (int)Math.Floor((point.Y - Header - passes.Length * Row - 50) / 25);
            var edges = snapshot.Edges.Where(edge => edge.Before == Model.SelectedPass || edge.After == Model.SelectedPass).Take(6).ToArray();
            if (index >= 0 && index < edges.Length)
            {
                var id = point.X < 260 ? edges[index].Before : edges[index].After;
                Model.Select(snapshot.Passes.First(pass => pass.Id == id), null);
            }
        }
    }
}

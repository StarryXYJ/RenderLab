using Avalonia;
using Avalonia.Controls;
using Orientation = Dock.Model.Core.Orientation;
using Dock.Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;
using Dock.Model.Mvvm.Core;
using Microsoft.Extensions.Logging;
using System.Text.Json;

namespace RenderLab.Editor.Docking;

internal sealed record DockPanelContent(string Id, Func<Control> CreateView);

/// <summary>Owns fixed panel identities and persists whitelisted layout values.</summary>
internal sealed class WorkbenchDockFactory : Factory
{
    private readonly Dictionary<string, Document> panels;
    private readonly ILogger logger;
    internal IRootDock Root { get; private set; } = null!;
    internal WorkbenchDockFactory(Dictionary<string, Func<Control>> views, ILogger logger)
    {
        this.logger = logger;
        panels = views.ToDictionary(p => p.Key, p => new Document
        {
            Id = p.Key,
            Title = p.Key == "Settings" ? "Render Settings" : p.Key,
            Context = new DockPanelContent(p.Key, p.Value),
            CanClose = true,
            CanFloat = true
        });
        DefaultHostWindowLocator = () => new HostWindow();
        DockableClosed += (_, args) => logger.LogInformation("Dock closed: {Panel}", args.Dockable?.Id);
        DockableMoved += (_, args) => logger.LogInformation("Dock moved: {Panel}", args.Dockable?.Id);
    }
    public override IRootDock CreateLayout()
    {
        var scene = Group("SceneGroup", "Scene");
        var settings = Group("SettingsGroup", "Settings");
        settings.Proportion = .26;
        scene.Proportion = .74;
        var body = new ProportionalDock
        {
            Id = "Body",
            Orientation = Orientation.Horizontal,
            VisibleDockables = CreateList<IDockable>(scene, new ProportionalDockSplitter(), settings)
        };
        return new RootDock
        {
            Id = "Root",
            VisibleDockables = CreateList<IDockable>(body),
            ActiveDockable = body,
            DefaultDockable = body
        };
    }
    private DocumentDock Group(string id, params string[] ids) => new()
    {
        Id = id,
        CanCreateDocument = false,
        EnableGlobalDocking = true,
        VisibleDockables = CreateList<IDockable>(ids.Select(value => panels[value]).ToArray()),
        ActiveDockable = ids.Length > 0 ? panels[ids[0]] : null
    };
    internal bool IsOpen(string id) => Walk(Root).Any(d => ReferenceEquals(d, panels[id]));
    internal Document Panel(string id) => panels[id];
    internal void Show(string id)
    {
        var panel = panels[id];
        if (!IsOpen(id))
        {
            var group = Walk(Root).OfType<DocumentDock>().FirstOrDefault(d => d.Id == "DiagnosticsGroup");
            if (group is null)
            {
                group = Group("DiagnosticsGroup");
                group.Proportion = .36;
                var sceneGroup = Walk(Root).OfType<DocumentDock>().FirstOrDefault(dock => dock.Id == "SceneGroup");
                var sceneOwner = sceneGroup is null ? null : Walk(Root).OfType<IDock>().FirstOrDefault(dock => dock.VisibleDockables?.Contains(sceneGroup) == true);
                if (sceneGroup is not null && sceneOwner?.VisibleDockables is { } siblings)
                {
                    var position = siblings.IndexOf(sceneGroup);
                    var proportion = sceneGroup.Proportion;
                    sceneGroup.Proportion = .64;
                    siblings[position] = new ProportionalDock
                    {
                        Id = "SceneWorkspace",
                        Orientation = Orientation.Vertical,
                        Proportion = proportion,
                        VisibleDockables = CreateList<IDockable>(sceneGroup, new ProportionalDockSplitter(), group)
                    };
                }
                else if (Root.VisibleDockables is not { Count: > 0 })
                {
                    Root.VisibleDockables = CreateList<IDockable>(group);
                }
                else
                {
                    var body = Root.VisibleDockables[0];
                    Root.VisibleDockables[0] = new ProportionalDock
                    {
                        Id = "Workspace",
                        Orientation = Orientation.Vertical,
                        VisibleDockables = CreateList<IDockable>(body, new ProportionalDockSplitter(), group)
                    };
                }
                Root.DefaultDockable = Root.VisibleDockables![0];
                Root.ActiveDockable = Root.DefaultDockable;
                InitLayout(Root);
            }
            AddDockable(group, panel);
        }
        SetActiveDockable(panel);
        SetFocusedDockable(Root, panel);
        logger.LogInformation("Dock shown: {Panel}", id);
    }
    internal void CloseFloatingWindows()
    {
        if (Root?.Windows is { } windows)
        {
            foreach (var window in windows.ToArray()) { window.Exit(); }
        }
    }
    internal void UseLayout(IRootDock layout) { Root = layout; InitLayout(layout); }
    internal static IEnumerable<IDockable> Walk(IDockable node)
    {
        yield return node;
        if (node is IDock dock && dock.VisibleDockables is { } children)
        {
            foreach (var child in children) { foreach (var value in Walk(child)) { yield return value; } }
        }
        if (node is IRootDock root && root.Windows is { } windows)
        {
            foreach (var window in windows)
            {
                if (window.Layout is { } layout) { foreach (var value in Walk(layout)) { yield return value; } }
            }
        }
    }
    internal sealed record Node(string Kind, string Id, double Proportion, bool Vertical, string? Active, Node[] Children);
    internal sealed record FloatLayout(double X, double Y, double Width, double Height, Node Root);
    internal sealed record SavedLayout(int Version, Node Root, FloatLayout[] Windows);
    private static Node SaveNode(IDockable value) => new(value switch
    {
        IRootDock => "Root",
        IProportionalDock => "Split",
        IProportionalDockSplitter => "Divider",
        IDocumentDock => "Tabs",
        IToolDock => "Tabs",
        _ => "Panel"
    }, value.Id, double.IsFinite(value.Proportion) ? value.Proportion : 0,
        value is IProportionalDock { Orientation: Orientation.Vertical }, (value as IDock)?.ActiveDockable?.Id,
        (value as IDock)?.VisibleDockables?.Select(SaveNode).ToArray() ?? []);
    internal string Serialize() => JsonSerializer.Serialize(new SavedLayout(1, SaveNode(Root),
        Root.Windows?.Where(w => w.Layout is not null).Select(w => new FloatLayout(w.X, w.Y, w.Width, w.Height, SaveNode(w.Layout!))).ToArray() ?? []));
    internal IRootDock Restore(string json, Rect area)
    {
        if (json.Length > 128 * 1024) { throw new InvalidDataException("Layout exceeds budget."); }
        var saved = JsonSerializer.Deserialize<SavedLayout>(json) ?? throw new InvalidDataException("Empty layout.");
        if (saved.Version != 1 || saved.Root.Kind != "Root" || saved.Windows.Length > 5) { throw new InvalidDataException("Unsupported layout."); }
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var count = 0;
        IDockable? Load(Node node, int depth)
        {
            if (++count > 128 || depth > 12 || node.Children is null) { throw new InvalidDataException("Invalid layout tree."); }
            if (node.Kind == "Panel")
            {
                if (!panels.TryGetValue(node.Id, out var panel)) { return null; }
                if (!seen.Add(node.Id)) { throw new InvalidDataException("Duplicate panel identity."); }
                return panel;
            }
            IDockable result = node.Kind switch
            {
                "Root" => new RootDock(),
                "Split" => new ProportionalDock { Orientation = node.Vertical ? Orientation.Vertical : Orientation.Horizontal },
                "Tabs" => new DocumentDock { CanCreateDocument = false, EnableGlobalDocking = true },
                "Divider" => new ProportionalDockSplitter(),
                _ => throw new InvalidDataException("Unknown layout node.")
            };
            result.Id = node.Id;
            result.Proportion = double.IsFinite(node.Proportion) && node.Proportion > 0 ? Math.Clamp(node.Proportion, .05, .95) : double.NaN;
            if (result is IDock dock)
            {
                var children = node.Children.Select(n => Load(n, depth + 1)).OfType<IDockable>().ToArray();
                dock.VisibleDockables = CreateList<IDockable>(children);
                dock.ActiveDockable = children.FirstOrDefault(c => c.Id == node.Active) ?? children.FirstOrDefault();
            }
            return result;
        }
        var root = (IRootDock)Load(saved.Root, 0)!;
        root.Windows = CreateList<IDockWindow>();
        foreach (var floating in saved.Windows)
        {
            if (floating.Root.Kind != "Root") { throw new InvalidDataException("Invalid floating root."); }
            var layout = (IRootDock)Load(floating.Root, 0)!;
            static double Finite(double value, double fallback) => double.IsFinite(value) ? value : fallback;
            var width = Math.Clamp(Finite(floating.Width, 640), 320, Math.Max(320, area.Width));
            var height = Math.Clamp(Finite(floating.Height, 480), 240, Math.Max(240, area.Height));
            var window = new DockWindow
            {
                Id = "Floating" + root.Windows.Count,
                Layout = layout,
                X = Math.Clamp(Finite(floating.X, area.X), area.X, Math.Max(area.X, area.Right - width)),
                Y = Math.Clamp(Finite(floating.Y, area.Y), area.Y, Math.Max(area.Y, area.Bottom - height)),
                Width = width,
                Height = height
            };
            layout.Window = window;
            root.Windows.Add(window);
        }
        if (!Walk(root).OfType<Document>().Any()) { throw new InvalidDataException("Layout has no known panels."); }
        return root;
    }
}

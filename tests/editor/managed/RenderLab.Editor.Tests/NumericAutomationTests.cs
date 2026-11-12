using Avalonia.Automation;
using Avalonia.Automation.Peers;
using Avalonia.Automation.Provider;
using Avalonia.Headless.NUnit;
using NUnit.Framework;
using RenderLab.Editor.Controls;

namespace RenderLab.Editor.Tests;

public class NumericAutomationTests
{
    [AvaloniaTest]
    public void RangeProviderReportsDoubleEventsAndRetainsNumericBehavior()
    {
        var control = new AccessibleNumericUpDown { Minimum = -2, Maximum = 5, Increment = 0.5m, Value = 1 };
        var peer = ControlAutomationPeer.CreatePeerForElement(control)!;
        var provider = peer.GetProvider<IRangeValueProvider>()!;
        var changes = new List<(AutomationProperty Property, object? Old, object? New)>();
        peer.PropertyChanged += (_, e) => changes.Add((e.Property, e.OldValue, e.NewValue));

        provider.SetValue(2.5);
        control.Minimum = -3;
        control.Maximum = 6;
        control.IsReadOnly = true;
        control.Value = null;

        Assert.That(provider.Value, Is.EqualTo(0d));
        Assert.That(provider.Minimum, Is.EqualTo(-3d));
        Assert.That(provider.Maximum, Is.EqualTo(6d));
        Assert.That(provider.SmallChange, Is.EqualTo(0.5d));
        Assert.That(provider.LargeChange, Is.EqualTo(0.5d));
        Assert.That(provider.IsReadOnly, Is.True);
        Assert.That(changes.Where(c => c.Property != RangeValuePatternIdentifiers.IsReadOnlyProperty)
            .All(c => c.Old is double && c.New is double), Is.True);
        Assert.That(changes.Any(c => c.Property == RangeValuePatternIdentifiers.ValueProperty &&
            Equals(c.Old, 1d) && Equals(c.New, 2.5d)), Is.True);
        Assert.That(changes.Any(c => c.Property == RangeValuePatternIdentifiers.ValueProperty &&
            Equals(c.Old, 2.5d) && Equals(c.New, 0d)), Is.True);
        Assert.That(changes.Any(c => c.Property == RangeValuePatternIdentifiers.MinimumProperty &&
            Equals(c.Old, -2d) && Equals(c.New, -3d)), Is.True);
        Assert.That(changes.Any(c => c.Property == RangeValuePatternIdentifiers.MaximumProperty &&
            Equals(c.Old, 5d) && Equals(c.New, 6d)), Is.True);
        Assert.That(changes.Any(c => c.Property == RangeValuePatternIdentifiers.IsReadOnlyProperty &&
            Equals(c.Old, false) && Equals(c.New, true)), Is.True);
    }
}

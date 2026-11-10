using Avalonia;
using Avalonia.Automation;
using Avalonia.Automation.Peers;
using Avalonia.Automation.Provider;
using Avalonia.Controls;

namespace RenderLab.Editor.Controls;

// Avalonia 12.0.5 forwards decimal values to Windows UI Automation, which expects doubles.
public sealed class AccessibleNumericUpDown : NumericUpDown
{
    protected override Type StyleKeyOverride => typeof(NumericUpDown);
    protected override AutomationPeer OnCreateAutomationPeer() => new NumericPeer(this);

    private sealed class NumericPeer : ControlAutomationPeer, IRangeValueProvider
    {
        private readonly AccessibleNumericUpDown owner;

        public NumericPeer(AccessibleNumericUpDown owner) : base(owner)
        {
            this.owner = owner;
            owner.PropertyChanged += OnOwnerPropertyChanged;
        }

        public bool IsReadOnly => owner.IsReadOnly;
        public double Maximum => (double)owner.Maximum;
        public double Minimum => (double)owner.Minimum;
        public double Value => ToValue(owner.Value);
        public double SmallChange => (double)owner.Increment;
        public double LargeChange => (double)owner.Increment;
        public void SetValue(double value) => owner.Value = (decimal)value;
        protected override AutomationControlType GetAutomationControlTypeCore() => AutomationControlType.Spinner;
        protected override string GetClassNameCore() => "NumericUpDown";

        private double ToValue(decimal? value) => (double)(value ?? Math.Clamp(0m, owner.Minimum, owner.Maximum));

        private void OnOwnerPropertyChanged(object? sender, AvaloniaPropertyChangedEventArgs e)
        {
            if (e.Property == NumericUpDown.ValueProperty)
                RaisePropertyChangedEvent(RangeValuePatternIdentifiers.ValueProperty, ToValue((decimal?)e.OldValue), ToValue((decimal?)e.NewValue));
            else if (e.Property == NumericUpDown.MinimumProperty)
                RaisePropertyChangedEvent(RangeValuePatternIdentifiers.MinimumProperty, (double)(decimal)e.OldValue!, (double)(decimal)e.NewValue!);
            else if (e.Property == NumericUpDown.MaximumProperty)
                RaisePropertyChangedEvent(RangeValuePatternIdentifiers.MaximumProperty, (double)(decimal)e.OldValue!, (double)(decimal)e.NewValue!);
            else if (e.Property == NumericUpDown.IsReadOnlyProperty)
                RaisePropertyChangedEvent(RangeValuePatternIdentifiers.IsReadOnlyProperty, e.OldValue, e.NewValue);
        }
    }
}

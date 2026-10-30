namespace RenderLab.Editor.Services;

internal sealed record DeviceSelection(string Backend, bool Validation);

/// <summary>Serial device replacement with one rollback attempt and an explicit retry state.</summary>
internal sealed class DeviceSwitchTransaction(DeviceSelection initial)
{
    internal DeviceSelection Current { get; private set; } = initial;
    internal bool Busy { get; private set; }
    internal bool Failed { get; private set; }
    internal string Status { get; private set; } = "Ready";
    internal string Error { get; private set; } = "";
    internal event Action? Changed;
    internal void Report(string status) { Status = status; Changed?.Invoke(); }
    internal void ObserveReady(DeviceSelection selection)
    {
        if (Busy || (Current == selection && !Failed)) { return; }
        Current = selection; Failed = false; Error = "";
        Report("Ready (viewport recreated)");
    }

    internal async Task RunAsync(DeviceSelection target, Func<DeviceSelection, Task> attempt, CancellationToken cancellation)
    {
        if (Busy) { throw new InvalidOperationException("A device switch is already running."); }
        var previous = Current;
        Busy = true; Failed = false; Error = "";
        Report("Switching to " + target.Backend);
        try
        {
            try
            {
                await attempt(target);
                cancellation.ThrowIfCancellationRequested();
                Current = target;
                Report("Ready");
            }
            catch (OperationCanceledException) { throw; }
            catch (Exception error)
            {
                Error = "Target: " + error.Message;
                Report("Rolling back to " + previous.Backend);
                try
                {
                    await attempt(previous);
                    cancellation.ThrowIfCancellationRequested();
                    Report("Ready (rolled back)");
                }
                catch (OperationCanceledException) { throw; }
                catch (Exception rollback)
                {
                    Failed = true;
                    Error += "\nRollback: " + rollback.Message;
                    Report("Failed — use Retry");
                }
            }
        }
        catch (OperationCanceledException) { Report("Cancelled"); throw; }
        finally { Busy = false; Changed?.Invoke(); }
    }
}

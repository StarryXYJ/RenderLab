namespace RenderLab.Editor.Services;

internal sealed record ProjectSwitchResult(Exception? TargetError, Exception? RollbackError, bool Restored);

internal static class ProjectSwitchTransaction
{
    internal static async Task<ProjectSwitchResult> RunAsync(string target, string? previous,
        Func<string, CancellationToken, Task> open, CancellationToken cancellation)
    {
        try
        {
            await open(target, cancellation);
            cancellation.ThrowIfCancellationRequested();
            return new(null, null, false);
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { throw; }
        catch (Exception error)
        {
            if (previous is null) { return new(error, null, false); }
            try
            {
                await open(previous, cancellation);
                cancellation.ThrowIfCancellationRequested();
                return new(error, null, true);
            }
            catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { throw; }
            catch (Exception rollback) { return new(error, rollback, false); }
        }
    }
}

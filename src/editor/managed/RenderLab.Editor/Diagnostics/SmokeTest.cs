using System.Diagnostics;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;
using RenderLab.Editor.Controls;
using RenderLab.Editor.Views;
using RenderLab.Editor.Configuration;
using RenderLab.Editor.Interop;
using RenderLab.Editor.Docking;
using System.Security.Cryptography;
using Avalonia.VisualTree;

namespace RenderLab.Editor.Diagnostics;

internal sealed class SmokeTest
{
    private readonly MainWindow window;
    private readonly EditorOptions options;
    private readonly List<string> checks = [];
    private readonly Stopwatch elapsed = Stopwatch.StartNew();
    private readonly DispatcherTimer watchdog = new() { Interval = TimeSpan.FromSeconds(300) };
    private Task? switchTask;
    private int deviceSwitches;
    private int stage, ticks, resizeBurstTicks;
    private int acknowledgedPauseStage = -1;
    private ulong oldHandle, savedFrames, savedResizes, savedPaused, savedInput;
    private bool completed;
    private RenderViewportHost? detachedHost;
    private readonly float[] savedPose = new float[7];
    private readonly Dictionary<string, string> sourceHashes;
    private int configurationCase;
    private ulong configurationRequest;
    private ViewportConfig expectedConfig;
    private uint savedReflectionSamples;

    internal SmokeTest(MainWindow window, EditorOptions options)
    {
        this.window = window;
        this.options = options;
        sourceHashes = ReadSourceHashes();
        window.StatsUpdated += Update;
        watchdog.Tick += (_, _) => Fail(new TimeoutException("Viewport smoke stopped making progress."));
        watchdog.Start();
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
    private Dictionary<string, string> ReadSourceHashes() => Directory.EnumerateFiles(Path.GetDirectoryName(options.Project)!, "*", SearchOption.AllDirectories)
        .Where(path => path.EndsWith(".scene") || path.EndsWith(".material") || path.EndsWith(".rlproject"))
        .Where(path => !path.Contains(Path.DirectorySeparatorChar + "Library" + Path.DirectorySeparatorChar) && !path.Contains(Path.DirectorySeparatorChar + "UserSettings" + Path.DirectorySeparatorChar))
        .ToDictionary(path => path, path => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))));

    private bool PixelsMatch(ViewportStats stats) =>
        Math.Abs(stats.Width - window.Viewport.Bounds.Width * window.RenderScaling) <= 2 &&
        Math.Abs(stats.Height - window.Viewport.Bounds.Height * window.RenderScaling) <= 2;

    private void CheckPixels(ViewportStats stats) => Require(PixelsMatch(stats),
        "Native client size does not match Avalonia DIP-to-pixel layout.");

    private void Update(ViewportStats stats)
    {
        if (completed)
        {
            return;
        }
        try
        {
            Require(elapsed.Elapsed < TimeSpan.FromSeconds(300), "Viewport smoke timed out.");
            if (stage == 17 && resizeBurstTicks < 6)
            {
                ++resizeBurstTicks;
                window.Width += 10;
                return;
            }
            if (++ticks < 8 || stats.Frames < 1 || stats.Ready == 0)
            {
                return;
            }
            // Configuration now crosses a mailbox. First-use shader/PSO work may take
            // longer than eight UI heartbeats; wait for the published render result.
            if ((stage == 9 && stats.Pipeline != 1) ||
                (stage == 10 && (stats.Pipeline != 3 || stats.ReflectionSamples == 0)) ||
                (stage == 12 && (stats.Pipeline != 3 || stats.ReflectionSamples == 0 ||
                    (stats.ProfilingEnabled != 0 && stats.GpuFrame == 0))) ||
                (stage == 13 && (stats.Pipeline != 1 || stats.Activity != 2)))
            {
                return;
            }
            if ((stage is 1 or 17) && !PixelsMatch(stats))
            {
                return;
            }
            if (stage is 5 or 7 or 11 && acknowledgedPauseStage != stage)
            {
                if (stats.Activity != 3)
                {
                    return;
                }
                // Freeze the reference after the render owner acknowledges suspension.
                // A UI snapshot sampled before the command may already lag an in-flight frame.
                acknowledgedPauseStage = stage;
                savedFrames = stats.Frames;
                ticks = 0;
                return;
            }
            ticks = 0;
            var host = window.Viewport;
            var root = window.TryGetPlatformHandle()!.Handle;
            switch (stage++)
            {
                case 0:
                    Require(window.FocusBox.GetVisualAncestors().OfType<TopLevel>().Any(),
                        "Render Settings controls are not mounted in the visible Dock tree.");
                    Require(window.Docks.IsOpen("Scene") && window.Docks.IsOpen("Settings") &&
                        !window.Docks.IsOpen("Log") && !window.Docks.IsOpen("Render Graph") && !window.Docks.IsOpen("Profiler"),
                        "Default layout did not contain exactly the two requested panels.");
                    Require(stats.UiThreadId != 0 && stats.RenderThreadId != 0 &&
                        stats.UiThreadId != stats.RenderThreadId, "Runtime did not move to a dedicated render owner.");
                    Require(stats.DroppedSamples == 0, "Completed frame samples were dropped.");
                    Require(stats.ValidationEnabled == (options.Validation ? 1u : 0u) &&
                        stats.ProfilingEnabled == (options.Profiling ? 1u : 0u), "Creation settings were not applied.");
                    Require(NativeMethods.IsChild(root, host.Child), "Viewport is not embedded in the Avalonia window.");
                    CheckPixels(stats);
                    Require(NativeMethods.GetStats(ulong.MaxValue, out _) == 2, "Invalid handle accepted.");
                    var small = new byte[] { 0x5A };
                    Require(NativeMethods.LastError(small, 1, out var required) == 5 && required > 1 && small[0] == 0x5A,
                        "Error buffer was truncated or overwritten.");
                    Require(Task.Run(() => NativeMethods.GetStats(host.Session, out _)).GetAwaiter().GetResult() == 3,
                        "Wrong-thread access was accepted.");
                    Require(NativeMethods.Configure(host.Session, 99, 0, 0) == 1, "Invalid pipeline accepted.");
                    window.ViewModel.SelectedPipelineIndex = 99;
                    Require(window.ViewModel.SelectedPipelineIndex == 1 && window.ViewModel.ErrorText.Length > 0,
                        "Rejected renderer settings did not restore the view model selection.");
                    window.ViewModel.Exposure = .25;
                    window.ViewModel.Exposure = 0;
                    checks.Add("embedded HWND, current DPI pixel layout, invalid handles, error buffer, owner thread, invalid settings");
                    savedResizes = stats.Resizes;
                    window.Width = 1060;
                    window.Height = 700;
                    break;
                case 1:
                    Require(stats.Resizes > savedResizes, "Window resize did not reach swapchain.");
                    CheckPixels(stats);
                    checks.Add("resize and DIP-to-pixel client dimensions");
                    savedInput = stats.InputFrames;
                    NativeMethods.SetFocus(host.Child);
                    NativeMethods.SendMessage(host.Child, 0x0100, 'W', 0);
                    break;
                case 2:
                    Require(stats.InputFrames > savedInput && stats.PressedKeys > 0, "Focused camera input was not consumed.");
                    NativeMethods.SendMessage(host.Child, 0x0204, 2, 0);
                    break;
                case 3:
                    Require(stats.Captured != 0, "Right mouse capture failed.");
                    NativeMethods.SetFocus(root);
                    window.FocusBox.Focus();
                    break;
                case 4:
                    Require(stats.Focused == 0 && stats.Captured == 0 && stats.PressedKeys == 0,
                        "Focus loss did not release capture and keys.");
                    checks.Add("camera input, right mouse capture and managed-focus release");
                    savedFrames = stats.Frames;
                    savedPaused = stats.PausedTicks;
                    window.WindowState = WindowState.Minimized;
                    break;
                case 5:
                    Require(stats.PausedTicks > savedPaused && stats.Frames == savedFrames,
                        "Minimized window continued rendering.");
                    window.WindowState = WindowState.Normal;
                    break;
                case 6:
                    Require(stats.Frames > savedFrames, "Restored window did not resume rendering.");
                    checks.Add("minimize pauses rendering and restore resumes");
                    savedFrames = stats.Frames;
                    savedPaused = stats.PausedTicks;
                    host.IsVisible = false;
                    break;
                case 7:
                    Require(stats.PausedTicks > savedPaused && stats.Frames == savedFrames,
                        "Hidden viewport continued rendering.");
                    host.IsVisible = true;
                    break;
                case 8:
                    Require(stats.Frames > savedFrames, "Shown viewport did not resume.");
                    checks.Add("hide/show viewport");
                    window.PipelinePicker.SelectedIndex = 0;
                    window.ExposureSlider.Value = .5;
                    Require(window.ResetButton.Command != null, "Reset command binding is missing.");
                    window.ResetButton.Command!.Execute(null);
                    break;
                case 9:
                    Require(stats.Pipeline == 1 && window.ViewModel.SelectedPipelineIndex == 0 &&
                        Math.Abs(window.ViewModel.Exposure - .5) < .001,
                        "Compiled pipeline/exposure bindings did not update the view model and renderer.");
                    window.PipelinePicker.SelectedIndex = 2;
                    break;
                case 10:
                    Require(stats.Pipeline == 3 && stats.ReflectionSamples > 0, "Ray tracing did not present.");
                    checks.Add("DI services, compiled two-way pipeline/exposure bindings, generated reset command and three pipeline presentation");
                    savedFrames = stats.Frames;
                    savedPaused = stats.PausedTicks;
                    Require(window.PauseButton.Command != null, "Pause command binding is missing.");
                    window.PauseButton.Command!.Execute(null);
                    break;
                case 11:
                    Require(stats.PausedTicks > savedPaused && stats.Frames == savedFrames &&
                        window.ViewModel.IsPaused && window.PauseButton.Content?.ToString() == "Resume viewport",
                        "Pause command or generated property notification did not update rendering and UI.");
                    window.PauseButton.Command!.Execute(null);
                    checks.Add("generated pause command and pause-label notification");
                    oldHandle = host.Session;
                    detachedHost = host;
                    Require(window.RecreateButton.Command != null, "Recreate command binding is missing.");
                    window.RecreateButton.Command!.Execute(null);
                    break;
                case 12:
                    Require(stats.TickNs > 0 && stats.RenderNs > 0 &&
                        (stats.ProfilingEnabled == 0 || (stats.GpuFrame > 0 && stats.GpuNs > 0)),
                        "CPU/GPU frame timings were not collected.");
                    Require(host.Session != oldHandle && NativeMethods.GetStats(oldHandle, out _) == 2,
                        "Destroyed session generation was reused.");
                    Require(window.CloseReports.Count == 1 && window.CloseReports[0].RemainingBytes == 0 &&
                        window.CloseReports[0].ValidationErrors == 0, "First viewport shutdown leaked or failed validation.");
                    Require(stats.Pipeline == 3 && stats.ReflectionSamples > 0 && !window.ViewModel.IsPaused,
                        "Viewport settings were lost during host recreation.");
                    detachedHost!.CloseViewport();
                    Require(NativeMethods.GetStats(host.Session, out _) == 0,
                        "A repeated detached-host close destroyed the replacement session.");
                    checks.Add("detach/recreate, stale generation rejection, GPU-before-HWND shutdown");
                    window.PipelinePicker.SelectedIndex = 0;
                    break;
                case 13:
                    savedFrames = stats.Frames;
                    break;
                case 14:
                    Require(stats.Frames == savedFrames && stats.Activity == 2,
                        "Static raster viewport kept rendering.");
                    checks.Add("static raster viewport idles without additional presents");
                    window.ExposureSlider.Value = .75;
                    break;
                case 15:
                    Require(stats.Frames > savedFrames, "Exposure edit did not invalidate the static viewport.");
                    window.PipelinePicker.SelectedIndex = 2;
                    break;
                case 16:
                    if (stats.Pipeline != 3 || stats.ReflectionSamples < 2)
                    {
                        --stage;
                        break;
                    }
                    checks.Add("raster edits redraw and ray tracing continues progressive accumulation");
                    savedResizes = stats.Resizes;
                    break;
                case 17:
                    CheckPixels(stats);
                    Require(stats.Resizes > savedResizes && stats.Resizes <= savedResizes + 2,
                        "Consecutive resize updates were not coalesced.");
                    checks.Add("six consecutive UI resize updates coalesced into at most two swapchain rebuilds");
                    NativeMethods.Check(NativeMethods.Camera(host.Session, savedPose, 0));
                    oldHandle = host.Session;
                    window.Docks.FloatDockable(window.Docks.Panel("Scene"));
                    break;
                case 18:
                    Require(host.Session != oldHandle && NativeMethods.GetStats(oldHandle, out _) == 2,
                        "Floating Scene did not retire the old native host.");
                    var floatingPose = new float[7];
                    NativeMethods.Check(NativeMethods.Camera(host.Session, floatingPose, 0));
                    Require(floatingPose.Zip(savedPose).All(p => Math.Abs(p.First - p.Second) < .001f),
                        "Floating Scene lost the navigation camera.");
                    window.Docks.Show("Log"); window.Docks.Show("Log");
                    Require(WorkbenchDockFactory.Walk(window.Docks.Root).Count(p => p.Id == "Log") == 1,
                        "Opening the same Dock twice created duplicate panels.");
                    checks.Add("Scene floating recreates the session and preserves camera; menu activation is single-instance");
                    window.ResetLayout(false);
                    break;
                case 19:
                    Require(window.Docks.IsOpen("Scene") && !window.Docks.IsOpen("Log"), "Default reset retained diagnostic docks.");
                    var restoredPose = new float[7];
                    NativeMethods.Check(NativeMethods.Camera(host.Session, restoredPose, 0));
                    Require(restoredPose.Zip(savedPose).All(p => Math.Abs(p.First - p.Second) < .001f), "Dock reset lost camera pose.");
                    Require(Program.Logging!.Buffer.Snapshot().Any(r => r.Source.StartsWith("Native.") && r.Message.Contains("Render owner thread")),
                        "Native structured log mirror never reached the Log buffer.");
                    checks.Add("Scene docks back through layout reset; native log batches are collected without managed callbacks");
                    break;
                case 20:
                    var service = window.ViewportService;
                    var configState = service.Configuration;
                    if (configState.Status != 2) { --stage; break; }
                    Require(stats.SessionGeneration == host.Session && stats.ConfigRevision > 0, "Completed frames lack session/config identity.");
                    if (configurationRequest != 0)
                    {
                        Require(configState.AppliedId == configurationRequest, "Submission configuration was not confirmed by a successful frame.");
                    }
                    if (configurationCase >= 12)
                    {
                        checks.Add("all supported pipeline/submission modes, threaded/async/alias options and frame configuration identity");
                        var invalid = service.EditableConfig; invalid.ShadowResolution = 192; invalid.RequestId = configState.RequestedId + 1;
                        Require(NativeMethods.ConfigureV2(host.Session, in invalid) == 1, "Non-power-of-two shadow resolution accepted.");
                        invalid = service.EditableConfig; invalid.LightX = float.NaN; invalid.RequestId = configState.RequestedId + 1;
                        Require(NativeMethods.ConfigureV2(host.Session, in invalid) == 1, "Non-finite lighting accepted.");
                        invalid = service.EditableConfig; invalid.Size = 1; invalid.RequestId = configState.RequestedId + 1;
                        Require(NativeMethods.ConfigureV2(host.Session, in invalid) == 1, "Wrong config ABI size accepted.");
                        window.ViewModel.IsPaused = true;
                        break;
                    }
                    using (var capabilities = JsonDocument.Parse(service.Capabilities))
                    {
                        var pipeline = configurationCase / 4;
                        var submission = configurationCase % 4;
                        ++configurationCase;
                        if (capabilities.RootElement.GetProperty("pipelines")[pipeline].GetProperty("available").GetBoolean() &&
                            capabilities.RootElement.GetProperty("submissions")[submission].GetProperty("available").GetBoolean())
                        {
                            var candidate = service.EditableConfig;
                            candidate.Pipeline = (uint)pipeline + 1; candidate.Submission = (uint)submission;
                            candidate.RecordingThreads = submission % 2 == 0 ? 1u : 4u;
                            candidate.AsyncCompute = submission == 3 && capabilities.RootElement.GetProperty("asyncCompute").GetBoolean() ? 1u : 0;
                            candidate.MemoryAlias = submission == 3 ? 1u : 0; candidate.View = 0;
                            candidate.Flags |= 15;
                            service.Submit(candidate);
                            configurationRequest = service.Configuration.RequestedId;
                        }
                    }
                    --stage;
                    break;
                case 21:
                    if (stats.Activity != 3) { --stage; break; }
                    savedFrames = stats.Frames;
                    savedReflectionSamples = stats.ReflectionSamples;
                    for (var request = 0; request < 25; ++request)
                    {
                        var candidate = window.ViewportService.EditableConfig;
                        candidate.Exposure = request * .01f;
                        window.ViewportService.Submit(candidate);
                    }
                    configurationRequest = window.ViewportService.Configuration.RequestedId;
                    break;
                case 22:
                    Require(stats.Frames == savedFrames && window.ViewportService.Configuration.AppliedId != configurationRequest,
                        "Paused configuration was marked applied without a submitted frame.");
                    Require(window.ViewportService.Configuration.SupersededId > 0, "Coalesced requests lost superseded identity.");
                    window.ViewModel.SingleStepCommand.Execute(null);
                    break;
                case 23:
                    if (stats.Activity != 3 || stats.Frames == savedFrames) { --stage; break; }
                    Require(stats.Frames == savedFrames + 1 && window.ViewportService.Configuration.AppliedId == configurationRequest,
                        "Single step did not apply exactly one paused frame.");
                    Require(stats.ReflectionSamples == savedReflectionSamples + 1,
                        "Exposure-only edit incorrectly discarded progressive HDR history.");
                    checks.Add("invalid ABI/scalars rejected; rapid requests superseded; paused settings wait for exactly one single step");
                    var preset = window.ViewportService.EditableConfig;
                    preset.LightR = .7f; preset.LightG = .8f; preset.LightB = .9f; preset.LightIntensity = 2;
                    preset.EnvironmentIntensity = .8f; preset.EnvironmentRotation = 15;
                    preset.ShadowBias = .002f; preset.ShadowNormalBias = .02f; preset.ShadowResolution = 256;
                    preset.Flags |= 16; preset.CameraSpeed = 4;
                    window.ViewportService.Submit(preset);
                    expectedConfig = preset;
                    window.ViewportService.SavePreset();
                    var temporary = preset; temporary.Exposure = -.8f; window.ViewportService.Submit(temporary);
                    window.ViewportService.LoadPreset();
                    configurationRequest = window.ViewportService.Configuration.RequestedId;
                    window.ViewModel.SingleStepCommand.Execute(null);
                    break;
                case 24:
                    if (window.ViewportService.Configuration.AppliedId != configurationRequest || stats.Activity != 3) { --stage; break; }
                    var applied = window.ViewportService.Configuration.Applied;
                    Require(Math.Abs(applied.Exposure - expectedConfig.Exposure) < .001 && Math.Abs(applied.LightR - .7) < .001 &&
                        applied.ShadowResolution == 256 && Math.Abs(applied.EnvironmentRotation - 15) < .001,
                        "Render preset did not restore lighting/shadow/display overrides.");
                    Require(stats.ReflectionSamples == 1, "Lighting changes failed to invalidate progressive history.");
                    checks.Add("runtime lighting/environment/shadow overrides and preset restoration");
                    window.ViewModel.IsPaused = false;
                    oldHandle = host.Session;
                    window.RecreateViewport();
                    break;
                case 25:
                    if (window.ViewportService.Configuration.Status != 2) { --stage; break; }
                    Require(host.Session != oldHandle && window.ViewportService.Configuration.Applied.ShadowResolution == 256,
                        "Full render configuration was lost across Dock host recreation.");
                    var uncapped = window.ViewportService.EditableConfig; uncapped.Pipeline = 1; uncapped.TargetFps = 0; uncapped.Profiling = 0;
                    window.ViewportService.Submit(uncapped); configurationRequest = window.ViewportService.Configuration.RequestedId;
                    break;
                case 26:
                    if (window.ViewportService.Configuration.AppliedId != configurationRequest) { --stage; break; }
                    Require(stats.ProfilingEnabled == 0 && window.ViewportService.Configuration.Applied.TargetFps == 0,
                        "Dynamic pacing/profile configuration did not apply.");
                    var capped = window.ViewportService.EditableConfig; capped.TargetFps = 60; capped.Profiling = 1;
                    window.ViewportService.Submit(capped); configurationRequest = window.ViewportService.Configuration.RequestedId;
                    break;
                case 27:
                    if (window.ViewportService.Configuration.AppliedId != configurationRequest) { --stage; break; }
                    Require(ReadSourceHashes().OrderBy(p => p.Key).SequenceEqual(sourceHashes.OrderBy(p => p.Key)),
                        "Runtime settings modified Scene/Material/Project source files.");
                    Require(window.ViewModel.Settings.AppliedSummary.Contains("#" + configurationRequest),
                        "Settings UI does not identify the actual applied configuration.");
                    checks.Add("configuration retained across host recreation; VSync pacing transition and source file hashes unchanged");
                    window.Docks.Show("Log");
                    break;
                case 28:
                    if (window.ViewportService.Configuration.Status != 2) { --stage; break; }
                    var logView = window.GetVisualDescendants().OfType<LogView>().FirstOrDefault();
                    if (logView is null) { --stage; break; }
                    var logModel = (RenderLab.Editor.ViewModels.LogViewModel)logView.DataContext!;
                    if (logModel.Rows.Count == 0) { --stage; break; }
                    Require(window.Docks.IsOpen("Log") && logModel.Rows.Any(row => row.Source.StartsWith("Native.")),
                        "Mounted Log panel did not display native structured events.");
                    window.Docks.CloseDockable(window.Docks.Panel("Log"));
                    break;
                case 29:
                    if (window.ViewportService.Configuration.Status != 2) { --stage; break; }
                    Require(window.Docks.IsOpen("Scene") && window.Docks.IsOpen("Settings") && !window.Docks.IsOpen("Log"),
                        "Opening and closing diagnostics disrupted the Scene/settings docks.");
                    checks.Add("diagnostic Dock opens beneath Scene and closes without losing the active renderer");
                    File.WriteAllText(Path.Combine(options.Output, "layout.json"), window.Docks.Serialize());
                    oldHandle = host.Session;
                    window.FindControl<MenuItem>("RestoreLayoutMenu")!.RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(MenuItem.ClickEvent));
                    break;
                case 30:
                    if (window.ViewportService.Configuration.Status != 2) { --stage; break; }
                    Require(host.Session != oldHandle && window.FocusBox.GetVisualAncestors().OfType<TopLevel>().Any(),
                        "Saved layout failed to recreate Scene and mount Render Settings.");
                    Require(window.ViewportService.Configuration.Applied.ShadowResolution == 256,
                        "Saved layout restore lost the runtime configuration.");
                    checks.Add("saved layout restored through the menu with mounted settings and preserved runtime configuration");
                    break;
                case 31:
                    if (deviceSwitches == 20)
                    {
                        window.ViewModel.IsPaused = false;
                        checks.Add("ten DX12/Vulkan round trips, paused/minimized transitions, configuration and stale-session isolation");
                        window.Docks.Show("Render Graph");
                        window.ViewportService.RequestSnapshot();
                        break;
                    }
                    if (switchTask is null)
                    {
                        oldHandle = host.Session;
                        window.ViewModel.IsPaused = deviceSwitches % 3 == 0;
                        if (deviceSwitches % 4 == 1)
                        {
                            window.WindowState = WindowState.Minimized;
                            DispatcherTimer.RunOnce(() => window.WindowState = WindowState.Normal, TimeSpan.FromMilliseconds(600));
                        }
                        var target = window.ViewportService.DeviceSwitch.Current.Backend == "dx12" ? "vulkan" : "dx12";
                        switchTask = window.ViewportService.SwitchDeviceAsync(target, options.Validation);
                    }
                    if (!switchTask.IsCompleted) { --stage; break; }
                    switchTask.GetAwaiter().GetResult();
                    Require(!window.ViewportService.DeviceSwitch.Failed && window.ViewportService.DeviceSwitch.Error.Length == 0,
                        "API switch failed or silently rolled back.");
                    Require(host.Session != oldHandle && NativeMethods.GetStats(oldHandle, out _) == 2,
                        "API switch retained a stale session.");
                    Require(window.ViewportService.Configuration.Applied.ShadowResolution == 256,
                        "API switch lost fixed-scene overrides.");
                    ++deviceSwitches; switchTask = null;
                    --stage;
                    break;
                case 32:
                    if (window.GraphModel.Snapshot is not { } graph) { --stage; break; }
                    Require(window.GetVisualDescendants().OfType<RenderGraphView>().Any(), "Graph viewer did not mount.");
                    Require(graph.Session == host.Session && graph.Config == stats.ConfigRevision && graph.Frame > 0 &&
                        graph.Passes.Any(p => p.Live) && graph.Resources.Any(r => r.Exported) && graph.Edges.Length > 0,
                        "Graph viewer does not reflect the submitted plan identity/dependencies.");
                    savedFrames = graph.Frame;
                    window.GraphModel.Frozen = true;
                    window.ViewModel.Exposure += .1;
                    break;
                case 33:
                    if (window.ViewportService.Configuration.Status != 2) { --stage; break; }
                    Require(window.GraphModel.Snapshot!.Frame == savedFrames && stats.Frames > savedFrames,
                        "Freezing graph either changed the snapshot or paused rendering.");
                    window.GraphModel.ExportCommand.Execute(null);
                    Require(File.Exists(window.GraphModel.ExportPath), "Graph export was not written.");
                    window.GraphModel.Frozen = false;
                    window.GraphModel.RefreshNowCommand.Execute(null);
                    break;
                case 34:
                    if (window.GraphModel.Snapshot is not { } latest || latest.Config != stats.ConfigRevision || latest.Frame <= savedFrames)
                    { --stage; break; }
                    window.Docks.CloseDockable(window.Docks.Panel("Render Graph"));
                    checks.Add("mounted actual-plan matrix, submitted identities/dependencies, independent freeze/live and JSON export");
                    Finish(stats);
                    break;
            }
        }
        catch (Exception error)
        {
            Fail(error);
        }
    }

    private void Finish(ViewportStats stats)
    {
        window.Viewport.CloseViewport();
        Require(window.CloseReports.Count >= 4 && window.CloseReports.All(r => r.RemainingBytes == 0 && r.ValidationErrors == 0 && r.WorkerFailed == 0),
            "Final native shutdown did not release all resources cleanly.");
        NativeMethods.Check(NativeMethods.Create(window.TryGetPlatformHandle()!.Handle,
            options.Backend == "dx12" ? 0u : 1u, options.Validation ? 1u : 0u,
            options.Profiling ? 1u : 0u, options.TargetFps, 0,
            options.Project, options.Shaders, Path.Combine(options.Output, "startup-close.log"),
            out var startupSession, out _));
        NativeMethods.Check(NativeMethods.Destroy(startupSession, out var startupClose));
        Require(startupClose.RemainingBytes == 0 && startupClose.ValidationErrors == 0 && startupClose.WorkerFailed == 0,
            "Closing during render startup leaked or failed validation.");
        checks.Add("immediate close during asynchronous render startup");
        completed = true;
        watchdog.Stop();
        File.WriteAllText(Path.Combine(options.Output, "smoke.json"), JsonSerializer.Serialize(new
        {
            backend = options.Backend,
            avalonia = "12.0.5",
            passed = true,
            checks,
            stats,
            closeReports = window.CloseReports,
            startupClose,
            sourceHashes,
            dpiBoundary = "Current monitor scale verified; cross-monitor DPI transition not tested."
        }, EditorDiagnostics.JsonOptions));
        ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown(0);
    }

    internal void Fail(Exception error)
    {
        if (completed)
        {
            return;
        }
        completed = true;
        watchdog.Stop();
        File.WriteAllText(Path.Combine(options.Output, "smoke-failure.txt"), error.ToString());
        Dispatcher.UIThread.Post(() => ((IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!).Shutdown(1));
    }
}

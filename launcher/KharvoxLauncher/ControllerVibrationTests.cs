using System.Reflection;

namespace KharvoxLauncher;

internal static class ControllerVibrationTests
{
    internal static void Run(string testRoot)
    {
        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
        }

        var path = Path.Combine(testRoot, "controller-vibration.json");
        File.WriteAllText(path, "{\"SettingsVersion\":33,\"Preset\":3}");
        Require(LauncherSettingsStore.Load(path).ControllerVibration,
            "Legacy settings preserve enabled controller vibration.");
        var flags = BindingFlags.Instance | BindingFlags.NonPublic;
        using var form = new MainForm(path);
        var toggle = (CheckBox)typeof(MainForm).GetField("controllerVibration", flags)!.GetValue(form)!;
        Require(toggle.Checked && form.CreateLaunchOptions().ControllerVibration,
            "Controller vibration defaults on in the UI and launch options.");
        var unchangedOptions = form.CreateLaunchOptions().DiagnosticSummary()
            .Replace("controllerVibration=enabled ", string.Empty);
        foreach (var enabled in new[] { false, true })
        {
            toggle.Checked = enabled;
            Require(form.CreateLaunchOptions().DiagnosticSummary()
                .Replace("controllerVibration=" + (enabled ? "enabled " : "disabled "), string.Empty)
                == unchangedOptions, "Changing vibration preserves all other launch options.");
            Require(form.CreateLaunchOptions().ControllerVibration == enabled,
                "Controller vibration selection reaches launch options.");
            Require(form.CreateLaunchOptions().DiagnosticSummary().Contains(
                "controllerVibration=" + (enabled ? "enabled" : "disabled")),
                "Controller vibration selection reaches diagnostics.");
            typeof(MainForm).GetMethod("SaveSettings", flags)!.Invoke(form, null);
            Require(LauncherSettingsStore.Load(path).ControllerVibration == enabled,
                "Controller vibration selection persists.");
            using var restored = new MainForm(path);
            Require(restored.CreateLaunchOptions().ControllerVibration == enabled,
                "Controller vibration selection survives reopening the launcher.");
        }
        toggle.Checked = false;
        var preset = (ComboBox)typeof(MainForm).GetField("preset", flags)!.GetValue(form)!;
        foreach (var index in new[] { 0, 1, 2 })
        {
            preset.SelectedIndex = index;
            Require(!form.CreateLaunchOptions().ControllerVibration,
                "Changing presets preserves the vibration preference.");
        }
    }
}

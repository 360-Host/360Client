// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Launcher360.cs — ALL launcher C# in one file                       ║
// ║  Drop into: launcher/src/Flarial.Launcher/                         ║
// ╚══════════════════════════════════════════════════════════════════════╝

using System;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Diagnostics;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media.Imaging;
using System.Runtime.Serialization;
using System.Xml;
using Windows.ApplicationModel;
using Windows.UI;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Hosting;
using Windows.UI.Core;
using Flarial.Launcher.Runtime.Game;
using Flarial.Launcher.Runtime.Modding;
using Flarial.Launcher.Runtime.Services;
using Flarial.Launcher.Runtime.Versions;
using Flarial.Launcher.Xaml;
using Windows.Win32.Foundation;
using static Windows.Win32.Graphics.Dwm.DWMWINDOWATTRIBUTE;
using static Windows.Win32.PInvoke;
using static System.Environment;
using static System.Environment.SpecialFolder;

[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]

// ════════════════════════════════════════════════════════════════════════
// MANAGEMENT
// ════════════════════════════════════════════════════════════════════════
namespace Flarial.Launcher.Management
{
    public enum DllSelection { Client, Custom }

    [DataContract]
    public sealed class ApplicationSettings
    {
        [DataMember] public bool AutomaticUpdates   { get; set; } = true;
        [DataMember] public DllSelection DllSelection { get; set; } = DllSelection.Client;
        [DataMember] public string CustomDllPath    { get; set; } = string.Empty;
        [DataMember] public bool? WaitForInitialization { get; set; } = true;

        [OnDeserializing]
        void OnDeserializing(StreamingContext _)
        {
            AutomaticUpdates = true; CustomDllPath = string.Empty;
            WaitForInitialization = true; DllSelection = DllSelection.Client;
        }

        private const string FileName = "360Launcher.xml";
        private static readonly XmlWriterSettings s_xml = new() { Indent = true };
        private static readonly DataContractSerializer s_ser = new(typeof(ApplicationSettings));

        public static ApplicationSettings ReadSettings()
        {
            try
            {
                using var s = File.OpenRead(FileName);
                var set = (ApplicationSettings)s_ser.ReadObject(s);
                try { set.CustomDllPath = Path.GetFullPath(set.CustomDllPath.Trim()); }
                catch { set.CustomDllPath = string.Empty; }
                return set;
            }
            catch { return new(); }
        }

        public void SaveSettings()
        {
            using var w = XmlWriter.Create(FileName, s_xml);
            s_ser.WriteObject(w, this);
        }
    }

    public static class ApplicationManifest
    {
        private static readonly Assembly s_asm = Assembly.GetExecutingAssembly();
        public static readonly string s_version = $"{s_asm.GetName().Version}";
        public static Stream GetResourceStream(string name) =>
            s_asm.GetManifestResourceStream(name);
    }
}

// ════════════════════════════════════════════════════════════════════════
// RUNTIME — embedded DLL extraction + self-update
// ════════════════════════════════════════════════════════════════════════
namespace Flarial.Launcher.Runtime
{
    static class EmbeddedClient
    {
        private const string Res  = "360Client.dll";
        private const string File = "360Client.dll";

        public static string GetOrExtract()
        {
            var dir = Path.Combine(GetFolderPath(LocalApplicationData), "360Launcher");
            Directory.CreateDirectory(dir);
            var outPath = Path.Combine(dir, File);

            using var s = GetStream()
                ?? throw new InvalidOperationException("360Client.dll not embedded. Re-download 360Launcher.exe.");

            var bytes = Read(s);
            if (System.IO.File.Exists(outPath) && Hash(outPath) == Hash(bytes))
                return outPath;

            System.IO.File.WriteAllBytes(outPath, bytes);
            return outPath;
        }

        public static bool IsEmbedded =>
            Assembly.GetEntryAssembly()?.GetManifestResourceInfo(Res) is not null;

        public static string? GetEmbeddedVersion()
        {
            var s = Assembly.GetEntryAssembly()
                            ?.GetManifestResourceStream("360Client.version.txt");
            if (s is null) return null;
            using var sr = new StreamReader(s);
            return sr.ReadToEnd().Trim();
        }

        static Stream? GetStream() =>
            Assembly.GetEntryAssembly()?.GetManifestResourceStream(Res);

        static byte[] Read(Stream s) { using var m = new MemoryStream(); s.CopyTo(m); return m.ToArray(); }

        static string Hash(string path) { using var fs = System.IO.File.OpenRead(path); return Hash(Read(fs)); }

        static string Hash(byte[] d)
        {
            using var sha = SHA256.Create();
            return BitConverter.ToString(sha.ComputeHash(d)).Replace("-", "");
        }
    }

    public sealed class Client360
    {
        private const string ApiUrl =
            "https://api.github.com/repos/YOUR_USERNAME/360Client/releases/latest";

        public static readonly Client360 Instance = new();
        private Client360() { }

        public string Version => EmbeddedClient.GetEmbeddedVersion()
            ?? Assembly.GetEntryAssembly()?.GetName().Version?.ToString() ?? "Unknown";
        public bool IsEmbedded => EmbeddedClient.IsEmbedded;

        public static async Task<bool> UpdateAvailableAsync()
        {
            try
            {
                using var m = await HttpService.GetAsync(ApiUrl);
                if (!m.IsSuccessStatusCode) return false;
                var tag = Extract(await m.Content.ReadAsStringAsync(), "tag_name");
                return !string.IsNullOrEmpty(tag) && tag != Instance.Version;
            }
            catch { return false; }
        }

        public static async Task DownloadUpdateAsync(Action<int> progress)
        {
            using var m = await HttpService.GetAsync(ApiUrl);
            var json = await m.Content.ReadAsStringAsync();
            string? url = null;
            int idx = json.IndexOf("browser_download_url", StringComparison.Ordinal);
            while (idx >= 0)
            {
                int s = json.IndexOf('"', idx + 22) + 1, e = json.IndexOf('"', s);
                var u = json[s..e];
                if (u.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) { url = u; break; }
                idx = json.IndexOf("browser_download_url", e, StringComparison.Ordinal);
            }
            if (url is null) throw new InvalidOperationException("No .exe in release.");

            var tmp    = Path.GetTempFileName() + ".exe";
            var script = Path.GetTempFileName() + ".cmd";
            await HttpService.DownloadAsync(url, tmp, progress);

            var exe = Environment.ProcessPath ?? Process.GetCurrentProcess().MainModule!.FileName;
            var bat = new StringBuilder()
                .AppendLine(":w")
                .AppendLine($"tasklist /fi \"pid eq {Environment.ProcessId}\" | find \"{Environment.ProcessId}\" > nul")
                .AppendLine("if not errorlevel 1 (timeout /t 1 /nobreak > nul & goto w)")
                .AppendLine($"move /y \"{tmp}\" \"{exe}\"")
                .AppendLine($"start \"\" \"{exe}\"")
                .AppendLine("del \"%~f0\"").ToString();
            await System.IO.File.WriteAllTextAsync(script, bat);
            Process.Start(new ProcessStartInfo { FileName="cmd.exe", Arguments=$"/c \"{script}\"",
                CreateNoWindow=true, UseShellExecute=false });
            Application.Current.Shutdown();
        }

        public bool Launch(bool? waitForInit)
        {
            var path = EmbeddedClient.GetOrExtract();
            var lib  = new Library(path);
            if (!lib.IsLoadable) throw new InvalidOperationException($"Bad DLL: {path}");
            return Injector.Launch(waitForInit, lib) is not null;
        }

        static string Extract(string json, string field)
        {
            var key = $"\"{field}\":\"";
            var s = json.IndexOf(key, StringComparison.Ordinal);
            if (s < 0) return string.Empty;
            s += key.Length;
            var e = json.IndexOf('"', s);
            return e < 0 ? string.Empty : json[s..e];
        }
    }
}

// ════════════════════════════════════════════════════════════════════════
// PAGES
// ════════════════════════════════════════════════════════════════════════
namespace Flarial.Launcher.Pages
{
    using Flarial.Launcher.Controls;
    using Flarial.Launcher.Interface;
    using Flarial.Launcher.Interface.Dialogs;
    using Flarial.Launcher.Management;
    using Flarial.Launcher.Runtime;
    using Flarial.Launcher.Runtime.Client;
    using Flarial.Launcher.Runtime.Game;
    using Flarial.Launcher.Runtime.Modding;
    using Flarial.Launcher.Runtime.Versions;

    sealed class HomePage : Grid
    {
        internal readonly Button _button = new()
        {
            Content="Connecting...", Width=240, Height=52, IsEnabled=false
        };
        internal readonly TextBlock _leftText = new()
        {
            Text="⚪  Not Installed", Margin=new(16,10,0,0),
            VerticalAlignment=VerticalAlignment.Top,
            HorizontalAlignment=HorizontalAlignment.Left
        };
        readonly TextBlock _ver = new()
        {
            Text=ApplicationManifest.s_version, Margin=new(0,10,16,0),
            VerticalAlignment=VerticalAlignment.Top,
            HorizontalAlignment=HorizontalAlignment.Right
        };
        readonly MainNavigationView _view;
        readonly ApplicationSettings _settings;
        UnsupportedVersionDialog? _unsupported;

        public HomePage(MainNavigationView view, ApplicationSettings settings)
        {
            _view = view; _settings = settings;
            // ── Layout: left panel (brand) | right panel (launch) ──────
            ColumnDefinitions.Add(new());
            ColumnDefinitions.Add(new ColumnDefinition { Width=new GridLength(1.4,GridUnitType.Star) });

            // Left — 360 numeral
            var brand = new StackPanel {
                VerticalAlignment=VerticalAlignment.Center,
                HorizontalAlignment=HorizontalAlignment.Center,
                Spacing=4
            };
            brand.Children.Add(new TextBlock {
                Text="360", FontSize=108, FontWeight=Windows.UI.Text.FontWeights.Black,
                Foreground=new Windows.UI.Xaml.Media.SolidColorBrush(Color.FromArgb(255,0,120,215)),
                HorizontalAlignment=HorizontalAlignment.Center, CharacterSpacing=-200
            });
            brand.Children.Add(new TextBlock {
                Text="L A U N C H E R", FontSize=14, CharacterSpacing=400,
                Foreground=new Windows.UI.Xaml.Media.SolidColorBrush(Color.FromArgb(180,255,255,255)),
                HorizontalAlignment=HorizontalAlignment.Center
            });
            SetColumn(brand,0); Children.Add(brand);

            // Right — launch button
            var launch = new StackPanel {
                VerticalAlignment=VerticalAlignment.Center,
                HorizontalAlignment=HorizontalAlignment.Center,
                Spacing=16, Padding=new(40,0,40,0)
            };
            _button.Background = new Windows.UI.Xaml.Media.SolidColorBrush(Color.FromArgb(255,0,120,215));
            _button.Foreground = new Windows.UI.Xaml.Media.SolidColorBrush(Colors.White);
            _button.HorizontalAlignment=HorizontalAlignment.Center;
            launch.Children.Add(new TextBlock { Text="Ready to Play?", FontSize=20,
                FontWeight=Windows.UI.Text.FontWeights.SemiBold,
                HorizontalAlignment=HorizontalAlignment.Center });
            launch.Children.Add(_button);
            SetColumn(launch,1); Children.Add(launch);

            // Status bar overlaid at top
            SetColumnSpan(_leftText,2); Children.Add(_leftText);
            SetColumnSpan(_ver,2);      Children.Add(_ver);

            _button.Click += OnClick;
        }

        void OnProgress(int v) => Dispatcher.Invoke(() => _button.Content=$"Downloading... {v}%");

        async void OnClick(object s, RoutedEventArgs e)
        {
            var btn = (Button)s;
            try
            {
                btn.IsEnabled=false; btn.Content="▶   Launch";
                var reg = (VersionRegistry)Tag;
                var custom = _settings.DllSelection is Management.DllSelection.Custom;

                if (!Minecraft.IsInstalled)      { await MainDialog.NotInstalled.ShowAsync(); return; }
                if (!Minecraft.IsGamingServicesInstalled) { await MainDialog.GamingServicesMissing.ShowAsync(); return; }
                if (!Minecraft.IsPackaged && !await MainDialog.UnsignedInstall.ShowAsync()) return;

                if (!custom && !reg.IsSupported)
                {
                    _unsupported ??= new(reg.PreferredVersion);
                    switch (await _unsupported.PromptAsync())
                    {
                        case ContentDialogResult.Primary:
                            (~_view).SelectedItem=_view._versionsItem;
                            (~_view).Content=_view._versionsItem.Tag; break;
                        case ContentDialogResult.Secondary:
                            var si=(NavigationViewItem)(~_view).SettingsItem;
                            (~_view).SelectedItem=si; (~_view).Content=si.Tag; break;
                    }
                    return;
                }

                if (custom)
                {
                    var path=_settings.CustomDllPath;
                    if (string.IsNullOrWhiteSpace(path)) { await MainDialog.InvalidCustomDll.ShowAsync(); return; }
                    var lib=new Library(path);
                    if (!lib.IsLoadable)        { await MainDialog.InvalidCustomDll.ShowAsync(); return; }
                    btn.Content="Launching...";
                    if (await Task.Run(()=>Injector.Launch(_settings.WaitForInitialization,lib)) is null)
                        await MainDialog.LaunchFailure.ShowAsync();
                    return;
                }

                // Use embedded 360Client
                btn.Content="Verifying...";
                if (!await FlarialClient.Release.DownloadAsync(OnProgress))
                    { await MainDialog.ClientUpdateFailure.ShowAsync(); return; }

                btn.Content="Launching...";
                if (!await Task.Run(()=>FlarialClient.Release.Launch(_settings.WaitForInitialization)))
                    await MainDialog.LaunchFailure.ShowAsync();
            }
            finally { btn.IsEnabled=true; btn.Content="▶   Launch"; }
        }
    }

    sealed class VersionsPage : Grid
    {
        internal readonly ListBox _listBox = new()
            { VerticalAlignment=VerticalAlignment.Stretch, HorizontalAlignment=HorizontalAlignment.Stretch };
        internal readonly Button _button = new()
            { Content="Install Version", Height=44, IsEnabled=false };
        readonly TextBlockProgressBar _bar = new();
        readonly MainNavigationView _view;
        VersionItem? _item;

        public VersionsPage(MainNavigationView view)
        {
            _view=view; Margin=new(16,12,16,16); RowSpacing=10;
            RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});
            RowDefinitions.Add(new());
            RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});
            RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});

            var title=new TextBlock{Text="Versions",FontSize=18,FontWeight=Windows.UI.Text.FontWeights.SemiBold};
            SetRow(title,0); Children.Add(title);
            SetRow(_listBox,1); Children.Add(_listBox);
            SetRow(_bar,2);     Children.Add(_bar);
            SetRow(_button,3);  Children.Add(_button);

            _button.Background=new Windows.UI.Xaml.Media.SolidColorBrush(Color.FromArgb(255,0,120,215));
            _listBox.SetValue(VirtualizingStackPanel.IsVirtualizingProperty,true);
            VirtualizingStackPanel.SetVirtualizationMode(_listBox,VirtualizationMode.Recycling);
            _listBox.SelectionChanged+=OnSel;
            _button.Click+=OnClick;
            Application.Current.MainWindow.Closing+=(s,e)=>{
                if(_item is{}){e.Cancel=true;(~_view).SelectedItem=_view._versionsItem;(~_view).Content=_view._versionsItem.Tag;}};
            (~view).RegisterPropertyChangedCallback(ContentControl.ContentProperty,(s,p)=>{
                if(_item is null&&_listBox.Items.Count>0){_listBox.SelectedItem=null;_listBox.ScrollIntoView(_listBox.Items[0]);}});
        }

        void OnProg(int v,bool state)=>Dispatcher.Invoke(()=>{
            string t=state?"Installing...":"Downloading...";
            _bar._progressBar.Value=v<=0?0:v;
            _bar._textBlock.Text=v<=0?t:$"{t} {v}%";});

        void OnSel(object s,RoutedEventArgs e){var lb=(ListBox)s;if(_item is{}){lb.SelectedItem=_item;lb.ScrollIntoView(_item);}}

        async void OnClick(object s,RoutedEventArgs e)
        {
            try
            {
                _button.Opacity=0; _button.IsEnabled=false;
                _bar._textBlock.Text="Downloading..."; _bar._textBlock.Visibility=Visibility.Visible;
                _bar._progressBar.Value=0; _bar._progressBar.Visibility=Visibility.Visible;
                if(!Minecraft.IsInstalled){await MainDialog.NotInstalled.ShowAsync();return;}
                if(!Minecraft.IsPackaged){await MainDialog.UnpackagedInstall.ShowAsync();return;}
                if(!Minecraft.IsGamingServicesInstalled){await MainDialog.GamingServicesMissing.ShowAsync();return;}
                if(_listBox.SelectedItem is null){await MainDialog.SelectVersion.ShowAsync();return;}
                if(!await MainDialog.InstallVersion.ShowAsync())return;
                _item=(VersionItem)_listBox.SelectedItem;
                _listBox.ScrollIntoView(_item);
                await _item.InstallAsync(OnProg);
            }
            finally
            {
                _item=null; _button.Opacity=100; _button.IsEnabled=true;
                _bar._textBlock.Visibility=Visibility.Collapsed;
                _bar._progressBar.Visibility=Visibility.Collapsed;
            }
        }
    }

    sealed class SettingsPage : Grid
    {
        readonly ApplicationSettings _settings;
        readonly ToggleSwitch _autoUpdate = new()
        {
            Header="Automatic Updates", OnContent="Enabled.", OffContent="Ask before updating.",
            VerticalAlignment=VerticalAlignment.Stretch, HorizontalAlignment=HorizontalAlignment.Stretch
        };

        public SettingsPage(ApplicationSettings settings)
        {
            _settings=settings; Margin=new(20,16,20,20); RowSpacing=16;
            RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});
            RowDefinitions.Add(new());
            RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});

            var scroll=new ScrollViewer{VerticalScrollBarVisibility=ScrollBarVisibility.Auto};
            var content=new StackPanel{Spacing=16};
            content.Children.Add(_autoUpdate);
            content.Children.Add(new TextBlock{Text="DLL Selection:"});
            content.Children.Add(new Controls.DllSelectionBox(settings));
            content.Children.Add(new TextBlock{Text="Initialization Type:"});
            content.Children.Add(new Controls.InitializationTypeBox(settings));
            scroll.Content=content;

            SetRow(new TextBlock{Text="Settings",FontSize=20,FontWeight=Windows.UI.Text.FontWeights.SemiBold},0);
            Children.Add(scroll); SetRow(scroll,1);
            var folders=new Controls.FolderButtonsBox(); Children.Add(folders); SetRow(folders,2);

            _autoUpdate.Toggled+=(s,e)=>{ if(s is ToggleSwitch ts)_settings.AutomaticUpdates=ts.IsOn; };
            _autoUpdate.IsOn=_settings.AutomaticUpdates;
        }
    }
}

// ════════════════════════════════════════════════════════════════════════
// INTERFACE
// ════════════════════════════════════════════════════════════════════════
namespace Flarial.Launcher.Interface
{
    using Flarial.Launcher.Management;
    using Flarial.Launcher.Pages;
    using Flarial.Launcher.Runtime;
    using Flarial.Launcher.Runtime.Game;
    using Flarial.Launcher.Runtime.Versions;

    sealed class MainNavigationView : XamlElement<NavigationView>, IDisposable
    {
        private const string StatusOffline     = "⚪  Not Installed";
        private const string StatusSupported   = "🔵  {0}";
        private const string StatusUnsupported = "🔴  {0}";
        private const string BtnPlay           = "▶   Launch";
        private const string BtnUpdating       = "Updating";
        private const string BtnConnect        = "Connecting...";

        internal readonly NavigationViewItem _homeItem = new()
            { Icon=new FontIcon{Glyph="\uE80F",FontSize=15}, Content="Home" };
        internal readonly NavigationViewItem _versionsItem = new()
            { Icon=new FontIcon{Glyph="\uE74C",FontSize=15}, Content="Versions" };

        private readonly HomePage     _homePage;
        private readonly VersionsPage _versionsPage;
        private readonly SettingsPage _settingsPage;
        private readonly PackageCatalog _catalog;
        private readonly ApplicationSettings _settings;
        private VersionRegistry? _registry;
        private bool _disposed;

        public MainNavigationView(ApplicationSettings settings) : base(new())
        {
            _settings=settings??throw new ArgumentNullException(nameof(settings));
            _catalog=PackageCatalog.OpenForCurrentUser();
            _homePage=new(this,settings); _versionsPage=new(this); _settingsPage=new(settings);
            _homeItem.Tag=_homePage; _versionsItem.Tag=_versionsPage;

            var nav=~this;
            nav.PaneDisplayMode=NavigationViewPaneDisplayMode.Left;
            nav.IsPaneOpen=true; nav.CompactPaneLength=52; nav.OpenPaneLength=210;
            nav.UseLayoutRounding=true;
            nav.IsBackButtonVisible=NavigationViewBackButtonVisible.Collapsed;
            nav.IsSettingsVisible=true; nav.PaneTitle="360Launcher";
            nav.MenuItems.Add(_homeItem);
            nav.MenuItems.Add(new NavigationViewItemSeparator());
            nav.MenuItems.Add(_versionsItem);
            nav.SelectedItem=_homeItem; nav.Content=_homePage;
            nav.Loaded+=OnLoaded; nav.ItemInvoked+=OnItemInvoked;
        }

        static void OnItemInvoked(NavigationView sender, NavigationViewItemInvokedEventArgs args)
        {
            if(args.IsSettingsInvoked){
                if(sender.SettingsItem is NavigationViewItem si&&si.Tag is UIElement p) sender.Content=p; return;}
            if(args.InvokedItemContainer?.Tag is UIElement page) sender.Content=page;
        }

        async void OnLoaded(object s, RoutedEventArgs e)
        {
            if((~this).SettingsItem is NavigationViewItem si2) si2.Tag=_settingsPage;
            MainDialog.Current.XamlRoot=(~this).XamlRoot;
            SetButton(BtnConnect,false);

            try
            {
                if(await Client360.UpdateAvailableAsync()&&
                    (_settings.AutomaticUpdates||await MainDialog.LauncherUpdateAvailable.ShowAsync()))
                {
                    SetButton(BtnUpdating,false);
                    await Client360.DownloadUpdateAsync(pct=>
                        (~this).Dispatcher.Invoke(()=>SetButton($"{BtnUpdating}... {pct}%",false)));
                    return;
                }
            }
            catch{}

            if(!Client360.Instance.IsEmbedded){ await MainDialog.ConnectionFailure.ShowAsync(); Shutdown(); return; }

            try
            {
                _registry=await VersionRegistry.CreateAsync();
                (~this).Tag=_registry; (_homePage as FrameworkElement).Tag=_registry;
            }
            catch{ await MainDialog.ConnectionFailure.ShowAsync(); Shutdown(); return; }

            var pop=Task.Run(()=>{ foreach(var item in _registry)
                (~this).Dispatcher.Invoke(()=>_versionsPage._listBox.Items.Add(item)); });

            _catalog.PackageInstalling  +=OnInstalling;
            _catalog.PackageUninstalling+=OnUninstalling;
            _catalog.PackageUpdating    +=OnUpdating;

            RefreshStatus(); SetButton(BtnPlay,true);
            await pop; _versionsPage._button.IsEnabled=true;
        }

        void OnInstalling(PackageCatalog _,PackageInstallingEventArgs a)
        { if(a.IsComplete)(~this).Dispatcher.Invoke(()=>OnPkgChanged(a.Package.Id.FamilyName)); }
        void OnUninstalling(PackageCatalog _,PackageUninstallingEventArgs a)
        { if(a.IsComplete)(~this).Dispatcher.Invoke(()=>OnPkgChanged(a.Package.Id.FamilyName)); }
        void OnUpdating(PackageCatalog _,PackageUpdatingEventArgs a)
        { if(a.IsComplete)(~this).Dispatcher.Invoke(()=>OnPkgChanged(a.TargetPackage.Id.FamilyName)); }

        void OnPkgChanged(string name)
        { if(name.Equals(Minecraft.PackageFamilyName,StringComparison.OrdinalIgnoreCase)) RefreshStatus(); }

        void RefreshStatus()
        {
            if(!Minecraft.IsInstalled){_homePage._leftText.Text=StatusOffline;return;}
            _homePage._leftText.Text=string.Format(
                _registry?.IsSupported??false?StatusSupported:StatusUnsupported,
                VersionRegistry.InstalledVersion);
        }

        void SetButton(string c,bool en){_homePage._button.Content=c;_homePage._button.IsEnabled=en;}
        static void Shutdown()=>Application.Current.Shutdown();

        public void Dispose()
        {
            if(_disposed)return; _disposed=true;
            _catalog.PackageInstalling  -=OnInstalling;
            _catalog.PackageUninstalling-=OnUninstalling;
            _catalog.PackageUpdating    -=OnUpdating;
            (~this).Loaded-=OnLoaded; (~this).ItemInvoked-=OnItemInvoked;
        }
    }

    sealed class MainWindow : Window
    {
        public MainWindow(ApplicationSettings settings)
        {
            var helper=new WindowInteropHelper(this);
            var hwnd=(HWND)helper.EnsureHandle();
            HwndSource.FromHwnd(hwnd).AddHook(Hook);
            unsafe{ BOOL b=true; DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&b,(uint)sizeof(BOOL)); }
            using(var s=ApplicationManifest.GetResourceStream("Application.ico"))
            { Icon=BitmapFrame.Create(s,BitmapCreateOptions.PreservePixelFormat,BitmapCacheOption.OnLoad); Icon.Freeze(); }
            UseLayoutRounding=true; SnapsToDevicePixels=true;
            ResizeMode=ResizeMode.CanMinimize; SizeToContent=SizeToContent.WidthAndHeight;
            WindowStartupLocation=WindowStartupLocation.CenterScreen;
            Title="360Launcher";
            Content=new Xaml.XamlHost(~new MainNavigationView(settings)){Width=1080,Height=620,Focusable=true};
        }
        static nint Hook(nint hwnd,int msg,nint wParam,nint lParam,ref bool handled)
        {
            if(!handled&&msg==WM_SYSCOMMAND)
                switch((uint)wParam&0xFFF0){case SC_KEYMENU or SC_MOUSEMENU:handled=true;break;}
            return new();
        }
    }
}

// ════════════════════════════════════════════════════════════════════════
// ENTRY POINT
// ════════════════════════════════════════════════════════════════════════
namespace Flarial.Launcher
{
    using Flarial.Launcher.Interface;
    using Flarial.Launcher.Management;
    using Flarial.Launcher.Runtime.Modding;

    sealed class MainApplication : Application
    {
        readonly ApplicationSettings _settings;
        public MainApplication(ApplicationSettings s){_settings=s;}
        protected override void OnExit(ExitEventArgs e){base.OnExit(e);_settings.SaveSettings();}
    }

    static class Program
    {
        const string ErrFormat =
            "360Launcher crashed.\nVersion: {0}\nException: {1}\n\n{2}\n\n{3}";

        static Program()
        {
            AppDomain.CurrentDomain.UnhandledException+=OnUnhandled;
        }

        static void OnUnhandled(Exception ex)
        {
            var trace=ex.StackTrace?.Trim()??"";
            while(ex.InnerException is not null)ex=ex.InnerException;
            MessageBox.Show(string.Format(ErrFormat,
                Management.ApplicationManifest.s_version,ex.GetType().Name,ex.Message,trace),
                "360Launcher — Error",MessageBoxButton.OK,MessageBoxImage.Error);
            Environment.Exit(1);
        }

        static void OnUnhandled(object s,System.UnhandledExceptionEventArgs e)=>OnUnhandled((Exception)e.ExceptionObject);
        static void OnUnhandled(object s,Windows.UI.Xaml.UnhandledExceptionEventArgs e){e.Handled=true;OnUnhandled(e.Exception);}

        [STAThread]
        static void Main(string[] args)
        {
            using var mutex=new System.Threading.Mutex(false,"A1B2C3D4-360L-4E5F-9A8B-7C6D5E4F3A2B",out var created);
            if(!created)return;

            var path=Path.Combine(GetFolderPath(LocalApplicationData),"360Launcher");
            Environment.CurrentDirectory=Directory.CreateDirectory(path).FullName;

            var settings=Management.ApplicationSettings.ReadSettings();

            for(var i=0;i<args.Length;i++)
                if(args[i]=="--inject"&&i+1<args.Length){Injector.Launch(true,new(args[i+1]));return;}

            using(Windows.UI.Xaml.Hosting.WindowsXamlManager.InitializeForCurrentThread())
            {
                var app=Windows.UI.Xaml.Application.Current;
                app.UnhandledException+=OnUnhandled;
                app.RequestedTheme=Windows.UI.Xaml.ApplicationTheme.Dark;
                app.Resources.MergedDictionaries.Add(new Windows.UI.Xaml.Controls.ColorPaletteResources
                    {Accent=Windows.UI.Color.FromArgb(255,0,120,215)});
                new MainApplication(settings).Run(new MainWindow(settings));
            }
        }
    }
}

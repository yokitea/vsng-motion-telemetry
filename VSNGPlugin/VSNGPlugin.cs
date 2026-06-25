// ============================================================
//  Virtual Sailor NG - SimHub Telemetry Plugin
//  Menerima data UDP dari vsng_telemetry.dll (port 4445)
//  dan mengekspos properti ke SimHub Dashboard / Hardware.
//
//  Properties yang tersedia di SimHub:
//    VSNGPlugin RPM          - Putaran mesin (RPM)
//    VSNGPlugin Speed_Knots  - Kecepatan (knots)
//    VSNGPlugin Fuel         - Bahan bakar (0-100)
//    VSNGPlugin Heading      - Heading kompas (0-360 derajat)
//    VSNGPlugin EngineOn     - Status mesin (1=hidup, 0=mati)
//    VSNGPlugin LightOn      - Status lampu navigasi (1=nyala)
//    VSNGPlugin Stall        - Status stall/mesin mati (1=stall)
//    VSNGPlugin Connected    - Status koneksi VSNG (1=terhubung)
// ============================================================

using GameReaderCommon;
using SimHub.Plugins;
using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Net.WebSockets;
using System.Globalization;

namespace VSNGPlugin
{
    [PluginDescription("Receives instrument telemetry from Virtual Sailor NG via UDP port 4445")]
    [PluginAuthor("VSNG Rescue Boat Simulator")]
    [PluginName("VSNGPlugin")]
    public class VSNGDataPlugin : IPlugin, IDataPlugin
    {
        private static readonly log4net.ILog log = log4net.LogManager.GetLogger(typeof(VSNGDataPlugin));

        private UdpClient    _udpClient;
        private Thread       _listenerThread;
        private volatile bool _running    = false;

        // Nilai instrumen - gunakan object lock untuk thread safety
        private readonly object _lock = new object();
        private double _rpm      = 0;
        private double _speed    = 0;
        private double _fuel     = 0;
        private double _heading  = 0;
        private double _engineOn = 0;
        private double _lightOn  = 0;
        private double _stall    = 0;
        private double _roll     = 0;
        private double _pitch    = 0;
        private double _heave    = 0;

        // Command states
        private int          _decisionActive = 0;
        private int          _decisionVisibleA = 0;
        private int          _decisionVisibleB = 0;
        private string       _decisionA = "";
        private string       _decisionB = "";
        private bool         _isBlinking = false;
        // Timestamp penerimaan terakhir (untuk deteksi koneksi terputus)
        private DateTime _lastReceived = DateTime.MinValue;

        // WebSocket Client variables
        private ClientWebSocket _wsClient;
        private CancellationTokenSource _wsCts;
        private Task _wsTask;
        private volatile bool _wsConnected = false;

        public PluginManager PluginManager { get; set; }

        // --------------------------------------------------------
        // Init: Dipanggil SimHub saat plugin dimuat
        // --------------------------------------------------------
        public void Init(PluginManager pluginManager)
        {
            PluginManager = pluginManager;

            // Daftarkan semua properti yang akan muncul di SimHub
            pluginManager.AddProperty("RPM",         GetType(), typeof(double), "Engine RPM (from VSNG)");
            pluginManager.AddProperty("Speed_Knots", GetType(), typeof(double), "Speed in Knots (from VSNG)");
            pluginManager.AddProperty("Fuel",        GetType(), typeof(double), "Fuel level 0-100 (from VSNG)");
            pluginManager.AddProperty("Heading",     GetType(), typeof(double), "Compass heading 0-360 deg (from VSNG)");
            pluginManager.AddProperty("EngineOn",    GetType(), typeof(double), "Engine status: 1=ON, 0=OFF (from VSNG)");
            pluginManager.AddProperty("LightOn",     GetType(), typeof(double), "Navigation lights: 1=ON, 0=OFF (from VSNG)");
            pluginManager.AddProperty("Stall",       GetType(), typeof(double), "Engine stall: 1=STALL, 0=OK (from VSNG)");
            pluginManager.AddProperty("Connected",   GetType(), typeof(double), "VSNG connection: 1=OK, 0=No data");
            pluginManager.AddProperty("IOS_Connected", GetType(), typeof(double), "IOS WebSocket Connection: 1=OK, 0=Disconnected");

            pluginManager.AddProperty("Roll",        GetType(), typeof(double), "Ship Roll angle");
            pluginManager.AddProperty("Pitch",       GetType(), typeof(double), "Ship Pitch angle");
            pluginManager.AddProperty("Heave",       GetType(), typeof(double), "Ship Heave (vert vel)");

            pluginManager.AddProperty("Decision_Active", GetType(), typeof(int), "1 if decision overlay should be shown");
            pluginManager.AddProperty("Decision_Visible_A", GetType(), typeof(int), "1 if Option A is visible");
            pluginManager.AddProperty("Decision_Visible_B", GetType(), typeof(int), "1 if Option B is visible");
            pluginManager.AddProperty("Decision_OptionA", GetType(), typeof(string), "Text for Option A");
            pluginManager.AddProperty("Decision_OptionB", GetType(), typeof(string), "Text for Option B");

            pluginManager.AddAction("Select_Option_A", GetType(), (a, b) => { Task.Run(() => SendDecisionAsync("A")); });
            pluginManager.AddAction("Select_Option_B", GetType(), (a, b) => { Task.Run(() => SendDecisionAsync("B")); });

            log.Info("VSNGPlugin: Inisialisasi selesai. Mendengarkan UDP port 4445.");

            StartUdpListener();
            StartWebSocket();
        }

        // --------------------------------------------------------
        // DataUpdate: Dipanggil SimHub setiap frame (~60fps)
        // --------------------------------------------------------
        public void DataUpdate(PluginManager pluginManager, ref GameData data)
        {
            // Deteksi koneksi: tidak ada data >3 detik = terputus
            bool isConnected = (DateTime.Now - _lastReceived).TotalSeconds < 3.0;

            double rpm, spd, fuel, hdg, eng, lgt, stall, rll, ptc, hev;
            lock (_lock)
            {
                rpm   = _rpm;
                spd   = _speed;
                fuel  = _fuel;
                hdg   = _heading;
                eng   = _engineOn;
                lgt   = _lightOn;
                stall = _stall;
                rll   = _roll;
                ptc   = _pitch;
                hev   = _heave;
            }

            pluginManager.SetPropertyValue("RPM",         GetType(), rpm);
            pluginManager.SetPropertyValue("Speed_Knots", GetType(), spd);
            pluginManager.SetPropertyValue("Fuel",        GetType(), fuel);
            pluginManager.SetPropertyValue("Heading",     GetType(), hdg);
            pluginManager.SetPropertyValue("EngineOn",    GetType(), eng);
            pluginManager.SetPropertyValue("LightOn",     GetType(), lgt);
            pluginManager.SetPropertyValue("Stall",       GetType(), stall);
            pluginManager.SetPropertyValue("Roll",        GetType(), rll);
            pluginManager.SetPropertyValue("Pitch",       GetType(), ptc);
            pluginManager.SetPropertyValue("Heave",       GetType(), hev);
            pluginManager.SetPropertyValue("Connected",   GetType(), isConnected ? 1.0 : 0.0);
            pluginManager.SetPropertyValue("IOS_Connected", GetType(), _wsConnected ? 1.0 : 0.0);

            pluginManager.SetPropertyValue("Decision_Active", GetType(), _decisionActive);
            pluginManager.SetPropertyValue("Decision_Visible_A", GetType(), _decisionVisibleA);
            pluginManager.SetPropertyValue("Decision_Visible_B", GetType(), _decisionVisibleB);
            pluginManager.SetPropertyValue("Decision_OptionA", GetType(), _decisionA);
            pluginManager.SetPropertyValue("Decision_OptionB", GetType(), _decisionB);
        }

        // --------------------------------------------------------
        // End: Dipanggil SimHub saat plugin dihentikan
        // --------------------------------------------------------
        public void End(PluginManager pluginManager)
        {
            StopUdpListener();
            StopWebSocket();
        }

        // --------------------------------------------------------
        // UDP Listener
        // --------------------------------------------------------
        private void StartUdpListener()
        {
            try
            {
                _udpClient = new UdpClient(4445);
                _udpClient.Client.ReceiveTimeout = 1000; // 1 detik timeout agar thread bisa cek _running
                _running = true;
                _listenerThread = new Thread(ListenLoop);
                _listenerThread.IsBackground = true;
                _listenerThread.Name = "VSNG_UDP_Listener";
                _listenerThread.Start();
            }
            catch (Exception)
            {
                // Jika port sudah dipakai, plugin tetap jalan tapi tanpa data
            }
        }

        private void StopUdpListener()
        {
            _running = false;
            if (_udpClient != null)
            {
                try { _udpClient.Close(); } catch { }
                _udpClient = null;
            }
            if (_listenerThread != null)
            {
                _listenerThread.Join(2000);
                _listenerThread = null;
            }
        }

        private void ListenLoop()
        {
            IPEndPoint ep = new IPEndPoint(IPAddress.Any, 4445);
            while (_running)
            {
                try
                {
                    byte[] data = _udpClient.Receive(ref ep);
                    string msg  = Encoding.ASCII.GetString(data);
                    ParseMessage(msg);
                    _lastReceived = DateTime.Now;
                }
                catch (SocketException)
                {
                    // Timeout normal (ReceiveTimeout = 1000ms) - lanjut loop
                }
                catch (Exception)
                {
                    if (_running) Thread.Sleep(100);
                }
            }
        }

        // --------------------------------------------------------
        // WebSocket Client untuk mengirim ke IOS Server
        // --------------------------------------------------------
        private void StartWebSocket()
        {
            _wsCts = new CancellationTokenSource();
            _wsTask = Task.Run(() => WebSocketLoop(_wsCts.Token));
        }

        private void StopWebSocket()
        {
            if (_wsCts != null)
            {
                _wsCts.Cancel();
            }
            if (_wsTask != null)
            {
                try { _wsTask.Wait(2000); } catch { }
            }
        }

        private async Task WebSocketLoop(CancellationToken ct)
        {
            Uri serverUri = new Uri("ws://127.0.0.1:8000/ws/telemetry");

            while (!ct.IsCancellationRequested)
            {
                using (_wsClient = new ClientWebSocket())
                {
                    try
                    {
                        await _wsClient.ConnectAsync(serverUri, ct);
                        _wsConnected = true;
                        log.Info("VSNGPlugin: Terhubung ke IOS Server via WebSocket (ws://127.0.0.1:8000/ws/telemetry)");

                        // Start Receive loop as a separate unawaited task
                        var receiveTask = Task.Run(() => ReceiveLoop(_wsClient, ct), ct);

                        // Loop pengiriman data (10Hz / setiap 100ms)
                        while (_wsClient.State == WebSocketState.Open && !ct.IsCancellationRequested)
                        {
                            double rpm, spd, fuel, hdg, eng, lgt, stall, rll, ptc, hev;
                            lock (_lock)
                            {
                                rpm   = _rpm;
                                spd   = _speed;
                                fuel  = _fuel;
                                hdg   = _heading;
                                eng   = _engineOn;
                                lgt   = _lightOn;
                                stall = _stall;
                                rll   = _roll;
                                ptc   = _pitch;
                                hev   = _heave;
                            }

                            long timestamp = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
                            int gameRunning = (DateTime.Now - _lastReceived).TotalSeconds < 3.0 ? 1 : 0;

                            // Format JSON Terstandarisasi
                            string json = string.Format(CultureInfo.InvariantCulture,
                                "{{\"domain\":\"Laut\",\"timestamp\":{0},\"telemetry\":{{\"rpm\":{1},\"speed\":{2},\"fuel\":{3},\"heading\":{4},\"engine_on\":{5},\"light_on\":{6},\"stall\":{7},\"roll\":{8},\"pitch\":{9},\"heave\":{10}}},\"status\":{{\"domain\":\"Laut\",\"ios_connected\":1,\"game_running\":{11}}}}}",
                                timestamp, rpm, spd, fuel, hdg, eng, lgt, stall, rll, ptc, hev, gameRunning);

                            byte[] buffer = Encoding.UTF8.GetBytes(json);
                            var segment = new ArraySegment<byte>(buffer);

                            await _wsClient.SendAsync(segment, WebSocketMessageType.Text, true, ct);

                            await Task.Delay(100, ct); // Tunggu 100ms
                        }
                    }
                    catch (Exception ex)
                    {
                        if (_wsConnected) log.Error("VSNGPlugin: Koneksi WebSocket terputus: " + ex.Message);
                    }

                    _wsConnected = false;
                    if (_wsClient.State == WebSocketState.Open || _wsClient.State == WebSocketState.CloseReceived || _wsClient.State == WebSocketState.CloseSent)
                    {
                        try { await _wsClient.CloseAsync(WebSocketCloseStatus.NormalClosure, "Closing", CancellationToken.None); } catch { }
                    }
                }

                // Tunggu 2 detik sebelum mencoba reconnect
                if (!ct.IsCancellationRequested)
                {
                    try { await Task.Delay(2000, ct); } catch { }
                }
            }
        }

        private async Task ReceiveLoop(ClientWebSocket ws, CancellationToken ct)
        {
            var buffer = new byte[1024];
            while (ws.State == WebSocketState.Open && !ct.IsCancellationRequested)
            {
                try
                {
                    var result = await ws.ReceiveAsync(new ArraySegment<byte>(buffer), ct);
                    if (result.MessageType == WebSocketMessageType.Close) break;

                    string msg = Encoding.UTF8.GetString(buffer, 0, result.Count);
                    log.Info("VSNGPlugin: Terima perintah IOS -> " + msg);

                    // Parse JSON sederhana (show_decision / hide_decision)
                    if (msg.Contains("\"action\":\"show_decision\"") || msg.Contains("\"action\": \"show_decision\""))
                    {
                        _decisionActive = 1;
                        _decisionVisibleA = 1;
                        _decisionVisibleB = 1;
                        if (msg.Contains("option_a")) _decisionA = ExtractJsonValue(msg, "option_a");
                        if (msg.Contains("option_b")) _decisionB = ExtractJsonValue(msg, "option_b");
                    }
                    else if (msg.Contains("\"action\":\"hide_decision\"") || msg.Contains("\"action\": \"hide_decision\""))
                    {
                        _decisionActive = 0;
                        _decisionVisibleA = 0;
                        _decisionVisibleB = 0;
                    }
                }
                catch (Exception) { break; }
            }
        }

        private string ExtractJsonValue(string json, string key)
        {
            int idx = json.IndexOf("\"" + key + "\"");
            if (idx < 0) return "";
            int colonIdx = json.IndexOf(':', idx);
            if (colonIdx < 0) return "";
            int quote1 = json.IndexOf('"', colonIdx);
            if (quote1 < 0) return "";
            int quote2 = json.IndexOf('"', quote1 + 1);
            if (quote2 < 0) return "";
            return json.Substring(quote1 + 1, quote2 - quote1 - 1);
        }

        private async Task SendDecisionAsync(string selection)
        {
            if (_isBlinking) return; // Abaikan jika sedang proses blinking

            try
            {
                if (_wsConnected && _wsClient != null && _wsClient.State == WebSocketState.Open)
                {
                    _isBlinking = true;
                    
                    // Sembunyikan yang tidak dipilih
                    if (selection == "A") _decisionVisibleB = 0;
                    else if (selection == "B") _decisionVisibleA = 0;

                    // Kirim ke IOS langsung agar ter-register tanpa delay
                    string json = "{\"action\":\"decision_made\", \"selected\":\"" + selection + "\"}";
                    byte[] buffer = Encoding.UTF8.GetBytes(json);
                    await _wsClient.SendAsync(new ArraySegment<byte>(buffer), WebSocketMessageType.Text, true, CancellationToken.None);
                    log.Info("VSNGPlugin: Mengirim pilihan ke IOS -> " + json);

                    // Efek Blinking 5 detik (10 x 500ms)
                    for (int i = 0; i < 10; i++)
                    {
                        if (selection == "A") _decisionVisibleA = _decisionVisibleA == 1 ? 0 : 1;
                        else if (selection == "B") _decisionVisibleB = _decisionVisibleB == 1 ? 0 : 1;
                        await Task.Delay(500);
                    }

                    // Selesai blinking, sembunyikan semua
                    _decisionVisibleA = 0;
                    _decisionVisibleB = 0;
                    _decisionActive = 0;
                    _isBlinking = false;
                }
            }
            catch (Exception ex)
            {
                _isBlinking = false;
                log.Error("VSNGPlugin: Gagal mengirim pilihan: " + ex.Message);
            }
        }

        // --------------------------------------------------------
        // Parser: "RPM:1500.0;SPD:24.50;FUEL:85.0;HDG:180.0;ENG:1;LGT:0;STALL:0;ROLL:2.5;PTCH:-1.2;HEAV:0.3;"
        // --------------------------------------------------------
        private void ParseMessage(string msg)
        {
            string[] parts = msg.Split(';');
            lock (_lock)
            {
                foreach (string part in parts)
                {
                    if (string.IsNullOrEmpty(part)) continue;

                    int colonIdx = part.IndexOf(':');
                    if (colonIdx < 0) continue;

                    string key   = part.Substring(0, colonIdx).Trim();
                    string valStr = part.Substring(colonIdx + 1).Trim();

                    double val;
                    if (!double.TryParse(valStr, NumberStyles.Float, CultureInfo.InvariantCulture, out val))
                        continue;

                    switch (key)
                    {
                        case "RPM":   _rpm      = val; break;
                        case "SPD":   _speed    = val; break;
                        case "FUEL":  _fuel     = val; break;
                        case "HDG":   _heading  = val; break;
                        case "ENG":   _engineOn = val; break;
                        case "LGT":   _lightOn  = val; break;
                        case "STALL": _stall    = val; break;
                        case "ROLL":  _roll     = val; break;
                        case "PTCH":  _pitch    = val; break;
                        case "HEAV":  _heave    = val; break;
                    }
                }
            }
        }
    }
}

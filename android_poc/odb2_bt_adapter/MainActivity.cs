using Android;
using Android.Bluetooth;
using Android.Bluetooth.LE;
using Android.Content;
using Android.Content.PM;
using Android.OS;
using Android.Runtime;
using Android.Text.Method;
using Android.Views;
using Java.Interop;
using Java.Util;
using System.Text;

namespace odb2_bt_adapter;

[Activity(Label = "@string/app_name", MainLauncher = true)]
public class MainActivity : Activity
{
    private class AppAdvertiseCallback(MainActivity activity) : AdvertiseCallback
    {
        private readonly MainActivity mainActivity = activity;

        public override void OnStartFailure(AdvertiseFailure errorCode)
        {
            mainActivity.Log($"Advertising failed: {errorCode}");
        }

        public override void OnStartSuccess(AdvertiseSettings? settingsInEffect)
        {
            mainActivity.Log("Advertising successfully started");
        }
    }

    private class AppBluetoothGattServerCallback(MainActivity activity, Action<byte[]> rxCallback) : BluetoothGattServerCallback
    {
        private readonly MainActivity mainActivity = activity;
        private readonly Action<byte[]> rxCallback = rxCallback;
        private BluetoothDevice? connectedDevice;

        public void Tx(byte[] data)
        {
            if (mainActivity.IsGattConnected)
            {
                mainActivity.LogTxRx("GATT TX:", data);
#pragma warning disable CA1422 // Validate platform compatibility
                //var existing = mainActivity.btGattChar?.GetValue();
                //if (existing?.SequenceEqual(data) == true)
                //{
                //    mainActivity.btGattServer?.NotifyCharacteristicChanged(connectedDevice, mainActivity.btGattChar, false);
                //}
                //else
                //{
                //    mainActivity.btGattChar?.SetValue(data);
                //}
                mainActivity.btGattChar?.SetValue(data);
                mainActivity.btGattServer?.NotifyCharacteristicChanged(connectedDevice, mainActivity.btGattChar, false);
#pragma warning restore CA1422 // Validate platform compatibility
            }
            else
            {
                mainActivity.LogTxRx("DROPPING GATT TX:", data);
            }
        }

        public override void OnCharacteristicWriteRequest(BluetoothDevice? device, int requestId, BluetoothGattCharacteristic? characteristic, bool preparedWrite, bool responseNeeded, int offset, byte[]? value)
        {
            mainActivity.Log($"OnCharacteristicWriteRequest(<dev>, {requestId}, <char>, {preparedWrite}, {responseNeeded}, {offset}, <data>)");
            base.OnCharacteristicWriteRequest(device, requestId, characteristic, preparedWrite, responseNeeded, offset, value);

            rxCallback(value!);

            try
            {
                if (Encoding.ASCII.GetString(value!) == "ATZ\r")
                {
                    mainActivity.Log("Received ATZ command, waiting 500ms");
                    Thread.Sleep(500);
                }
            }
            catch { }

            if (responseNeeded)
            {
                mainActivity.btGattServer?.SendResponse(
                    device!,
                    requestId,
                    GattStatus.Success,
                    offset,
                    []);
            }

            //Tx(Encoding.ASCII.GetBytes("\r\rELM327 v1.4b\r\r"));
        }

        public override void OnDescriptorReadRequest(BluetoothDevice? device, int requestId, int offset, BluetoothGattDescriptor? descriptor)
        {
            mainActivity.Log($"OnDescriptorReadRequest(<dev>, {requestId}, {offset}, <desc>)");
            base.OnDescriptorReadRequest(device, requestId, offset, descriptor);
        }

        public override void OnDescriptorWriteRequest(BluetoothDevice? device, int requestId, BluetoothGattDescriptor? descriptor, bool preparedWrite, bool responseNeeded, int offset, byte[]? value)
        {
            mainActivity.Log($"OnDescriptorWriteRequest(<dev>, {requestId}, <desc>, {preparedWrite}, {responseNeeded}, {offset}, <data>)");
            mainActivity.LogTxRx("CCCD write", value!);
            base.OnDescriptorWriteRequest(device, requestId, descriptor, preparedWrite, responseNeeded, offset, value);

            if (responseNeeded)
            {
                mainActivity.btGattServer?.SendResponse(
                    device!,
                    requestId,
                    GattStatus.Success,
                    offset,
                    Encoding.ASCII.GetBytes("OK\r\n"));
            }
        }

        public override void OnConnectionStateChange(BluetoothDevice? device, [GeneratedEnum] ProfileState status, [GeneratedEnum] ProfileState newState)
        {
            mainActivity.Log($"OnConnectionStateChange(<dev>, {status}, {newState})");
            base.OnConnectionStateChange(device, status, newState);
            if (newState == ProfileState.Connected)
            {
                connectedDevice = device;
            }
            else if (newState == ProfileState.Disconnected)
            {
                connectedDevice = null;
            }
        }
    }

    private bool autoScroll = true;
    private BluetoothAdapter? btAdapter;
    private BluetoothManager? btManager;
    private BluetoothLeAdvertiser? bleAdvertiser;
    private BluetoothGattServer? btGattServer;
    private BluetoothGattService? btGattService;
    private AppBluetoothGattServerCallback? gattCallback;
    private BluetoothGattCharacteristic? btGattChar;
    private BluetoothSocket? btRfcommSocket;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);

        // Set our view from the "main" layout resource
        SetContentView(Resource.Layout.activity_main);

        var logView = FindViewById<TextView>(Resource.Id.outputTextView)!;
        logView.MovementMethod = new ScrollingMovementMethod();

        gattCallback = new AppBluetoothGattServerCallback(this, OnGattRx);
    }

    public void LogTxRx(string prefix, byte[] data)
    {
        string s;
        try { s = $"'{Encoding.ASCII.GetString(data)}'"; } catch { s = "<invalid>"; }
        string hex = BitConverter.ToString(data).Replace("-", "");
        Log($"{prefix} {hex}={s}");
    }

    private void Log(string msg)
    {
        RunOnUiThread(() =>
        {
            var timestamp = DateTime.Now.ToString("HH:mm:ss.fff");
            var view = FindViewById<TextView>(Resource.Id.outputTextView);
            view!.Append($"{timestamp} {msg}\n");

            if (autoScroll)
            {
                int scrollAmount = (view.Layout?.GetLineTop(view.LineCount) ?? 0) - view.Height;
                if (scrollAmount > 0)
                {
                    view.ScrollTo(0, scrollAmount);
                }
                else
                {
                    view.ScrollTo(0, 0);
                }
            }
        });
    }

    private bool IsGattConnected => btGattServer != null;
    private bool IsRfcommConnected => btRfcommSocket?.IsConnected == true;

    private void OnGattRx(byte[] data)
    {
        LogTxRx("GATT RX:", data);
        RfcommTx(data);
    }

    private void OnRfcommRx(byte[] data)
    {
        LogTxRx("RFCM RX:", data);
        gattCallback!.Tx(data);
    }

    private void RfcommTx(byte[] data)
    {
        if (IsRfcommConnected)
        {
            LogTxRx("RFCM TX:", data);
            btRfcommSocket!.OutputStream!.Write(data);
        }
        else
        {
            LogTxRx("DROPPING RFCM TX:", data);
        }
    }

    public void RequestBluetoothPermissions()
    {
        var permissions = new string[]
        {
            Manifest.Permission.BluetoothConnect,
            Manifest.Permission.BluetoothScan,
            Manifest.Permission.BluetoothAdvertise,
            Manifest.Permission.AccessFineLocation,
        };

        bool granted = true;
        foreach (var permission in permissions)
        {
            if (CheckSelfPermission(permission) != Permission.Granted)
            {
                granted = false;
                break;
            }
        }

        if (!granted)
        {
            Log("Requesting permissions");
            RequestPermissions(permissions, 0);
        }
        else
        {
            Log("Permissions already granted");
        }
    }

    private void InitBluetooth()
    {
        if (btManager != null)
        {
            return;
        }

#pragma warning disable CA1422 // Validate platform compatibility
        btAdapter = BluetoothAdapter.DefaultAdapter;
#pragma warning restore CA1422 // Validate platform compatibility
        if (btAdapter == null)
        {
            Log("BT Adapter null");
            return;
        }

        bleAdvertiser = btAdapter.BluetoothLeAdvertiser;
        if (bleAdvertiser == null)
        {
            Log("BT Advertiser null");
            return;
        }

        btManager = (BluetoothManager?)GetSystemService(Context.BluetoothService);
        if (btManager == null)
        {
            Log("BT Manager null");
            return;
        }

        Log("BT init success");
    }

    [Export("PermissionsButton_Clicked")]
    public void PermissionsButton_Clicked(View view)
    {
        _ = view;
        RequestBluetoothPermissions();
    }

    [Export("AdvertiseBleButton_Clicked")]
    public void AdvertiseBleButton_Clicked(View view)
    {
        _ = view;
        Log("AdvertiseBleButton_Clicked");
        try
        {
            InitBluetooth();
            if (bleAdvertiser == null || btManager == null)
            {
                return;
            }

            var settings = new AdvertiseSettings.Builder()
                    .SetConnectable(true)!
                    .Build()!;
            var advertiseData = new AdvertiseData.Builder()
                                .SetIncludeDeviceName(true)!
                                .SetIncludeTxPowerLevel(true)!
                                .Build()!;
            var scanResponseData = new AdvertiseData.Builder()
                                .AddServiceUuid(new ParcelUuid(UUID.FromString("000018f0-0000-1000-8000-00805f9b34fb")))!
                                .SetIncludeTxPowerLevel(true)!
                                .Build()!;
            bleAdvertiser.StartAdvertising(settings, advertiseData, scanResponseData, new AppAdvertiseCallback(this));

            btGattServer = btManager.OpenGattServer(this, gattCallback);
            if (btGattServer == null)
            {
                Log("BT Gatt Server null");
                return;
            }

            btGattService = new BluetoothGattService(UUID.FromString("000018f0-0000-1000-8000-00805f9b34fb"), GattServiceType.Primary);
            btGattChar = new BluetoothGattCharacteristic(
                UUID.FromString("5d041c74-e598-488d-9349-e68a2996a1d1"),
                GattProperty.Notify | GattProperty.Write,
                GattPermission.Write);
            var btGattDescriptor = new BluetoothGattDescriptor(
                UUID.FromString("00002902-0000-1000-8000-00805f9b34fb"),
                GattDescriptorPermission.Read | GattDescriptorPermission.Write);
            btGattChar.AddDescriptor(btGattDescriptor);
            btGattService.AddCharacteristic(btGattChar);
            btGattServer.AddService(btGattService);

            Log("AdvertiseBleButton_Clicked success");
        }
        catch (Exception e)
        {
            Log($"AdvertiseBleButton_Clicked exception: {e.Message}");
        }
    }

    [Export("ConnectRfcommButton_Clicked")]
    public void ConnectRfcommButton_Clicked(View view)
    {
        _ = view;
        Log("ConnectRfcommButton_Clicked");
        try
        {
            InitBluetooth();
            if (btAdapter == null)
            {
                return;
            }

            var bondedDevs = btAdapter.BondedDevices;
            if (bondedDevs == null || bondedDevs.Count == 0)
            {
                Log("No bonded devices found");
                return;
            }
            Log($"Found {bondedDevs.Count} bonded device(s)");
            var btDevice = bondedDevs.First();

            btRfcommSocket = btDevice.CreateInsecureRfcommSocketToServiceRecord(
                UUID.FromString("00001101-0000-1000-8000-00805f9b34fb"));
            if (btRfcommSocket == null)
            {
                Log("CreateInsecureRfcommSocketToServiceRecord returned null");
                return;
            }

            btRfcommSocket.Connect();
            Log("ConnectRfcommButton_Clicked connected");

            Task.Run(() =>
            {
                try
                {
                    var data = new byte[32];
                    while (btRfcommSocket.IsConnected)
                    {
                        int read = btRfcommSocket.InputStream!.ReadAtLeast(data, 1, throwOnEndOfStream: false);
                        var actualData = data[..read];
                        string s;
                        try { s = $"'{Encoding.ASCII.GetString(actualData)}'"; } catch { s = "<invalid>"; }
                        string hex = BitConverter.ToString(actualData).Replace("-", "");
                        Log($"RFComm read {hex}={s}");
                        OnRfcommRx(actualData);
                        Thread.Sleep(10);
                    }
                }
                catch (Exception ex)
                {
                    Log($"RFComm read exception: {ex.Message}");
                }
            });

        }
        catch (Exception e)
        {
            Log($"ConnectRfcommButton_Clicked exception: {e.Message}");
        }
    }

    [Export("AutoScrollButton_Clicked")]
    public void AutoScrollButton_Clicked(View view)
    {
        autoScroll = !autoScroll;
    }
}

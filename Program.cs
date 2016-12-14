using System;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

using NetMQ;
using NetMQ.zmq;



namespace csharp_viewer
{
    public struct PROCESS_INFORMATION
    {
        public IntPtr hProcess;
        public IntPtr hThread;
        public uint dwProcessId;
        public uint dwThreadId;
    }

    public struct STARTUPINFO
    {
        public uint cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public uint dwX;
        public uint dwY;
        public uint dwXSize;
        public uint dwYSize;
        public uint dwXCountChars;
        public uint dwYCountChars;
        public uint dwFillAttribute;
        public uint dwFlags;
        public short wShowWindow;
        public short cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }


    public struct SECURITY_ATTRIBUTES
    {
        public int length;
        public IntPtr lpSecurityDescriptor;
        public bool bInheritHandle;
    }


    // copy from following link:
    // http://msdn.microsoft.com/en-us/library/windows/desktop/ms684863%28v=vs.85%29.aspx
    [Flags]
    internal enum ProcessCreationFlags : uint
    {
        ZERO_FLAG = 0x00000000,
        CREATE_BREAKAWAY_FROM_JOB       = 0x01000000,
        CREATE_DEFAULT_ERROR_MODE       = 0x04000000,
        CREATE_NEW_CONSOLE              = 0x00000010,
        CREATE_NEW_PROCESS_GROUP        = 0x00000200,
        CREATE_NO_WINDOW                = 0x08000000,
        CREATE_PROTECTED_PROCESS        = 0x00040000,
        CREATE_PRESERVE_CODE_AUTHZ_LEVEL = 0x02000000,
        CREATE_SEPARATE_WOW_VDM         = 0x00001000,
        CREATE_SHARED_WOW_VDM           = 0x00001000,
        CREATE_SUSPENDED                = 0x00000004,
        CREATE_UNICODE_ENVIRONMENT      = 0x00000400,
        DEBUG_ONLY_THIS_PROCESS         = 0x00000002,
        DEBUG_PROCESS                   = 0x00000001,
        DETACHED_PROCESS                = 0x00000008,
        EXTENDED_STARTUPINFO_PRESENT    = 0x00080000,
        INHERIT_PARENT_AFFINITY         = 0x00010000,
    }

    // copy from this link:
    // http://msdn.microsoft.com/en-us/library/windows/desktop/ms687032%28v=vs.85%29.aspx
    internal enum ProcessExitCode : long
    {
        WAIT_ABANDONED      = 0x00000080L,
        WAIT_OBJECT_0       = 0x00000000L,
        WAIT_TIMEOUT        = 0x00000102L,
        WAIT_FAILED         = 0xFFFFFFFF,
    }


    class Program
    {
        public class VideoBufferPool : IBufferPool
        {
            private byte[] m_buffer;

            public VideoBufferPool(int bufferSize)
            {
                m_buffer = new byte[bufferSize];
            }

            public byte[] Take(int size)
            {
                return m_buffer;
            }

            public void Return(byte[] buffer)
            {
                // do nothing here
            }
        }


        static void Main(string[] args)
        {

            /****
             * 1. 在盯店宝中启动视频分析后台: sk_main -r rtstp_url -p admin_port(eg. 8964)
             *    其中 rtsp_url 是查找到的IPC rtsp地址, 
             *    admin_port 是指定的后台管理端口, 盯店宝通过这个端口和后台通讯
             *    
             */
            const int adminPort = 8965;
            const string rtspUrl = "rtsp://admin:admin@192.168.0.111:554";
            const string SK_MAIN_EXE = "C:\\workspace\\shop_keeper\\Debug\\shop_keeper.exe";
            // const string SK_MAIN_EXE = "C:\\workspace\\shop_keeper\\Release\\shop_keeper.exe";
            STARTUPINFO si = new STARTUPINFO();
            PROCESS_INFORMATION pi = new PROCESS_INFORMATION();
            CreateProcess(null, 
                          SK_MAIN_EXE + " -r " + rtspUrl + " -p " + adminPort, 
                          IntPtr.Zero, IntPtr.Zero, false,
                          (uint)(ProcessCreationFlags.CREATE_NO_WINDOW | ProcessCreationFlags.DETACHED_PROCESS), 
                          IntPtr.Zero, null, ref si, out pi);

            CloseHandle(pi.hThread);

            //Waiting for sk_main starting
            int rc = WaitForSingleObject(pi.hProcess, 1000);
            if (rc == (int)ProcessExitCode.WAIT_OBJECT_0)
            {
                Console.WriteLine("sk_main exit with error");
                Environment.Exit(-1);
            }

            /* the following codes handle multiple sk_main processes
             * 
             * IntPtr[] handleList = new IntPtr[4];
             * for (int i = 0; i < 4; ++i) handleList[i] = pi.hProcess;
             * WaitForMultipleObjects(4, handleList, false, timeout);
             * 
             */

            /****
             * 2. 视频分析后台启动后, 盯店宝连接 tcp://127.0.0.1:admin_port 
             *    如下例子中的 adminUrl
             *    
             */
            string adminUrl = "tcp://127.0.0.1:" + adminPort;
            int width = 0, height = 0;
            int fps = 0, dataPort = 0, msgPort = 0;

            BufferPool.SetCustomBufferPool(new VideoBufferPool(1024 * 1024 * 3));
            using (NetMQContext ctx = NetMQContext.Create())
            {
                using (var req = ctx.CreateRequestSocket())
                {
                    req.Connect(adminUrl);

                    /****
                     * 3. 盯店宝给后台发送 info 指令， 获得5个数字, 按顺序分别是:
                     *    视频的 width, height, FPS (帧率)
                     *    视频图像发布的端口 dataPort
                     *    后台发布消息的端口 msgPort, 消息格式为: cmd: args splited with space/comma
                     *    消息类型参见 sk.hpp 定义
                     * 
                     */
                    req.Send("info");
                    string msg = req.ReceiveString();
                    Console.WriteLine("From Server: {0}", msg);

                    char[] delims = { ' ', '\t' };
                    string[] vals = msg.Split(delims);
                    width = int.Parse(vals[0]);
                    height = int.Parse(vals[1]);
                    fps = int.Parse(vals[2]);
                    dataPort = int.Parse(vals[3]);
                    msgPort = int.Parse(vals[4]);

                    /*****
                     * 4. 盯店宝通过 store 指令让后台把获得的视频存储成文件
                     *    每隔3分钟存一个视频文件, 文件存好后, 
                     *    后台发送 nvideo 消息
                     * 
                     */
                    req.Send("store");
                    msg = req.ReceiveString();
                    Console.WriteLine("From Server: {0}", msg);

                    /****
                     * 5. 发送指令 track, 指定在某个范围跟踪人脸
                     *    指令和参数以 ":" 分隔
                     *    范围定义是: x,y,width,height
                     *    指定一个矩形区域, 参数用 "," or " " 分隔
                     * 
                     */
                    req.Send("track:14 0 256 336");
                    msg = req.ReceiveString();
                    Console.WriteLine("Response from server: {0}", msg);

                    /****
                     * 5.1 或者 发送指令 alarm, 指定在某个范围检测越界提醒
                     *    指令和参数以 ":" 分隔
                     *    范围定义是: x,y,width,height
                     *    指定一个矩形区域, 参数用 "," or " " 分隔
                     */
                    req.Send("alarm:370,0,256,336");
                    msg = req.ReceiveString();
                    Console.WriteLine("Response from server: {0}", msg);


                    /****
                     * 6. clean 指令清除 sqlite 记录的历史数据 (optional)
                     *    参数 10 指清除10天以前的记录
                     * 
                     */
                    req.Send("clean:10");
                    msg = req.ReceiveString();
                    Console.WriteLine("Response from server: {0}", msg);
                }

                using (var client = ctx.CreateSubscriberSocket())
                {
                    /****
                     * 7. 连接 dataPort, 获取视频图像数据
                     * 
                     */
                    client.Connect("tcp://127.0.0.1:" + dataPort);
                    client.Subscribe("");

                    /****
                     * 8. 连接 msgPort, 获取 实时消息, including
                     *    alarm:     闯入提醒
                     *    face:      检测到人脸, welcome
                     *    nvideo:    新的视频文件生成
                     *    shutdown:  后台将关闭
                     *    参见 sk.hpp
                     * 
                     */
                    var sub = ctx.CreateSubscriberSocket();
                    sub.Connect("tcp://127.0.0.1:" + msgPort);

                    byte[] data = new byte[width * height * 3];
                    int counter = 0;
                    while (counter++ < 300)
                    {
                        var message = new Msg();
                        message.InitPool(width * height * 3);
                        client.Receive(ref message, SendReceiveOptions.None);
                        Buffer.BlockCopy(message.Data, 0, data, 0, message.Size);
                        Console.WriteLine("mesage Size: {0}", message.Size);
                        message.Close();
                        Thread.Sleep(15);

                        string m = null;
                        try
                        {
                            m = sub.ReceiveString(SendReceiveOptions.DontWait);
                            if (m == null) continue;
                        }
                        catch (AgainException)
                        {
                            continue;
                        }

                        if ("alarm".Equals(m))
                        {
                            Console.WriteLine("recv alarm");
                        }
                        else if ("face".Equals(m))
                        {
                            Console.WriteLine("Welcome, somebody");
                        }
                        else if ("shutdown".Equals(m))
                        {
                            Console.WriteLine("Deamon process shutdown");
                        }
                        else
                        {
                            Console.WriteLine("Recv msg unknown: {0}", m);
                        }
                    }

                    sub.Close();
                }

                using (var req = ctx.CreateRequestSocket())
                {
                    req.Connect(adminUrl);

                    /****
                     * 9. stats 指令获取当日的人脸总计
                     * 
                     */
                    req.Send("stats");
                    string msg = req.ReceiveString();
                    Console.WriteLine("Response from server: {0}", msg);

                    /****
                     * 10. reset 指令清除所有的报警和跟踪定义
                     * 
                     */
                    req.Send("reset");
                    msg = req.ReceiveString();
                    Console.WriteLine("Response from server: {0}", msg);

                    Thread.Sleep(1000);


                    /****
                     * 11. shutdown 指令 关闭视频分析后台
                     * 
                     */
                    req.Send("shutdown");
                    msg = req.ReceiveString();
                    Console.WriteLine("Shutdown Response From Server: {0}", msg);

                    /***
                     * Or Call WIN32 API to kill sk_main process
                     * TerminateProcess(pi.hProcess, 0)
                     * 
                     */
                }
            }
        }


        /****
         * Import WIN32 API to create process without console
         *
         */

        [DllImport("kernel32.dll")]
        static extern bool CreateProcess(string lpApplicationName,
                                         string lpCommandLine,
                                         IntPtr lpProcessAttributes,
                                         IntPtr lpThreadAttributes,
                                         bool bInheritHandles,
                                         uint dwCreationFlags,
                                         IntPtr lpEnvironment,
                                         string lpCurrentDirectory,
                                         ref STARTUPINFO lpStartupInfo,
                                         out PROCESS_INFORMATION lpProcessInformation);

        [DllImport("kernel32.dll")]
        static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll")]
        static extern int WaitForSingleObject(IntPtr hHandle, int dwMilliseconds);

        [DllImport("kernel32.dll")]
        static extern int WaitForMultipleObjects(int nCount, IntPtr[] lpHandles, bool bWaitAll, int dwMilliseconds);

        [DllImport("kernel32.dll")]
        static extern bool TerminateProcess(IntPtr hProcess, uint uExitCode);

    }

}

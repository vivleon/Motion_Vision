using System;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace MFCServer1
{
    internal static class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            Console.WriteLine("[SERVER] C# TCP Inspection Server Starting...");

            var pythonService = new PythonTcpClient("127.0.0.1", 8008);

            var dbService = new DatabaseService(
                "Server=127.0.0.1;Database=qcdb;Uid=root;Pwd=1234;Charset=utf8mb4;"
            );

            var tcpServer = new TcpInspectionServer(
                9001,
                pythonService,
                dbService
            );

            Task.Run(delegate
            {
                ServerMonitor.TcpListening = true;
                Console.WriteLine("[SERVER] Listening on TCP port 9001...");
                tcpServer.StartSync();
            });

            // ✅ 다시 살림: 이제 안전해졌음
            Task.Run(async delegate
            {
                await ServerMonitor.StartHealthCheck(pythonService);
            });

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Form2());
        }
    }
}

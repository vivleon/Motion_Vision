using System;
using System.Windows.Forms;

namespace MFCServer1
{
    internal static class Program
    {
        [STAThread]
        static void Main()
        {
            // 클라가 보내는 포트 = 9000
            var server = new TcpInspectionServer(9000);
            server.Start();

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Form2());

            server.Stop();
        }
    }
}

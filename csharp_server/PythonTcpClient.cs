using System;
using System.Net.Sockets;
using System.Text;
using System.IO;
using System.Threading.Tasks;

namespace MFCServer1
{
    public class PythonTcpClient
    {
        private readonly string _ip;
        private readonly int _port;

        // ✅ 헬스체크 연결 유지용
        private TcpClient _healthClient;
        private NetworkStream _healthStream;

        public PythonTcpClient(string ip, int port)
        {
            _ip = ip;
            _port = port;
        }

        // =============================
        // Dual 이미지 분석 (TOP + SIDE)
        // =============================
        public async Task<string> AnalyzeDualAsync(string topPath, string sidePath)
        {
            Console.WriteLine("[PYCLIENT] Connecting to Python...");

            byte[] topBytes = File.ReadAllBytes(topPath);
            byte[] sideBytes = File.ReadAllBytes(sidePath);

            using (TcpClient cli = new TcpClient())
            {
                await cli.ConnectAsync(_ip, _port);
                Console.WriteLine("[PYCLIENT] Connected to Python");

                using (NetworkStream ns = cli.GetStream())
                {
                    ns.WriteByte(0x02);
                    Console.WriteLine("[PYCLIENT] Sent msg_type 0x02");

                    byte[] lenTop = BitConverter.GetBytes(topBytes.Length);
                    ns.Write(lenTop, 0, 4);
                    ns.Write(topBytes, 0, topBytes.Length);
                    Console.WriteLine("[PYCLIENT] Sent TOP image length=" + topBytes.Length);

                    byte[] lenSide = BitConverter.GetBytes(sideBytes.Length);
                    ns.Write(lenSide, 0, 4);
                    ns.Write(sideBytes, 0, sideBytes.Length);
                    Console.WriteLine("[PYCLIENT] Sent SIDE image length=" + sideBytes.Length);

                    // 응답 수신
                    using (var ms = new MemoryStream())
                    {
                        byte[] buf = new byte[4096];
                        int read;
                        do
                        {
                            read = ns.Read(buf, 0, buf.Length);
                            if (read > 0)
                                ms.Write(buf, 0, read);
                        } while (read > 0);

                        string result = Encoding.UTF8.GetString(ms.ToArray());
                        Console.WriteLine("[PYCLIENT] Received response: " + result);
                        return result;
                    }
                }
            }
        }

        // ======================================
        // Single 이미지 분석 (TOP 또는 SIDE 중 하나)
        // ======================================
        public async Task<string> AnalyzeSingleAsync(string imgPath, string camLabel)
        {
            byte[] imgBytes = File.ReadAllBytes(imgPath);

            using (TcpClient cli = new TcpClient())
            {
                await cli.ConnectAsync(_ip, _port);
                using (NetworkStream ns = cli.GetStream())
                {
                    // mode 0x03 : single 분석 요청
                    ns.WriteByte(0x03);

                    // 카메라 라벨 (예: "top" / "side")
                    byte[] labelBytes = Encoding.UTF8.GetBytes(camLabel ?? "");
                    byte[] lenLabel = BitConverter.GetBytes(labelBytes.Length);
                    ns.Write(lenLabel, 0, 4);
                    ns.Write(labelBytes, 0, labelBytes.Length);

                    // 이미지 데이터
                    byte[] lenImg = BitConverter.GetBytes(imgBytes.Length);
                    ns.Write(lenImg, 0, 4);
                    ns.Write(imgBytes, 0, imgBytes.Length);

                    // 응답(JSON) 받기
                    using (var ms = new MemoryStream())
                    {
                        byte[] buf = new byte[4096];
                        int read;
                        do
                        {
                            read = ns.Read(buf, 0, buf.Length);
                            if (read > 0)
                                ms.Write(buf, 0, read);
                        } while (read > 0);

                        return Encoding.UTF8.GetString(ms.ToArray());
                    }
                }
            }
        }

        // ======================================
        // ✅ Python 서버 헬스체크용 Ping (연결 유지)
        // ======================================
        public async Task<bool> CheckHealthAsync()
        {
            try
            {
                // 처음 한 번만 연결
                if (_healthClient == null || !_healthClient.Connected)
                {
                    _healthClient = new TcpClient();
                    await _healthClient.ConnectAsync(_ip, _port);
                    _healthStream = _healthClient.GetStream();
                }

                // 헬스체크 신호 전송
                _healthStream.WriteByte(0x01);
                _healthStream.Flush();

                // 응답 수신
                byte[] buf = new byte[16];
                int len = _healthStream.Read(buf, 0, buf.Length);
                string resp = Encoding.UTF8.GetString(buf, 0, len);

                return resp.Contains("OK");
            }
            catch
            {
                // 연결이 끊긴 경우 재시도할 수 있게 초기화
                _healthClient?.Close();
                _healthClient = null;
                _healthStream = null;
                return false;
            }
        }
    }
}

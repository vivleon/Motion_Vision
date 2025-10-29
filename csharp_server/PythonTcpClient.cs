using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

namespace MFCServer1
{
    public class PythonTcpClient
    {
        private readonly string _host;
        private readonly int _port;

        public PythonTcpClient(string host, int port)
        {
            _host = host;
            _port = port;
        }

        // 단일 이미지 분석 요청
        // 프로토콜:
        //   [0x03]
        //   [4바이트 label length][label utf-8]
        //   [4바이트 image length][image bytes]
        // 서버는 JSON 문자열 보내고 연결 끊는다.
        public async Task<string> AnalyzeSingleAsync(string imagePath, string cameraLabel)
        {
            Console.WriteLine($"[PythonTcpClient] connecting to {_host}:{_port}...");

            using (TcpClient client = new TcpClient())
            {
                await client.ConnectAsync(_host, _port);
                Console.WriteLine("[PythonTcpClient] connected.");

                using (NetworkStream ns = client.GetStream())
                {
                    // 준비
                    byte[] imgBytes = File.ReadAllBytes(imagePath);
                    byte[] labelBytes = Encoding.UTF8.GetBytes(cameraLabel ?? "");

                    // 모드 바이트 0x03
                    await ns.WriteAsync(new byte[] { 0x03 }, 0, 1);

                    // 라벨 길이 (4바이트 little endian)
                    byte[] lbLen = BitConverter.GetBytes(labelBytes.Length);
                    await ns.WriteAsync(lbLen, 0, 4);

                    // 라벨 내용
                    if (labelBytes.Length > 0)
                        await ns.WriteAsync(labelBytes, 0, labelBytes.Length);

                    // 이미지 길이
                    byte[] imgLen = BitConverter.GetBytes(imgBytes.Length);
                    await ns.WriteAsync(imgLen, 0, 4);

                    // 이미지 바이트
                    await ns.WriteAsync(imgBytes, 0, imgBytes.Length);

                    Console.WriteLine("[PythonTcpClient] sent label+image. waiting for AI response...");

                    // 파이썬은 json 쏘고 바로 close하니까, 읽을 수 있을 때까지 모아서 받자
                    using (var ms = new MemoryStream())
                    {
                        byte[] buf = new byte[4096];
                        while (true)
                        {
                            int n;
                            try
                            {
                                n = await ns.ReadAsync(buf, 0, buf.Length);
                            }
                            catch
                            {
                                break;
                            }

                            if (n <= 0)
                                break;

                            ms.Write(buf, 0, n);
                        }

                        string json = Encoding.UTF8.GetString(ms.ToArray());
                        Console.WriteLine("[PythonTcpClient] got response: " + json);
                        return json;
                    }
                }
            }
        }

        // Health Check 용 (선택)
        // 프로토콜:
        //   [0x01]
        // 응답: "OK"
        public async Task<bool> HealthCheckAsync()
        {
            Console.WriteLine($"[PythonTcpClient] healthcheck {_host}:{_port}...");
            using (TcpClient client = new TcpClient())
            {
                await client.ConnectAsync(_host, _port);
                using (NetworkStream ns = client.GetStream())
                {
                    // 0x01 전송
                    await ns.WriteAsync(new byte[] { 0x01 }, 0, 1);

                    byte[] buf = new byte[64];
                    int n = await ns.ReadAsync(buf, 0, buf.Length);
                    string resp = Encoding.UTF8.GetString(buf, 0, n);

                    Console.WriteLine("[PythonTcpClient] health resp: " + resp);
                    return resp.Trim().ToUpper().Contains("OK");
                }
            }
        }
    }
}

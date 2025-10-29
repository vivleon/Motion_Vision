using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

namespace MFCServer1
{
    public class TcpInspectionServer
    {
        private readonly int _port;
        private TcpListener _listener;
        private bool _running;

        // 파이썬 AI 서버 정보 (ai_server.py)
        private readonly string _pyHost = "10.10.21.110";
        private readonly int _pyPort = 8009;

        // 최근 받은 사진 캐시
        // 세션/동시성 처리 안 하고 단일 라인 전송만 가정
        private string _pendingTopPath = null;
        private string _pendingFrontPath = null;

        public TcpInspectionServer(int port)
        {
            _port = port;
        }

        public void Start()
        {
            if (_running) return;
            _running = true;

            _listener = new TcpListener(IPAddress.Any, _port);
            _listener.Start();

            Console.WriteLine("[TcpInspectionServer] listening on port " + _port);
            ServerMonitor.UpdateServerStatus(true, _port);

            _ = Task.Run(AcceptLoop);
        }

        public void Stop()
        {
            _running = false;
            try { _listener.Stop(); } catch { }

            Console.WriteLine("[TcpInspectionServer] stopped.");
            ServerMonitor.UpdateServerStatus(false, _port);
        }

        private async Task AcceptLoop()
        {
            while (_running)
            {
                TcpClient client = null;
                try
                {
                    client = await _listener.AcceptTcpClientAsync();
                }
                catch
                {
                    if (!_running) break;
                }

                if (client != null)
                {
                    _ = Task.Run(() => HandleClient(client));
                }
            }
        }

        private async Task HandleClient(TcpClient client)
        {
            using (client)
            {
                NetworkStream ns = null;

                try



                {
                    var remote = (IPEndPoint)client.Client.RemoteEndPoint;
                    string remoteIp = remote.Address.ToString();
                    ServerMonitor.UpdateClientInfo(remoteIp);
                    Console.WriteLine("[TcpInspectionServer] client connected: " + remoteIp);

                    ns = client.GetStream();

                    // ---------------------------
                    // 1) 길이(4바이트, network byte order = big endian)
                    // ---------------------------
                    byte[] lenBuf = new byte[4];
                    if (await ReadExactAsync(ns, lenBuf, 0, 4) < 4)
                    {
                        Console.WriteLine("[TcpInspectionServer] client disconnected before len");
                        return;
                    }

                    // 클라에서 htonl 해서 보냈으니까 여기서 ntohl
                    int netLen = BitConverter.ToInt32(lenBuf, 0);
                    int imgLen = IPAddress.NetworkToHostOrder(netLen);

                    if (imgLen <= 0 || imgLen > 100_000_000)
                    {
                        Console.WriteLine("[TcpInspectionServer] invalid imgLen: " + imgLen);
                        return;
                    }

                    // ---------------------------
                    // 2) 이미지 본문 수신
                    // ---------------------------
                    byte[] imgBuf = new byte[imgLen];
                    if (await ReadExactAsync(ns, imgBuf, 0, imgLen) < imgLen)
                    {
                        Console.WriteLine("[TcpInspectionServer] disconnected mid-image");
                        return;
                    }

                    Console.WriteLine("[TcpInspectionServer] received image bytes=" + imgLen);

                    // ---------------------------
                    // 3) 디스크에 저장
                    // ---------------------------
                    string saveDir = @"C:\captures";
                    Directory.CreateDirectory(saveDir);

                    // 이름: yyyyMMdd_HHmmss_fff_top/front 나중에 결정
                    // 일단 generic 이름 하나 만들어두고, 나중에 top/front 결정해서 다시 rename해도 되고
                    // 여기선 그냥 지금 시각으로 고유 파일 만들고 그대로 씀
                    string fileName = DateTime.Now.ToString("yyyyMMdd_HHmmss_fff") + ".jpg";
                    string fullPath = Path.Combine(saveDir, fileName);

                    File.WriteAllBytes(fullPath, imgBuf);
                    Console.WriteLine("[TcpInspectionServer] saved " + fullPath);

                    // ---------------------------
                    // 4) top / front 결정해서 캐시
                    //
                    //    기준:
                    //      아직 top 비어있으면 -> 이번 이미지는 top으로 간주
                    //      top 이미 있고 front 비어있으면 -> 이번 이미지는 front로 간주
                    //      둘 다 이미 있으면 -> 새 캡처 사이클이라고 보고 top 갱신 / front 클리어 후 top부터 다시
                    // ---------------------------
                    string roleAssigned = "";
                    if (_pendingTopPath == null)
                    {
                        _pendingTopPath = fullPath;
                        roleAssigned = "TOP";
                        Console.WriteLine("[TcpInspectionServer] cached TOP = " + _pendingTopPath);
                    }
                    else if (_pendingFrontPath == null)
                    {
                        _pendingFrontPath = fullPath;
                        roleAssigned = "FRONT";
                        Console.WriteLine("[TcpInspectionServer] cached FRONT = " + _pendingFrontPath);
                    }
                    else
                    {
                        // 이전 세트가 아직 처리 안 된 상태에서 또 들어온 상황이면
                        // 그냥 새로운 사이클 시작한다고 보고 덮어쓴다.
                        _pendingTopPath = fullPath;
                        _pendingFrontPath = null;
                        roleAssigned = "TOP_RESET";
                        Console.WriteLine("[TcpInspectionServer] reset cycle, new TOP = " + _pendingTopPath);
                    }

                    // ---------------------------
                    // 5) 둘 다 준비됐으면 파이썬 dual 분석 호출
                    // ---------------------------
                    if (_pendingTopPath != null && _pendingFrontPath != null)
                    {
                        string finalResult = "에러";
                        string reason = "";

                        try
                        {
                            string json = await AnalyzeDualAsync(_pendingTopPath, _pendingFrontPath);

                            // json 예:
                            // {
                            //   "result":"정상",
                            //   "top_result":"정상",
                            //   "side_result":"정상",
                            //   "det_top":[["scratch",0.92]],
                            //   "det_side":[["dent",0.88]]
                            // }
                            ExtractDualResult(json, out finalResult, out reason);

                            // 파이썬 연결 OK로 간주
                            ServerMonitor.UpdatePythonStatus(true, "");
                        }
                        catch (Exception exAi)
                        {
                            Console.WriteLine("[TcpInspectionServer] python error: " + exAi.Message);
                            ServerMonitor.UpdatePythonStatus(false, exAi.Message);

                            finalResult = "에러";
                            reason = exAi.Message;
                        }

                        // ServerMonitor에 기록 -> Form2 그리드 1줄 추가됨
                        ServerMonitor.RecordInspection(
                            DateTime.Now,
                            finalResult,          // "정상"/"비정상"/"에러"
                            reason,               // 사유(결함명 등)
                            _pendingTopPath,      // top 이미지 경로
                            _pendingFrontPath     // front 이미지 경로
                        );

                        // 클라에 이 판정 전송
                        // (두 번째 이미지 전송 쪽 recv()에서 받을 수 있음)
                        byte[] respBytes = Encoding.UTF8.GetBytes(finalResult);
                        await ns.WriteAsync(respBytes, 0, respBytes.Length);

                        Console.WriteLine("[TcpInspectionServer] sent result=" + finalResult);

                        // 한 사이클 끝났으니까 비워줌
                        _pendingTopPath = null;
                        _pendingFrontPath = null;
                    }
                    else
                    {
                        // 아직 한 장만 받은 상태면 뭐라고 응답할지?
                        // 클라 지금은 recv() 안 하고 바로 닫으니까
                        // 안 보내도 되는데, 보내고 싶으면 간단히 역할 알려줘도 됨.
                        string ack = "RECV " + roleAssigned;
                        byte[] ackB = Encoding.UTF8.GetBytes(ack);
                        await ns.WriteAsync(ackB, 0, ackB.Length);

                        Console.WriteLine("[TcpInspectionServer] sent ack=" + ack);
                    }

                    // 이 연결은 여기서 끝. (클라가 어차피 소켓 닫음)
                }
                catch (Exception ex)
                {
                    Console.WriteLine("[TcpInspectionServer] HandleClient error = " + ex.Message);
                    ServerMonitor.UpdatePythonStatus(false, ex.Message);

                    try
                    {
                        if (ns != null && ns.CanWrite)
                        {
                            byte[] errB = Encoding.UTF8.GetBytes("에러");
                            await ns.WriteAsync(errB, 0, errB.Length);
                        }
                    }
                    catch { }
                }
                finally
                {
                    if (ns != null) { try { ns.Close(); } catch { } }
                }
            }
        }

        // 파이썬 dual inference
        private async Task<string> AnalyzeDualAsync(string topPath, string frontPath)
        {
            // 파이썬 서버 쪽 프로토콜 0x02:
            // [0x02]
            // [4바이트 TOP length (little endian)]
            // [TOP bytes]
            // [4바이트 SIDE length (little endian)]
            // [SIDE bytes]
            //
            // 응답: UTF-8 JSON 후 소켓 닫기

            Console.WriteLine("[TcpInspectionServer] calling python dual...");

            using (TcpClient pyCli = new TcpClient())
            {
                await pyCli.ConnectAsync(_pyHost, _pyPort);
                using (NetworkStream ns = pyCli.GetStream())
                {
                    byte[] topBytes = File.ReadAllBytes(topPath);
                    byte[] frontBytes = File.ReadAllBytes(frontPath);

                    // 0x02 모드 전송
                    await ns.WriteAsync(new byte[] { 0x02 }, 0, 1);

                    // top
                    byte[] lenTop = BitConverter.GetBytes(topBytes.Length);   // little endian ok
                    await ns.WriteAsync(lenTop, 0, 4);
                    await ns.WriteAsync(topBytes, 0, topBytes.Length);

                    // front(side)
                    byte[] lenFront = BitConverter.GetBytes(frontBytes.Length);
                    await ns.WriteAsync(lenFront, 0, 4);
                    await ns.WriteAsync(frontBytes, 0, frontBytes.Length);

                    // 파이썬은 JSON만 쏘고 닫는다.
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
                            if (n <= 0) break;
                            ms.Write(buf, 0, n);
                        }

                        string json = Encoding.UTF8.GetString(ms.ToArray());
                        Console.WriteLine("[TcpInspectionServer] python resp=" + json);
                        return json;
                    }
                }
            }
        }

        // 파이썬 dual JSON에서 최종 결과 / 사유 간단 추출
        private void ExtractDualResult(string json, out string finalResult, out string reasonOut)
        {
            finalResult = "에러";
            reasonOut = "";

            if (string.IsNullOrEmpty(json))
                return;

            // result
            // 예: "result":"정상"
            string key = "\"result\":\"";
            int idx = json.IndexOf(key, StringComparison.OrdinalIgnoreCase);
            if (idx >= 0)
            {
                int start = idx + key.Length;
                int end = json.IndexOf("\"", start, StringComparison.OrdinalIgnoreCase);
                if (end > start)
                {
                    finalResult = json.Substring(start, end - start);
                }
            }

            // 사유(불량명) 추출:
            // det_top": [["something",0.98], ...]
            // det_side": [["xxx",0.88], ...]
            // 첫 label을 reasonOut으로 쓴다.
            reasonOut = ExtractFirstLabel(json);
        }

        private string ExtractFirstLabel(string json)
        {
            // "det_top":[["label",score],...]
            // or "det_side":[["label",score],...]
            string[] keys = { "\"det_top\":[[\"", "\"det_side\":[[\"" };

            foreach (var k in keys)
            {
                int idx = json.IndexOf(k, StringComparison.OrdinalIgnoreCase);
                if (idx >= 0)
                {
                    int start = idx + k.Length;
                    int end = json.IndexOf("\"", start, StringComparison.OrdinalIgnoreCase);
                    if (end > start)
                    {
                        return json.Substring(start, end - start);
                    }
                }
            }
            return "";
        }

        // 정확히 size 바이트 읽기
        private static async Task<int> ReadExactAsync(NetworkStream ns, byte[] buf, int offset, int size)
        {
            int total = 0;
            while (total < size)
            {
                int n = await ns.ReadAsync(buf, offset + total, size - total);
                if (n <= 0) break;
                total += n;
            }
            return total;
        }
    }
}

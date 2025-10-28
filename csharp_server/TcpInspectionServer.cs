using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Text.Json; // System.Text.Json 사용 (.NET 6+)

namespace MFCServer1
{
    public class TcpInspectionServer
    {
        private readonly int _port;
        private readonly PythonTcpClient _python;
        private readonly DatabaseService _db;
        private TcpListener _listener;
        private bool _running;

        public TcpInspectionServer(
            int listenPort,
            PythonTcpClient pythonService,
            DatabaseService dbService)
        {
            _port = listenPort;
            _python = pythonService;
            _db = dbService;
        }

        public void StartSync()
        {
            _listener = new TcpListener(IPAddress.Any, _port);
            _listener.Start();
            _running = true;

            ServerMonitor.TcpListening = true;
            ServerMonitor.TcpPort = _port;

            Console.WriteLine("[TCP] Listening on " + _port);

            while (_running)
            {
                try
                {
                    TcpClient client = _listener.AcceptTcpClient();
                    ThreadPool.QueueUserWorkItem(HandleClient, client);
                }
                catch (Exception ex)
                {
                    Console.WriteLine("[TCP] Accept error: " + ex.Message);
                    Thread.Sleep(200);
                }
            }
        }

        private void HandleClient(object state)
        {
            TcpClient client = (TcpClient)state;
            NetworkStream ns = client.GetStream();

            string remoteIp = ((IPEndPoint)client.Client.RemoteEndPoint).Address.ToString();
            ServerMonitor.LastClient = remoteIp;
            ServerMonitor.LastClientTime = DateTime.Now;

            Console.WriteLine("[TCP] Client connected: " + remoteIp);

            try
            {
                // 메시지 예: "C:\top.jpg|C:\side.jpg"
                // or "C:\top.jpg|" (side없음)
                // or "|C:\side.jpg" (top없음)
                byte[] buffer = new byte[4096];
                int len = ns.Read(buffer, 0, buffer.Length);
                string msg = Encoding.UTF8.GetString(buffer, 0, len);
                Console.WriteLine("[TCP] RECV: " + msg);

                string[] parts = msg.Split('|');
                string topPath = "";
                string sidePath = "";
                if (parts.Length >= 1)
                    topPath = parts[0].Trim();
                if (parts.Length >= 2)
                    sidePath = parts[1].Trim();

                bool hasTop = !string.IsNullOrWhiteSpace(topPath);
                bool hasSide = !string.IsNullOrWhiteSpace(sidePath);

                // 파이썬 분석 요청
                string pyResultJson;
                Console.WriteLine("[TCP] Call Python AnalyzeDualAsync...");
                if (hasTop && hasSide)
                {
                    pyResultJson = _python.AnalyzeDualAsync(topPath, sidePath).Result;
                }
                else if (hasTop)
                {
                    Console.WriteLine("[TCP] Call Python AnalyzeSingleAsync (top)...");
                    pyResultJson = _python.AnalyzeSingleAsync(topPath, "top").Result;
                }
                else if (hasSide)
                {
                    Console.WriteLine("[TCP] Call Python AnalyzeSingleAsync (side)...");
                    pyResultJson = _python.AnalyzeSingleAsync(sidePath, "side").Result;
                }
                else
                {
                    pyResultJson = "{\"result\":\"비정상\",\"error\":\"no image\"}";
                }
                Console.WriteLine("[TCP] PY RESULT: " + pyResultJson);

                Console.WriteLine("[TCP] PY RESULT: " + pyResultJson);

                // 결과 추출
                string finalResult = ExtractResult(pyResultJson); // "정상"/"비정상"
                string reason = ExtractReason(pyResultJson);      // "dent(0.70), scratch(0.65)" 같은 문자열

                // ServerMonitor에 최신 상태 반영 (폼 상단 라벨/미리보기용)
                ServerMonitor.LastResult = finalResult;
                ServerMonitor.LastTopImagePath = topPath;
                ServerMonitor.LastSideImagePath = sidePath;

                // 로그 1줄 추가 (이게 DataGridView 한 줄로 감)
                ServerMonitor.AddLog(finalResult, reason, topPath, sidePath);

                // DB 저장 (image_paths는 그냥 두 경로 합친 문자열로 넣자)
                try
                {
                    _db.InsertInspection(finalResult, topPath + ";" + sidePath);
                }
                catch (Exception exDb)
                {
                    Console.WriteLine("[DB] insert fail: " + exDb.Message);
                }

                // 클라에 응답
                byte[] ok = Encoding.UTF8.GetBytes(finalResult);
                ns.Write(ok, 0, ok.Length);
            }
            catch (Exception ex)
            {
                Console.WriteLine("[TCP] HandleClient error: " + ex.ToString());
            }
            finally
            {
                ns.Close();
                client.Close();
            }
        }

        // "result": "정상"/"비정상"
        private string ExtractResult(string json)
        {
            try
            {
                using (JsonDocument doc = JsonDocument.Parse(json))
                {
                    if (doc.RootElement.TryGetProperty("result", out var r))
                    {
                        return r.GetString() ?? "비정상";
                    }
                }
            }
            catch { }

            return "비정상";
        }

        // 불합격 사유 문자열 만들기
        // 로직:
        //  - top_result / side_result 가 "비정상"인 쪽만 살핀다
        //  - det_top / det_side 안에 있는 (class, conf)들 중
        //    conf 높은 애들을 문자열로 합쳐서 보여준다
        private string ExtractReason(string json)
        {
            try
            {
                using (JsonDocument doc = JsonDocument.Parse(json))
                {
                    string topRes = doc.RootElement.TryGetProperty("top_result", out var tr) ? tr.GetString() : null;
                    string sideRes = doc.RootElement.TryGetProperty("side_result", out var sr) ? sr.GetString() : null;

                    string reasonTop = "";
                    string reasonSide = "";

                    if (topRes == "비정상" &&
                        doc.RootElement.TryGetProperty("det_top", out var detTopEl) &&
                        detTopEl.ValueKind == JsonValueKind.Array)
                    {
                        // det_top: [ ["no_cap",0.8], ["top_scratch",0.4], ...]
                        reasonTop = BuildReasonList(detTopEl);
                        if (!string.IsNullOrEmpty(reasonTop))
                            reasonTop = "[TOP] " + reasonTop;
                    }

                    if (sideRes == "비정상" &&
                        doc.RootElement.TryGetProperty("det_side", out var detSideEl) &&
                        detSideEl.ValueKind == JsonValueKind.Array)
                    {
                        reasonSide = BuildReasonList(detSideEl);
                        if (!string.IsNullOrEmpty(reasonSide))
                            reasonSide = "[SIDE] " + reasonSide;
                    }

                    // 둘 다 있는 경우는 , 로 연결
                    string combined = "";
                    if (!string.IsNullOrEmpty(reasonTop))
                        combined += reasonTop;
                    if (!string.IsNullOrEmpty(reasonSide))
                    {
                        if (combined != "")
                            combined += " ; ";
                        combined += reasonSide;
                    }

                    return combined;
                }
            }
            catch
            {
                // 파싱 실패하면 그냥 빈 사유
            }

            return "";
        }

        // det 배열에서 "class(conf)" 묶은 문자열 만들어주기
        private string BuildReasonList(JsonElement detArray)
        {
            // detArray 예: [ ["dent",0.7], ["scratch",0.2] ]
            // 여기서 conf가 0.5 이상 같은 임계 이상만 넣고 싶다면 여기서 필터링 가능
            // 지금은 다 넣자.
            var sb = new StringBuilder();
            bool first = true;

            foreach (var detItem in detArray.EnumerateArray())
            {
                // detItem 예: ["dent",0.7]
                if (detItem.ValueKind != JsonValueKind.Array) continue;
                if (detItem.GetArrayLength() < 2) continue;

                string clsName = detItem[0].GetString() ?? "";
                double conf = detItem[1].GetDouble();

                string piece = $"{clsName}({conf:0.00})";
                if (!first) sb.Append(", ");
                sb.Append(piece);
                first = false;
            }

            return sb.ToString();
        }
    }
}

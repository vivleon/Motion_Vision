using System;
using System.Collections.Generic;

namespace MFCServer1
{
    // 전역 상태 저장 및 최근 검사 로그 기록
    public static class ServerMonitor
    {
        private static readonly object _lock = new object();

        // TCP 서버 상태
        public static bool TcpListening { get; private set; } = false;
        public static int TcpPort { get; private set; } = 9001;

        // 파이썬 AI 상태
        public static bool PythonAlive { get; private set; } = false;
        public static string PythonLastError { get; private set; } = "None";

        // 최근 접속자
        public static string LastClient { get; private set; } = "-";
        public static DateTime LastClientTime { get; private set; } = DateTime.MinValue;

        // 최근 판정 / 이미지 경로
        public static string LastResult { get; private set; } = "-";
        public static string LastTopImagePath { get; private set; } = "";
        public static string LastSideImagePath { get; private set; } = "";

        // 그리드에 바인딩할 검사 로그 저장소
        private static readonly List<InspectionRecord> _history = new List<InspectionRecord>();
        private static long _nextId = 1;

        // 그리드 한 줄이 가질 데이터 구조
        public class InspectionRecord
        {
            public long Id { get; set; }          // 번호
            public DateTime Time { get; set; }    // 시간
            public string Result { get; set; }    // 결과 ("정상" / "비정상" / "에러")
            public string Reason { get; set; }    // 불합격 사유 (없으면 "")
            public string TopPath { get; set; }   // TOP 경로
            public string SidePath { get; set; }  // SIDE 경로
        }

        public static void UpdateServerStatus(bool listening, int port)
        {
            lock (_lock)
            {
                TcpListening = listening;
                TcpPort = port;
            }
        }

        public static void UpdatePythonStatus(bool alive, string err)
        {
            lock (_lock)
            {
                PythonAlive = alive;
                PythonLastError = string.IsNullOrEmpty(err) ? "None" : err;
            }
        }

        public static void UpdateClientInfo(string ip)
        {
            lock (_lock)
            {
                LastClient = string.IsNullOrEmpty(ip) ? "-" : ip;
                LastClientTime = DateTime.Now;
            }
        }

        public static void RecordInspection(
            DateTime time,
            string result,
            string reason,
            string topPath,
            string sidePath
        )
        {
            lock (_lock)
            {
                // 최신 상태(상단 라벨/미리보기용)
                LastResult = result ?? "-";
                LastTopImagePath = topPath ?? "";
                LastSideImagePath = sidePath ?? "";

                // 로그 1건 추가
                InspectionRecord rec = new InspectionRecord();
                rec.Id = _nextId++;
                rec.Time = time;
                rec.Result = result ?? "";
                rec.Reason = reason ?? "";
                rec.TopPath = topPath ?? "";
                rec.SidePath = sidePath ?? "";

                _history.Add(rec);

                // DB 저장은 여기서 바로 시도 (예외는 상태로만 남기고 죽지 않음)
                try
                {
                    DatabaseService.InsertInspection(
                        time,
                        rec.Result,
                        rec.TopPath,
                        rec.SidePath,
                        "" // line/설비 등 필요하면 여기에 채워
                    );
                }
                catch (Exception ex)
                {
                    PythonLastError = "[DB] " + ex.Message;
                }
            }
        }

        public static List<InspectionRecord> GetRecent()
        {
            lock (_lock)
            {
                // 바인딩에 쓸 복사본 반환
                return new List<InspectionRecord>(_history);
            }
        }
    }
}

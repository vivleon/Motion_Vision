using System;                       // DateTime
using MySql.Data.MySqlClient;       // MySQL Connector (NuGet: MySql.Data)

namespace MFCServer1
{
    // =========================================================
    // DatabaseService
    // ---------------------------------------------------------
    // DB에 검사 결과를 저장하는 헬퍼 클래스.
    // 간단하게 InsertInspection()만 노출한다.
    //
    // 실제 테이블 구조는 너 프로젝트에 맞춰야 하는데,
    // 여기서는 예시로 inspections 테이블이 있다고 가정:
    //
    // CREATE TABLE inspections (
    //   id INT AUTO_INCREMENT PRIMARY KEY,
    //   time DATETIME,
    //   result VARCHAR(20),
    //   top_path TEXT,
    //   side_path TEXT,
    //   line_name VARCHAR(50)
    // );
    //
    // (네가 이미 쓰고 있는 테이블명이 다를 수도 있으니까 필요하면 이름 바꿔.)
    // =========================================================
    public static class DatabaseService
    {
        // DB 접속 문자열 (너 환경에 맞게 수정)
        // Server=127.0.0.1;Database=???;Uid=???;Pwd=???;Charset=utf8;
        private static readonly string _connStr =
            "Server=127.0.0.1;Database=racingdb;Uid=root;Pwd=1234;Charset=utf8;";

        // =====================================================
        // InsertInspection
        // -----------------------------------------------------
        // 검사 결과 1건을 DB에 INSERT.
        //
        // time      : 검사 시각
        // result    : "정상"/"비정상"/"에러"
        // topPath   : 상부 이미지 경로
        // sidePath  : 측면 이미지 경로(없으면 "-")
        // line      : 라인명 ("라인1" 등)
        //
        // 실패하더라도 예외 던지게 놔둔다.
        // (ServerMonitor에서 try/catch로 감싼 상태로 호출됨)
        // =====================================================
        public static void InsertInspection(
            DateTime time,
            string result,
            string topPath,
            string sidePath,
            string line
        )
        {
            using (var conn = new MySqlConnection(_connStr))
            {
                conn.Open();

                string sql = @"
INSERT INTO inspections
(time, result, top_path, side_path, line_name)
VALUES
(@time, @result, @top, @side, @line);
";

                using (var cmd = new MySqlCommand(sql, conn))
                {
                    // 시간
                    cmd.Parameters.AddWithValue("@time", time);

                    // 판정 결과
                    cmd.Parameters.AddWithValue("@result", result ?? "");

                    // 상부 이미지 경로
                    cmd.Parameters.AddWithValue("@top", topPath ?? "");

                    // 측면 이미지 경로 (없으면 "-")
                    cmd.Parameters.AddWithValue("@side", sidePath ?? "");

                    // 라인명
                    cmd.Parameters.AddWithValue("@line", line ?? "");

                    cmd.ExecuteNonQuery();
                }
            }
        }
    }
}

using System;
using System.Linq;
using System.Windows.Forms;
using System.Drawing;
using System.IO;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace MFCServer1
{
    public partial class Form2 : Form
    {
        private System.Windows.Forms.Timer _timer;   // UI 갱신 타이머

        public Form2()
        {
            InitializeComponent();

            // ===== DataGridView 스타일/컬럼 설정 =====
            // (너 기존 코드 그대로 유지)
            dataGridView1.AutoGenerateColumns = false;
            dataGridView1.Columns.Clear();

            dataGridView1.AllowUserToAddRows = false;
            dataGridView1.AllowUserToDeleteRows = false;
            dataGridView1.AllowUserToResizeRows = false;
            dataGridView1.MultiSelect = false;
            dataGridView1.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
            dataGridView1.ReadOnly = true;
            dataGridView1.RowHeadersVisible = false;

            dataGridView1.RowTemplate.Height = 28;
            dataGridView1.DefaultCellStyle.Font = new Font("맑은 고딕", 9.5f, FontStyle.Regular);

            dataGridView1.EnableHeadersVisualStyles = false;
            dataGridView1.ColumnHeadersDefaultCellStyle.BackColor = Color.Gainsboro;
            dataGridView1.ColumnHeadersDefaultCellStyle.ForeColor = Color.Black;
            dataGridView1.ColumnHeadersDefaultCellStyle.Font =
                new Font("맑은 고딕", 10F, FontStyle.Bold);
            dataGridView1.ColumnHeadersDefaultCellStyle.Alignment =
                DataGridViewContentAlignment.MiddleCenter;
            dataGridView1.ColumnHeadersHeight = 30;

            dataGridView1.DefaultCellStyle.Alignment =
                DataGridViewContentAlignment.MiddleLeft;
            dataGridView1.DefaultCellStyle.Padding = new Padding(4, 2, 4, 2);

            dataGridView1.DefaultCellStyle.SelectionBackColor = Color.SteelBlue;
            dataGridView1.DefaultCellStyle.SelectionForeColor = Color.White;

            // ===== 컬럼들 =====
            var colId = new DataGridViewTextBoxColumn
            {
                HeaderText = "번호",
                DataPropertyName = "Id",
                Width = 50,
                ReadOnly = true,
                DefaultCellStyle = new DataGridViewCellStyle
                {
                    Alignment = DataGridViewContentAlignment.MiddleCenter,
                    Font = new Font("맑은 고딕", 9.5f, FontStyle.Regular)
                }
            };

            var colTime = new DataGridViewTextBoxColumn
            {
                HeaderText = "시간",
                DataPropertyName = "Time",
                Width = 150,
                MinimumWidth = 150,
                ReadOnly = true,
                DefaultCellStyle = new DataGridViewCellStyle
                {
                    Alignment = DataGridViewContentAlignment.MiddleCenter,
                    Format = "yyyy-MM-dd HH:mm:ss",
                    Font = new Font("맑은 고딕", 9.5f, FontStyle.Regular)
                }
            };

            var colResult = new DataGridViewTextBoxColumn
            {
                HeaderText = "결과",
                DataPropertyName = "Result",
                Width = 70,
                MinimumWidth = 70,
                ReadOnly = true,
                DefaultCellStyle = new DataGridViewCellStyle
                {
                    Alignment = DataGridViewContentAlignment.MiddleCenter,
                    Font = new Font("맑은 고딕", 10f, FontStyle.Bold),
                    ForeColor = Color.Black
                }
            };

            var colReason = new DataGridViewTextBoxColumn
            {
                HeaderText = "불합격 사유",
                DataPropertyName = "Reason",
                Width = 250,
                MinimumWidth = 200,
                ReadOnly = true,
                DefaultCellStyle = new DataGridViewCellStyle
                {
                    Alignment = DataGridViewContentAlignment.MiddleLeft,
                    Font = new Font("맑은 고딕", 9f, FontStyle.Regular)
                }
            };

            var colTopPath = new DataGridViewTextBoxColumn
            {
                HeaderText = "TOP 경로",
                DataPropertyName = "TopPath",
                Width = 250,
                MinimumWidth = 200,
                ReadOnly = true,
                DefaultCellStyle = new DataGridViewCellStyle
                {
                    Alignment = DataGridViewContentAlignment.MiddleLeft,
                    Font = new Font("맑은 고딕", 9f, FontStyle.Regular)
                }
            };

            var colSidePath = new DataGridViewTextBoxColumn
            {
                HeaderText = "SIDE 경로",
                DataPropertyName = "SidePath",
                AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill,
                ReadOnly = true,
                DefaultCellStyle = new DataGridViewCellStyle
                {
                    Alignment = DataGridViewContentAlignment.MiddleLeft,
                    Font = new Font("맑은 고딕", 9f, FontStyle.Regular)
                }
            };

            dataGridView1.Columns.Add(colId);
            dataGridView1.Columns.Add(colTime);
            dataGridView1.Columns.Add(colResult);
            dataGridView1.Columns.Add(colReason);
            dataGridView1.Columns.Add(colTopPath);
            dataGridView1.Columns.Add(colSidePath);

            dataGridView1.CellFormatting += (s, e) =>
            {
                if (dataGridView1.Columns[e.ColumnIndex].DataPropertyName == "Result" &&
                    e.Value != null)
                {
                    string val = e.Value.ToString();
                    if (val.Contains("비정상"))
                        e.CellStyle.ForeColor = Color.Red;
                    else if (val.Contains("정상"))
                        e.CellStyle.ForeColor = Color.Green;
                }
            };

            // ===== UI 갱신 타이머 =====
            _timer = new System.Windows.Forms.Timer();
            _timer.Interval = 1000;
            _timer.Tick += Timer_Tick;
            _timer.Start();

            // ===== 파이썬 헬스체크 "딱 한 번만" =====
            _ = KickPythonHealthOnce();  // fire & forget OK
        }

        // 폼 뜰 때 한 번만 파이썬 서버(127.0.0.1:8009) ping
        private async Task KickPythonHealthOnce()
        {
            try
            {
                var py = new PythonTcpClient("127.0.0.1", 8009);
                bool ok = await py.HealthCheckAsync(); // 0x01 -> "OK"
                if (ok)
                {
                    ServerMonitor.UpdatePythonStatus(true, "");
                }
                else
                {
                    ServerMonitor.UpdatePythonStatus(false, "No OK");
                }
            }
            catch (Exception ex)
            {
                ServerMonitor.UpdatePythonStatus(false, ex.Message);
            }
        }

        // 1초마다 서버 상태 + 로그 갱신 + 이미지 갱신
        private void Timer_Tick(object sender, EventArgs e)
        {
            // TCP 상태
            if (ServerMonitor.TcpListening)
            {
                lblTcpStatus.Text = "TCP LISTENING : Port " + ServerMonitor.TcpPort;
                lblTcpStatus.ForeColor = Color.LimeGreen;
            }
            else
            {
                lblTcpStatus.Text = "TCP STOPPED";
                lblTcpStatus.ForeColor = Color.Red;
            }

            // PYTHON 상태 (마지막 체크 결과 유지)
            if (ServerMonitor.PythonAlive)
            {
                lblPythonStatus.Text = "PYTHON OK";
                lblPythonStatus.ForeColor = Color.LimeGreen;
            }
            else
            {
                lblPythonStatus.Text = "PYTHON DOWN";
                lblPythonStatus.ForeColor = Color.Red;
            }

            // 기타 라벨
            lblPythonError.Text = "LastError: " + ServerMonitor.PythonLastError;

            lblLastClient.Text =
                "LastClient: " + ServerMonitor.LastClient +
                " at " +
                (ServerMonitor.LastClientTime == DateTime.MinValue
                    ? "-"
                    : ServerMonitor.LastClientTime.ToString("HH:mm:ss"));

            lblLastResult.Text = "LastResult: " + ServerMonitor.LastResult;

            // 로그 최신순 바인딩
            List<ServerMonitor.InspectionRecord> latestLogs = ServerMonitor
                .GetRecent()
                .OrderByDescending(x => x.Time)
                .ToList();

            dataGridView1.DataSource = latestLogs;
            dataGridView1.Refresh();

            // 마지막 판정 이미지 미리보기
            ShowImages(ServerMonitor.LastTopImagePath, ServerMonitor.LastSideImagePath);
        }

        private void dataGridView1_CellClick(object sender, DataGridViewCellEventArgs e)
        {
            if (e.RowIndex < 0)
                return;

            var rowObj = dataGridView1.Rows[e.RowIndex].DataBoundItem
                         as ServerMonitor.InspectionRecord;
            if (rowObj == null)
                return;

            ShowImages(rowObj.TopPath, rowObj.SidePath);
        }

        private void ShowImages(string topPath, string sidePath)
        {
            if (!string.IsNullOrEmpty(topPath) && File.Exists(topPath))
            {
                try { picTop.Image = LoadImageNoLock(topPath); } catch { }
            }

            if (!string.IsNullOrEmpty(sidePath) && File.Exists(sidePath))
            {
                try { picSide.Image = LoadImageNoLock(sidePath); } catch { }
            }
        }

        private Image LoadImageNoLock(string path)
        {
            using (var fs = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.ReadWrite))
            {
                var ms = new MemoryStream();
                fs.CopyTo(ms);
                ms.Position = 0;
                return Image.FromStream(ms);
            }
        }
    }
}

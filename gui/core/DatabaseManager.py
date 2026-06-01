import sqlite3
import json
import os

class DatabaseManager:
    def __init__(self, db_path):
        self.db_path = db_path
        self._init_db()

    def _init_db(self):
        with sqlite3.connect(self.db_path) as conn:
            c = conn.cursor()
            c.execute('''CREATE TABLE IF NOT EXISTS run_history (
                            id INTEGER PRIMARY KEY AUTOINCREMENT,
                            run_id TEXT UNIQUE,
                            output_file TEXT,
                            start_time TEXT,
                            end_time TEXT,
                            operator TEXT,
                            applied_hv TEXT,
                            temperature TEXT,
                            config_json TEXT,
                            total_events INTEGER,
                            avg_rate_hz REAL,
                            total_size_mb REAL,
                            status TEXT
                        )''')
            
            # 기존 DB 구조를 파괴하지 않고 하위 호환성을 유지하기 위한 마이그레이션
            try:
                c.execute("ALTER TABLE run_history ADD COLUMN live_time_sec REAL")
                c.execute("ALTER TABLE run_history ADD COLUMN dead_time_pct REAL")
            except sqlite3.OperationalError:
                pass 
            conn.commit()

    def record_run_start(self, output_file, env_data, config_file):
        with sqlite3.connect(self.db_path) as conn:
            c = conn.cursor()
            c.execute("SELECT COUNT(*) FROM run_history")
            run_index = c.fetchone()[0] + 1
            run_id_str = f"RUN_{run_index:06d}"
            
            config_str = "{}"
            if os.path.exists(config_file):
                with open(config_file, 'r') as f:
                    config_str = f.read()

            c.execute('''INSERT INTO run_history 
                         (run_id, output_file, start_time, operator, applied_hv, temperature, config_json, status)
                         VALUES (?, ?, datetime('now', 'localtime'), ?, ?, ?, ?, ?)''',
                      (run_id_str, output_file, env_data.get('Operator', ''),
                       env_data.get('Applied HV', ''), env_data.get('Temperature (C)', ''),
                       config_str, "RUNNING"))
            conn.commit()
            return c.lastrowid

    def update_run_status(self, run_id, status):
        with sqlite3.connect(self.db_path) as conn:
            c = conn.cursor()
            c.execute("UPDATE run_history SET status = ? WHERE id = ?", (status, run_id))
            conn.commit()

    def update_daq_summary(self, run_id, stats):
        with sqlite3.connect(self.db_path) as conn:
            c = conn.cursor()
            
            events = int(stats.get('events', 0))
            rate_str = str(stats.get('rate', '0.0')).replace('Hz', '').strip()
            speed_str = str(stats.get('speed', '0.00')).replace('MB/s', '').strip()
            
            rate = float(rate_str) if rate_str else 0.0
            speed = float(speed_str) if speed_str else 0.0
            
            # C++에서 전송된 TTT 기반 절대 선속 데이터를 DB로 직접 밀어넣음
            live_time = float(stats.get('live_time', 0.0))
            dead_time_pct = float(stats.get('dead_time_pct', 0.0))

            c.execute('''UPDATE run_history 
                         SET end_time = datetime('now', 'localtime'),
                             total_events = ?,
                             avg_rate_hz = ?,
                             total_size_mb = ?,
                             status = ?,
                             live_time_sec = ?,
                             dead_time_pct = ?
                         WHERE id = ?''', 
                      (events, rate, speed, "COMPLETED", live_time, dead_time_pct, run_id))
            conn.commit()

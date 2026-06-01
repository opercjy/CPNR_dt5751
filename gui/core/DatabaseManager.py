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
            
            # 🌟 [패치] 구형 DB 스키마(run_id 없음) 감지 시 백업 테이블로 밀어내고 새로 만듦
            c.execute("PRAGMA table_info(run_history)")
            columns = [info[1] for info in c.fetchall()]
            if columns and "run_id" not in columns:
                c.execute("ALTER TABLE run_history RENAME TO run_history_legacy_backup")
                conn.commit()

            # 최신 프로덕션 등급 스키마 창설
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
                            status TEXT,
                            live_time_sec REAL,
                            dead_time_pct REAL
                        )''')
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

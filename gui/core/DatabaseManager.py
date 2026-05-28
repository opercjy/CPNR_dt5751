import sqlite3
import os
import datetime
import json

class DatabaseManager:
    def __init__(self, db_path):
        self.db_path = db_path
        self.init_db()

    def init_db(self):
        os.makedirs(os.path.dirname(self.db_path), exist_ok=True)
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS run_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                start_time TEXT,
                output_file TEXT,
                env_metadata TEXT,
                config_dump TEXT
            )
        ''')
        
        # 하위 호환성을 위한 동적 스키마 마이그레이션
        cursor.execute("PRAGMA table_info(run_history)")
        columns = [info[1] for info in cursor.fetchall()]
        if 'daq_summary' not in columns:
            cursor.execute("ALTER TABLE run_history ADD COLUMN daq_summary TEXT")
        if 'production_summary' not in columns:
            cursor.execute("ALTER TABLE run_history ADD COLUMN production_summary TEXT")
            
        conn.commit()
        conn.close()

    def record_run_start(self, output_file, env_dict, config_path):
        config_dump = ""
        if os.path.exists(config_path):
            with open(config_path, 'r') as f: config_dump = f.read()
        env_json = json.dumps(env_dict, ensure_ascii=False)
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        start_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        cursor.execute('''
            INSERT INTO run_history (start_time, output_file, env_metadata, config_dump)
            VALUES (?, ?, ?, ?)
        ''', (start_time, output_file, env_json, config_dump))
        run_id = cursor.lastrowid
        conn.commit()
        conn.close()
        return run_id

    def update_daq_summary(self, run_id, summary_dict):
        summary_json = json.dumps(summary_dict, ensure_ascii=False)
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        cursor.execute("UPDATE run_history SET daq_summary = ? WHERE id = ?", (summary_json, run_id))
        conn.commit()
        conn.close()

    def update_production_summary(self, raw_file_path, summary_dict):
        # Production은 별도로 실행되므로 원본 .dat 파일명으로 해당 런을 추적하여 업데이트
        summary_json = json.dumps(summary_dict, ensure_ascii=False)
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        cursor.execute("UPDATE run_history SET production_summary = ? WHERE output_file = ?", (summary_json, raw_file_path))
        conn.commit()
        conn.close()
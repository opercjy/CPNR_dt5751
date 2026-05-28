import os
import sqlite3
import json
from PyQt5.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, 
                             QPushButton, QTableWidget, QTableWidgetItem, 
                             QHeaderView, QMessageBox)

class DatabaseTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.bin_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
        self.db_path = os.path.join(self.bin_dir, "data", "run_history.db")
        self.setup_ui()

    def setup_ui(self):
        layout = QVBoxLayout(self)

        btn_layout = QHBoxLayout()
        self.btn_refresh = QPushButton("Refresh Database")
        self.btn_refresh.setStyleSheet("font-weight: bold; padding: 8px;")
        self.btn_refresh.clicked.connect(self.load_data)
        btn_layout.addWidget(self.btn_refresh)
        btn_layout.addStretch()
        layout.addLayout(btn_layout)

        self.table = QTableWidget(0, 5)
        # 🌟 수행자(Operator) 컬럼 전면 배치
        self.table.setHorizontalHeaderLabels(["Run ID", "Start Time", "Operator", "Output File", "Merged Env & Summary"])
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.Stretch)
        self.table.horizontalHeader().setSectionResizeMode(4, QHeaderView.Stretch)
        layout.addWidget(self.table)

    def load_data(self):
        if not os.path.exists(self.db_path):
            return

        try:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.cursor()
            # daq_summary와 production_summary도 함께 불러옵니다.
            cursor.execute("SELECT id, start_time, output_file, env_metadata, daq_summary, production_summary FROM run_history ORDER BY id DESC")
            rows = cursor.fetchall()
            conn.close()

            self.table.setRowCount(0)
            for row_idx, row_data in enumerate(rows):
                self.table.insertRow(row_idx)
                
                self.table.setItem(row_idx, 0, QTableWidgetItem(str(row_data[0])))
                self.table.setItem(row_idx, 1, QTableWidgetItem(str(row_data[1])))
                
                # 🌟 JSON 데이터 병합 및 Operator 파싱
                env_raw = row_data[3]
                daq_raw = row_data[4]
                prod_raw = row_data[5]
                
                operator_name = "Unknown"
                merged_info = []

                if env_raw:
                    try:
                        env_dict = json.loads(env_raw)
                        # JSON에서 Operator를 꺼내고 (pop), 나머지 딕셔너리를 문자열로 변환
                        operator_name = env_dict.pop("Operator", "Unknown")
                        if env_dict:
                            merged_info.append("[ENV] " + ", ".join([f"{k}: {v}" for k, v in env_dict.items()]))
                    except:
                        pass
                
                if daq_raw:
                    try:
                        daq_dict = json.loads(daq_raw)
                        merged_info.append(f"[DAQ] Evts: {daq_dict.get('events', '0')}, Spd: {daq_dict.get('avg_speed', '0')}MB/s")
                    except:
                        pass

                if prod_raw:
                    try:
                        prod_dict = json.loads(prod_raw)
                        merged_info.append(f"[ROOT] Evts: {prod_dict.get('events', '0')}, Spd: {prod_dict.get('avg_speed', '0')}MB/s")
                    except:
                        pass

                self.table.setItem(row_idx, 2, QTableWidgetItem(operator_name))
                self.table.setItem(row_idx, 3, QTableWidgetItem(str(row_data[2])))
                self.table.setItem(row_idx, 4, QTableWidgetItem(" | ".join(merged_info)))

        except Exception as e:
            QMessageBox.critical(self, "DB Error", f"Failed to load database:\n{e}")

    def showEvent(self, event):
        self.load_data()
        super().showEvent(event)
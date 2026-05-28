import subprocess
import re
from PyQt5.QtCore import QThread, pyqtSignal

class ProcessManager(QThread):
    log_signal = pyqtSignal(str)
    stat_signal = pyqtSignal(dict) 
    finished_signal = pyqtSignal(int)

    def __init__(self, cmd, cwd=None):
        super().__init__()
        self.cmd = cmd
        self.cwd = cwd
        self.process = None
        self.is_running = False
        
        # 터미널 색상 및 \r, \033[K 등의 제어 문자 완벽 제거
        self.ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])|\r')

    def run(self):
        self.is_running = True
        try:
            self.process = subprocess.Popen(
                self.cmd,
                cwd=self.cwd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                universal_newlines=True
            )
            
            for line in iter(self.process.stdout.readline, ''):
                if not self.is_running: break
                if line:
                    clean_line = self.ansi_escape.sub('', line).strip()
                    if not clean_line: continue
                    
                    # 🌟 DT5751 압축 로그 포맷 감지
                    if "[DAQ]" in clean_line:
                        self._parse_and_emit_stats(clean_line)
                    else:
                        self.log_signal.emit(clean_line)
            
            self.process.wait()
            self.finished_signal.emit(self.process.returncode)
        except Exception as e:
            self.log_signal.emit(f"[Error] Process execution failed: {e}")
            self.finished_signal.emit(-1)
        finally:
            self.is_running = False

    def _parse_and_emit_stats(self, line):
        # Format: [DAQ] 00:01 | Evt: 214 | 213.6 Hz | 1.68 MB/s | Drop: 0
        try:
            stats = {}
            parts = [p.strip() for p in line.split("|")]
            if len(parts) >= 5:
                stats['time'] = parts[0].split("]")[1].strip()
                stats['events'] = parts[1].split(":")[1].strip()
                stats['rate'] = parts[2]
                stats['speed'] = parts[3]
                stats['drops'] = parts[4].split(":")[1].strip()
            self.stat_signal.emit(stats)
        except Exception:
            pass

    def stop(self):
        self.is_running = False
        if self.process and self.process.poll() is None:
            self.log_signal.emit("[System] Sending SIGINT to gracefully stop the process...")
            self.process.send_signal(2)
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
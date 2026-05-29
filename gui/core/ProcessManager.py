import re
from PyQt5.QtCore import QObject, pyqtSignal, QProcess

class ProcessManager(QObject):
    log_signal = pyqtSignal(str)
    stat_signal = pyqtSignal(dict) 
    finished_signal = pyqtSignal(int)

    def __init__(self, cmd, cwd=None, parent=None):
        super().__init__(parent)
        self.cmd = cmd
        self.process = QProcess()
        if cwd:
            self.process.setWorkingDirectory(cwd)
            
        # 🌟 비동기 시그널-슬롯 연결 (GUI 프리징 100% 차단)
        self.process.readyReadStandardOutput.connect(self.handle_stdout)
        self.process.readyReadStandardError.connect(self.handle_stderr)
        self.process.finished.connect(self.handle_finished)
        
        self.ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])|\r')

    def start(self):
        self.process.start(self.cmd[0], self.cmd[1:])

    def handle_stdout(self):
        self.process.setReadChannel(QProcess.StandardOutput)
        while self.process.canReadLine():
            line_bytes = self.process.readLine()
            line = line_bytes.data().decode('utf-8', errors='ignore').strip()
            if not line: continue
            
            clean_line = self.ansi_escape.sub('', line).strip()
            if not clean_line: continue
            
            if "[DAQ]" in clean_line:
                self._parse_and_emit_stats(clean_line)
            else:
                self.log_signal.emit(clean_line)

    def handle_stderr(self):
        self.process.setReadChannel(QProcess.StandardError)
        while self.process.canReadLine():
            line_bytes = self.process.readLine()
            line = line_bytes.data().decode('utf-8', errors='ignore').strip()
            if line:
                self.log_signal.emit(f"<span style='color:red;'>[Error] {line}</span>")

    def handle_finished(self, exitCode, exitStatus):
        self.finished_signal.emit(exitCode)

    def _parse_and_emit_stats(self, line):
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
        if self.process.state() == QProcess.Running:
            self.log_signal.emit("[System] Sending SIGINT to gracefully stop the process...")
            self.process.terminate() # SIGTERM (graceful)
            if not self.process.waitForFinished(3000):
                self.process.kill()  # SIGKILL (force)
    
    def isRunning(self):
        return self.process.state() == QProcess.Running

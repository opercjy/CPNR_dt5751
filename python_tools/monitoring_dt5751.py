#!/usr/bin/env python3
import sys
import zmq
import struct
import numpy as np
import pyqtgraph as pg
from collections import deque
from pyqtgraph.Qt import QtWidgets, QtCore

# DT5751 EventHeader: ExtTTT(Q), EvtID(I), RecLen(I), Mask(H), Pattern(H), SampleRate_ps(H), Reserved(H)
HEADER_FORMAT = "=QIIHHHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

class LiveMonitorCLI(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("HEP DAQ: Standalone Live Monitor (DT5751 10-bit)")
        self.resize(1000, 800)

        self.current_mask = -1
        self.curves_wave = {}
        self.curves_qlong = {}
        self.q_long_hists = {}
        
        self.colors = [
            '#0d6efd', '#198754', '#dc3545', '#fd7e14'
        ]

        self.ctx = zmq.Context()
        self.sock = self.ctx.socket(zmq.SUB)
        self.sock.setsockopt(zmq.RCVHWM, 2000)
        self.sock.connect("tcp://127.0.0.1:5555")
        self.sock.setsockopt_string(zmq.SUBSCRIBE, "")

        self.setup_ui()

        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.poll_zmq)
        self.timer.start(33)

    def setup_ui(self):
        central_widget = QtWidgets.QWidget()
        self.setCentralWidget(central_widget)
        layout = QtWidgets.QVBoxLayout(central_widget)
        
        ctrl_layout = QtWidgets.QHBoxLayout()
        self.btn_clear = QtWidgets.QPushButton("Clear All Histograms")
        self.btn_clear.setStyleSheet("font-weight: bold; padding: 5px 15px;")
        self.btn_clear.clicked.connect(self.clear_data)
        ctrl_layout.addWidget(self.btn_clear)
        ctrl_layout.addStretch()
        layout.addLayout(ctrl_layout)

        pg.setConfigOptions(antialias=True, background='#f8f9fa', foreground='#212529')
        self.glw = pg.GraphicsLayoutWidget()
        layout.addWidget(self.glw)

        self.plot_wave = self.glw.addPlot(title="Live Waveform (Auto Overlay)")
        self.plot_wave.setLabel('bottom', "Time (ns)")
        self.plot_wave.setLabel('left', "ADC Value (10-bit)")
        self.plot_wave.setYRange(0, 1023, padding=0.05)
        self.plot_wave.addLegend(offset=(10, 10))
        self.glw.nextRow()

        self.plot_qlong = self.glw.addPlot(title="Real-time Python Computed Charge Spectrum")
        self.plot_qlong.setLogMode(y=True)
        self.plot_qlong.setLabel('bottom', "Integrated Charge (ADC Bins)")
        self.plot_qlong.setLabel('left', "Counts (Log)")
        self.plot_qlong.addLegend(offset=(10, 10))

    def rebuild_plots(self, mask):
        self.plot_wave.clear()
        self.plot_qlong.clear()
        if self.plot_wave.legend: self.plot_wave.legend.clear()
        if self.plot_qlong.legend: self.plot_qlong.legend.clear()
        
        self.curves_wave.clear()
        self.curves_qlong.clear()
        self.q_long_hists.clear()
        
        active_channels = [i for i in range(4) if (mask >> i) & 1]
        
        for ch in active_channels:
            color = self.colors[ch % len(self.colors)]
            pen = pg.mkPen(color, width=1.5)
            self.curves_wave[ch] = self.plot_wave.plot(name=f"CH {ch}", pen=pen)
            
            brush = pg.mkColor(color)
            brush.setAlpha(100)
            self.curves_qlong[ch] = self.plot_qlong.plot(name=f"CH {ch}", stepMode="center", fillLevel=0, brush=brush, pen=color)
            self.q_long_hists[ch] = deque(maxlen=10000)

    def clear_data(self):
        for ch in self.q_long_hists:
            self.q_long_hists[ch].clear()
            self.curves_wave[ch].setData(np.array([], dtype=np.double), np.array([], dtype=np.double))
            self.curves_qlong[ch].setData(np.array([0, 1]), np.array([0.1]))

    def poll_zmq(self):
        latest_msg = None
        while True:
            try:
                msg = self.sock.recv(flags=zmq.NOBLOCK)
                latest_msg = msg
                
                header = struct.unpack(HEADER_FORMAT, msg[:HEADER_SIZE])
                record_len = int(header[2])
                mask = int(header[3])
                
                if mask != self.current_mask:
                    self.current_mask = mask
                    self.rebuild_plots(mask)
                    
                active_channels = [i for i in range(4) if (mask >> i) & 1]
                
                for idx, ch in enumerate(active_channels):
                    offset = HEADER_SIZE + (idx * record_len * 2)
                    wave_bytes = msg[offset : offset + (record_len * 2)]
                    
                    if wave_bytes:
                        wave_arr = np.frombuffer(wave_bytes, dtype=np.uint16)
                        if len(wave_arr) > 200:
                            baseline_end = min(150, record_len // 4)
                            baseline = np.mean(wave_arr[:baseline_end])
                            pulse_area = np.sum(baseline - wave_arr[baseline_end:]) 
                            if pulse_area > 0:
                                self.q_long_hists[ch].append(pulse_area)
            except zmq.Again:
                break

        if latest_msg:
            header = struct.unpack(HEADER_FORMAT, latest_msg[:HEADER_SIZE])
            record_len = int(header[2])
            mask = int(header[3])
            sr_ps = int(header[5])
            dt_ns = sr_ps / 1000.0

            active_channels = [i for i in range(4) if (mask >> i) & 1]
            time_axis = np.arange(record_len) * dt_ns
            
            for idx, ch in enumerate(active_channels):
                if ch in self.curves_wave:
                    offset = HEADER_SIZE + (idx * record_len * 2)
                    wave_bytes = latest_msg[offset : offset + (record_len * 2)]
                    if wave_bytes:
                        wave_arr = np.frombuffer(wave_bytes, dtype=np.uint16)
                        self.curves_wave[ch].setData(time_axis, wave_arr)
            
            for ch in self.curves_qlong:
                if len(self.q_long_hists[ch]) > 2:
                    y, x = np.histogram(np.array(self.q_long_hists[ch]), bins=100)
                    y = np.where(y == 0, 0.1, y)
                    self.curves_qlong[ch].setData(x, y)

    def closeEvent(self, event):
        if self.timer.isActive():
            self.timer.stop()
        self.sock.close()
        self.ctx.term()
        event.accept()

if __name__ == "__main__":
    app = QtWidgets.QApplication(sys.argv)
    viewer = LiveMonitorCLI()
    viewer.show()
    sys.exit(app.exec())
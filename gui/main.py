#!/usr/bin/env python3
import sys
import os
from PyQt5.QtWidgets import QApplication

# 하위 모듈 인식을 위해 현재 경로를 sys.path의 최우선(0번)으로 강제 주입
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from windows.MainWindow import MainWindow

def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion") 

    window = MainWindow()
    window.show()

    sys.exit(app.exec()) 

if __name__ == "__main__":
    main()
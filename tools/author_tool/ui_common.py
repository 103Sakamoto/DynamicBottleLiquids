"""Shared Qt setup and standard form controls."""
import sys
from pathlib import Path
import importlib.util
if sys.platform == 'win32' and importlib.util.find_spec('pywin32_bootstrap'):
    import pywin32_bootstrap
from PySide6.QtCore import Qt
from PySide6.QtGui import QFont, QColor, QPalette, QIcon
from PySide6.QtWidgets import QApplication, QFrame, QVBoxLayout, QDoubleSpinBox, QToolButton, QDialog, QHBoxLayout, QWidget, QLabel, QPushButton
from qfluentwidgets import BodyLabel, setTheme, Theme, setThemeColor

VERSION='0.9'

def application():
    existing=QApplication.instance()
    if existing:return existing
    if sys.platform == 'win32':
        import ctypes
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID('DynamicBottleLiquids.Patcher')
    app=QApplication(sys.argv)
    app.setApplicationName('DBL patcher')
    app.setWindowIcon(QIcon(str(Path(__file__).with_name('app-icon.ico'))))
    app.setStyle('Fusion');palette=QPalette()
    for role,color in [('Window','#f3f5f7'),('WindowText','#202124'),('Base','#ffffff'),
                       ('AlternateBase','#f3f5f7'),('Text','#202124'),('Button','#ffffff'),
                       ('ButtonText','#202124'),('Highlight','#0067c0'),('HighlightedText','#ffffff')]:
        palette.setColor(getattr(QPalette.ColorRole,role),QColor(color))
    app.setPalette(palette);app.setFont(QFont('Segoe UI',10))
    setTheme(Theme.LIGHT);setThemeColor('#0067C0');return app

def card():
    frame=QFrame();frame.setObjectName('card');layout=QVBoxLayout(frame)
    layout.setContentsMargins(16,16,16,16);layout.setSpacing(12);return frame,layout

def note(text):
    label=BodyLabel(text);label.setWordWrap(True);return label

def help_tip(widget,text):
    widget.setAccessibleDescription(text);widget.setProperty('helpText',text)
    return widget

class HelpButton(QToolButton):
    def __init__(self,text='',target=None):
        super().__init__();self.body=text;self.target=target;self.popup=None
        self.setText('ⓘ');self.setFixedSize(28,28);self.setAccessibleName('Show help')
        self.setStyleSheet('QToolButton {border:0;background:transparent;color:#616161;} QToolButton:hover {background:#EBF3FC;border-radius:4px;}')
        self.clicked.connect(self.open_help)
    def setToolTip(self,text):self.body=text
    def open_help(self):
        if self.popup and self.popup.isVisible():
            self.popup.close();return
        body=(self.target.property('helpText') or self.target.accessibleDescription()) if self.target else self.body
        if self.popup:self.popup.deleteLater()
        self.popup=QDialog(self,Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint);layout=QVBoxLayout(self.popup);layout.setContentsMargins(16,16,16,16)
        label=QLabel(body);label.setWordWrap(True);label.setMaximumWidth(320);layout.addWidget(label)
        self.popup.adjustSize();screen=self.screen().availableGeometry();point=self.mapToGlobal(self.rect().bottomLeft())
        point.setX(min(point.x(),screen.right()-self.popup.width()));point.setY(min(point.y(),screen.bottom()-self.popup.height()))
        self.popup.move(point);self.popup.show()

def help_note(text):return HelpButton(text)

def install_help(root):
    """Reuse Qt layout replacement and popup windows for click-only help."""
    def walk(layout):
        if layout is None:return
        for index in range(layout.count()):
            item=layout.itemAt(index)
            if item is None:continue
            widget=item.widget()
            if item.layout():walk(item.layout())
            elif widget:
                if isinstance(widget,HelpButton):continue
                description=widget.property('helpText') or widget.toolTip()
                if description and not widget.isHidden() and not widget.property('helpInstalled'):
                    widget.setToolTip('');widget.setProperty('helpText',description);widget.setProperty('helpInstalled',True)
                    wrapper=QWidget();wrapper.setSizePolicy(widget.sizePolicy());row=QHBoxLayout(wrapper);row.setContentsMargins(0,0,0,0);row.setSpacing(4)
                    layout.replaceWidget(widget,wrapper);row.addWidget(widget,0 if isinstance(widget,QLabel) else 1);row.addWidget(HelpButton(target=widget))
                    if isinstance(widget,QLabel):row.addStretch()
                walk(widget.layout())
    for widget in [root,*root.findChildren(QWidget)]:walk(widget.layout())

class StatusLabel(BodyLabel):
    def __init__(self,text=''):
        super().__init__(text);self.setWordWrap(True)
    def setText(self,text):
        self.setStyleSheet('');super().setText(text)
    def setToolTip(self,text):self.setProperty('helpText',text);self.setAccessibleDescription(text)
    def error(self,text):
        super().setText('⚠ '+text);self.setStyleSheet('color:#B10E1C;')

class CompactNumber(QDoubleSpinBox):
    def textFromValue(self,value):
        return super().textFromValue(value).rstrip('0').rstrip(self.locale().decimalPoint()) if self.decimals() else super().textFromValue(value)

def number(value,lo,hi,step):
    box=CompactNumber();box.setButtonSymbols(QDoubleSpinBox.ButtonSymbols.NoButtons);box.setMinimumHeight(33);box.setFixedWidth(110)
    box.setRange(lo,hi);box.setDecimals(4);box.setSingleStep(step);box.setValue(value)
    box.setKeyboardTracking(False)
    box.setFrame(False);box.lineEdit().setStyleSheet('QLineEdit {background:white;border:1px solid #D1D1D1;border-radius:4px;padding:3px 6px;} QLineEdit:focus {border-bottom:2px solid #0067C0;}')
    return box




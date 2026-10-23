"""Shared round color picker with an explicit desktop eyedropper."""
import math
import re
from .ui_common import help_note,help_tip,StatusLabel,install_help,note
from PySide6.QtCore import Qt,Signal,QPointF,QRectF
from PySide6.QtGui import QColor,QPainter,QPen,QConicalGradient,QRadialGradient
from PySide6.QtWidgets import QWidget,QDialog,QVBoxLayout,QHBoxLayout,QLabel,QApplication
from qfluentwidgets import PushButton,PrimaryPushButton,LineEdit,Slider,SubtitleLabel
from .screen_color import DesktopSampler

class ColorWheel(QWidget):
    colorChanged=Signal(QColor)
    def __init__(self,parent=None):
        super().__init__(parent);self.color=QColor('#6E3617');self.setMinimumSize(240,240);self.setMaximumSize(360,360)
        self.setAccessibleName('Hue and saturation wheel');self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setToolTip('Choose hue around the circle and saturation toward its edge. Arrow keys adjust hue and saturation.')
    def setColor(self,color):self.color=QColor(color);self.update()
    def paintEvent(self,event):
        p=QPainter(self);p.setRenderHint(QPainter.RenderHint.Antialiasing)
        center=QPointF(self.width()/2,self.height()/2);radius=min(self.width(),self.height())/2-12
        rect=QRectF(center.x()-radius,center.y()-radius,2*radius,2*radius);hue=QConicalGradient(center,0)
        for i in range(7):hue.setColorAt(i/6,QColor.fromHsvF((i/6)%1,1,self.color.valueF()))
        p.setPen(Qt.PenStyle.NoPen);p.setBrush(hue);p.drawEllipse(rect)
        white=QRadialGradient(center,radius);v=self.color.value();white.setColorAt(0,QColor(v,v,v));white.setColorAt(1,QColor(v,v,v,0))
        p.setBrush(white);p.drawEllipse(rect);h=max(self.color.hsvHueF(),0)*2*math.pi;s=self.color.hsvSaturationF()
        point=QPointF(center.x()+math.cos(h)*s*radius,center.y()-math.sin(h)*s*radius)
        p.setBrush(Qt.BrushStyle.NoBrush);p.setPen(QPen(QColor('white'),3));p.drawEllipse(point,6,6)
        p.setPen(QPen(QColor('#202020'),1));p.drawEllipse(point,7,7)
    def choose(self,point):
        dx,dy=point.x()-self.width()/2,self.height()/2-point.y();radius=min(self.width(),self.height())/2-12
        self.color=QColor.fromHsvF((math.atan2(dy,dx)/(2*math.pi))%1,min(1.,math.hypot(dx,dy)/radius),self.color.valueF())
        self.update();self.colorChanged.emit(self.color)
    def mousePressEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton:self.choose(event.position())
    def mouseMoveEvent(self,event):
        if event.buttons()&Qt.MouseButton.LeftButton:self.choose(event.position())
    def keyPressEvent(self,event):
        h=max(0,self.color.hsvHueF());s=self.color.hsvSaturationF()
        if event.key()==Qt.Key.Key_Left:h=(h-1/360)%1
        elif event.key()==Qt.Key.Key_Right:h=(h+1/360)%1
        elif event.key()==Qt.Key.Key_Up:s=min(1,s+.01)
        elif event.key()==Qt.Key.Key_Down:s=max(0,s-.01)
        else:return super().keyPressEvent(event)
        self.color=QColor.fromHsvF(h,s,self.color.valueF());self.update();self.colorChanged.emit(self.color)

class ColorDialog(QDialog):
    def __init__(self,color,parent=None):
        super().__init__(parent);self.setWindowTitle('Choose color');self.resize(640,600)
        self.original=QColor(color);self.color=QColor(color);self.syncing=False
        layout=QVBoxLayout(self);layout.setContentsMargins(24,24,24,24);layout.setSpacing(12)
        layout.addWidget(SubtitleLabel('Color palette'));self.wheel=ColorWheel();layout.addWidget(self.wheel,0,Qt.AlignmentFlag.AlignHCenter)
        self.wheel.colorChanged.connect(self.setColor)
        row=QHBoxLayout();layout.addLayout(row);row.addWidget(note('Brightness'))
        self.brightness=Slider(Qt.Orientation.Horizontal);self.brightness.setRange(0,100);row.addWidget(self.brightness,1)
        self.brightness.valueChanged.connect(self.brightnessChanged)
        row=QHBoxLayout();layout.addLayout(row);row.addWidget(note('Original'))
        self.original_swatch=QLabel();self.original_swatch.setFixedSize(72,36)
        self.original_swatch.setStyleSheet(f'background:{self.original.name()};border:1px solid #999;');row.addWidget(self.original_swatch)
        row.addWidget(note('New'));self.swatch=QLabel();self.swatch.setFixedSize(72,36);row.addWidget(self.swatch)
        self.hex=LineEdit();self.hex.setAccessibleName('Hex color, copy and paste');self.hex.textChanged.connect(self.hexChanged);row.addWidget(self.hex,1)
        copy=PushButton('Copy');copy.clicked.connect(lambda:QApplication.clipboard().setText(self.color.name().upper()));row.addWidget(copy)
        self.pick_button=PushButton('Color Picker · Pick from desktop…');layout.addWidget(self.pick_button);self.pick_button.clicked.connect(self.pickDesktop)
        layout.addWidget(help_note('Open a reference in any desktop app first. Picking temporarily hides this tool; click a color on any screen. Esc or right-click cancels. Only RGB is sampled; opacity stays separate.'))
        self.hint=StatusLabel('');layout.addWidget(self.hint)
        row=QHBoxLayout();layout.addLayout(row);row.addStretch()
        cancel=PushButton('Cancel');cancel.clicked.connect(self.reject);row.addWidget(cancel)
        self.accept_button=PrimaryPushButton('Use color');self.accept_button.clicked.connect(self.accept);row.addWidget(self.accept_button)
        self.sampler=DesktopSampler(self);self.sampler.picked.connect(self.setColor);self.sampler.failed.connect(self.hint.error)
        self.sampler.finished.connect(self.restoreFocus)
        self.setColor(self.color);install_help(self)
    def restoreFocus(self):
        if self.isVisible():self.raise_();self.activateWindow()
    def setColor(self,color):
        self.color=QColor(color);self.wheel.setColor(color);self.syncing=True
        self.brightness.setValue(round(color.valueF()*100));self.syncing=False
        self.hex.blockSignals(True);self.hex.setText(color.name().upper());self.hex.blockSignals(False)
        self.swatch.setStyleSheet(f'background:{color.name()};border:1px solid #999;');self.swatch.setAccessibleName('New color '+color.name())
        self.hint.setText('');self.hex.setToolTip('Enter six hex digits, with or without #. The New swatch updates immediately.');self.accept_button.setEnabled(True)
    def hexChanged(self,text):
        text=text.strip();text=text if text.startswith('#') else '#'+text
        if re.fullmatch(r'#[0-9a-fA-F]{6}',text):self.setColor(QColor(text))
        else:self.hint.error('Enter six hex digits, with or without #.');self.accept_button.setEnabled(False)
    def brightnessChanged(self,value):
        if not self.syncing:self.setColor(QColor.fromHsvF(max(0,self.color.hsvHueF()),self.color.hsvSaturationF(),value/100))
    def pickDesktop(self):self.sampler.start()
    def done(self,result):
        if self.sampler.active:self.sampler.cancel()
        super().done(result)

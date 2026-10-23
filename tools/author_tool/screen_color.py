"""Desktop eyedropper using Qt screen capture, window geometry and input events.

Captures only on explicit Pick from desktop. Frames live in memory for that
selection, never on disk. Geometry-to-image mapping handles per-screen scaling.
"""
from PySide6.QtCore import QObject,Signal,Qt,QTimer,QPoint
from PySide6.QtGui import QColor,QPainter,QPen
from PySide6.QtWidgets import QWidget,QApplication

def pixel_color(image,geometry,global_point):
    if image.isNull() or not geometry.contains(global_point):raise ValueError('Point is outside this screen.')
    x=min(image.width()-1,int((global_point.x()-geometry.x())*image.width()/geometry.width()))
    y=min(image.height()-1,int((global_point.y()-geometry.y())*image.height()/geometry.height()))
    return image.pixelColor(x,y)

class ScreenOverlay(QWidget):
    picked=Signal(QColor);cancelled=Signal()
    def __init__(self,geometry,image):
        super().__init__(None,Qt.WindowType.FramelessWindowHint|Qt.WindowType.WindowStaysOnTopHint|Qt.WindowType.Tool)
        self.screen_rect=geometry;self.image=image;self.position=QPoint(20,20)
        self.setGeometry(geometry);self.setMouseTracking(True);self.setCursor(Qt.CursorShape.CrossCursor)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
    def paintEvent(self,event):
        painter=QPainter(self);painter.drawImage(self.rect(),self.image)
        color=pixel_color(self.image,self.screen_rect,self.mapToGlobal(self.position))
        x=max(0,min(self.width()-280,self.position.x()+18));y=max(0,min(self.height()-54,self.position.y()+18))
        painter.fillRect(x,y,280,54,QColor('#202124'));painter.fillRect(x+8,y+8,32,32,color)
        painter.setPen(QColor('white'));painter.drawText(x+48,y+22,color.name().upper())
        painter.drawText(x+48,y+43,'Click to pick · Esc to cancel')
    def mouseMoveEvent(self,event):self.position=event.position().toPoint();self.update()
    def mousePressEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton:
            self.picked.emit(pixel_color(self.image,self.screen_rect,event.globalPosition().toPoint()))
        elif event.button()==Qt.MouseButton.RightButton:self.cancelled.emit()
    def keyPressEvent(self,event):
        if event.key()==Qt.Key.Key_Escape:self.cancelled.emit()

class DesktopSampler(QObject):
    picked=Signal(QColor);finished=Signal();failed=Signal(str)
    def __init__(self,parent=None):super().__init__(parent);self.overlays=[];self.hidden=[];self.active=False
    def start(self):
        if self.active:return
        self.active=True;self.hidden=[w for w in QApplication.topLevelWidgets() if w.isVisible()]
        for window in self.hidden:window.hide()
        QTimer.singleShot(250,self.capture)
    def capture(self):
        if not self.active:return
        try:
            screens=QApplication.screens()
            frames=[(s.geometry(),s.grabWindow(0).toImage()) for s in screens]
            if not frames or any(image.isNull() for _,image in frames):raise ValueError('Desktop capture is unavailable. Paste a hex color instead.')
            for geometry,image in frames:
                overlay=ScreenOverlay(geometry,image);overlay.picked.connect(self.choose);overlay.cancelled.connect(self.cancel)
                self.overlays.append(overlay);overlay.show()
            self.overlays[-1].activateWindow();self.overlays[-1].setFocus()
        except Exception as error:self.cancel();self.failed.emit(str(error))
    def choose(self,color):self.finish();self.picked.emit(color)
    def cancel(self):self.finish()
    def finish(self):
        self.active=False
        for overlay in self.overlays:overlay.close();overlay.deleteLater()
        self.overlays=[]
        for window in self.hidden:window.show()
        if self.hidden:self.hidden[-1].raise_();self.hidden[-1].activateWindow()
        self.hidden=[];self.finished.emit()

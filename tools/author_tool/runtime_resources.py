"""PyInstaller runtime hook: standard zipimport, with no resource extraction."""
import os
import sys
sys.path.insert(0, os.path.join(sys._MEIPASS, 'author-resources.zip'))

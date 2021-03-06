QT       += core gui widgets opengl

TARGET = w-1
TEMPLATE = app


win32 {
  INCLUDEPATH += $$(KINECTSDK20_DIR)\inc
  LIBS += opengl32.lib
  contains(QT_ARCH, i386) {
    LIBS += $$(KINECTSDK20_DIR)\lib\x86\kinect20.lib
  }
  else {
    LIBS += $$(KINECTSDK20_DIR)\lib\x64\kinect20.lib
  }
}

SOURCES += main.cpp \
    mainwindow.cpp \
    depthwidget.cpp \
    videowidget.cpp \
    rgbdwidget.cpp \
    threedwidget.cpp \
    irwidget.cpp

HEADERS  += mainwindow.h \
    util.h \
    depthwidget.h \
    videowidget.h \
    rgbdwidget.h \
    threedwidget.h \
    globals.h \
    irwidget.h

FORMS    += mainwindow.ui

DISTFILES += \
    .gitignore \
    shaders/mix.fs.glsl \
    shaders/mix.vs.glsl \
    README.md

RESOURCES += \
    w-1.qrc

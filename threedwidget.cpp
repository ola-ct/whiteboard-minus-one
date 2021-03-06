/*

    Copyright (c) 2015 Oliver Lau <ola@ct.de>, Heise Medien GmbH & Co. KG

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "util.h"
#include "threedwidget.h"

#include <limits>

#include <QtMath>
#include <QDebug>
#include <QString>
#include <QGLFramebufferObject>
#include <QGLShaderProgram>
#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>
#include <QFile>
#include <QRect>
#include <QSizeF>
#include <QPoint>

#include <Kinect.h>

static const int PROGRAM_VERTEX_ATTRIBUTE = 0;
static const int PROGRAM_TEXCOORD_ATTRIBUTE = 1;
static const QVector2D Vertices[4] = {
  QVector2D(+1.920f, +1.080f),
  QVector2D(+1.920f, -1.080f),
  QVector2D(-1.920f, +1.080f),
  QVector2D(-1.920f, -1.080f)
};
static const QVector2D Vertices4FBO[4] = {
  QVector2D(-1.f, -1.f),
  QVector2D(-1.f, +1.f),
  QVector2D(+1.f, -1.f),
  QVector2D(+1.f, +1.f)
};
static const QVector2D TexCoords[4] = {
  QVector2D(0, 0),
  QVector2D(0, 1),
  QVector2D(1, 0),
  QVector2D(1, 1)
};

static const QVector3D XAxis(1.f, 0.f, 0.f);
static const QVector3D YAxis(0.f, 1.f, 0.f);
static const QVector3D ZAxis(0.f, 0.f, 1.f);

static const float HFOV = 70.f;
static const float VFOV = 60.f;


struct DSP {
  DSP(void)
    : x(0)
    , y(0)
  { /* ... */ }
  DSP(const DepthSpacePoint *dsp)
    : x((dsp->X == -std::numeric_limits<float>::infinity()) ? -1 : INT16(dsp->X))
    , y((dsp->Y == -std::numeric_limits<float>::infinity()) ? -1 : INT16(dsp->Y))
  { /* ... */ }
  INT16 x;
  INT16 y;
};


class ThreeDWidgetPrivate {
public:
  ThreeDWidgetPrivate(void)
    : xRot(9.3f)
    , yRot(0.9f)
    , zRot(0.f)
    , xTrans(0.f)
    , yTrans(0.f)
    , zTrans(-1.35f)
    , scale(1.0)
    , lastFrameFBO(nullptr)
    , imageFBO(nullptr)
    , shaderProgram(nullptr)
    , mapping(new DepthSpacePoint[ColorSize])
    , intMapping(new DSP[ColorSize])
    , timestamp(0)
    , firstPaintEventPending(true)
    , frameCount(0)
    , haloSize(0)
  {
  }
  ~ThreeDWidgetPrivate()
  {
    SafeRelease(coordinateMapper);
    SafeDelete(shaderProgram);
    SafeDelete(lastFrameFBO);
    SafeDelete(imageFBO);
    SafeDeleteArray(mapping);
  }

  bool mixShaderProgramIsValid(void) const {
    return shaderProgram != nullptr && shaderProgram->isLinked();
  }

  GLfloat xRot;
  GLfloat yRot;
  GLfloat zRot;
  GLfloat xTrans;
  GLfloat yTrans;
  GLfloat zTrans;
  QMatrix4x4 mvMatrix;

  QGLFramebufferObject *lastFrameFBO;
  QGLFramebufferObject *imageFBO;
  QGLShaderProgram *shaderProgram;

  static const int MaxHaloSize = 2 * 16 * 2 * 16;
  int haloSize;
  QVector2D halo[MaxHaloSize];

  IKinectSensor *kinectSensor;
  ICoordinateMapper *coordinateMapper;
  DepthSpacePoint *mapping;
  DSP *intMapping;

  GLuint videoTextureHandle;
  GLuint depthTextureHandle;
  GLuint mapTextureHandle;

  GLint imageTextureLocation;
  GLint videoTextureLocation;
  GLint depthTextureLocation;
  GLint mapTextureLocation;
  GLint nearThresholdLocation;
  GLint farThresholdLocation;
  GLint gammaLocation;
  GLint contrastLocation;
  GLint saturationLocation;
  GLint mvMatrixLocation;
  GLint haloLocation;
  GLint haloSizeLocation;
  GLint ignoreDepthLocation;

  qreal scale;
  QRect viewport;
  QSize resolution;
  QPoint offset;
  QPoint lastMousePos;
  INT64 timestamp;
  bool firstPaintEventPending;
  int frameCount;
};


static const QGLFormat DefaultGLFormat = QGLFormat(QGL::DoubleBuffer | QGL::NoDepthBuffer | QGL::AlphaChannel | QGL::NoAccumBuffer | QGL::NoStencilBuffer | QGL::NoStereoBuffers | QGL::HasOverlay | QGL::NoSampleBuffers);

ThreeDWidget::ThreeDWidget(QWidget *parent)
  : QGLWidget(DefaultGLFormat, parent)
  , d_ptr(new ThreeDWidgetPrivate)
{
  Q_D(ThreeDWidget);
  setFocusPolicy(Qt::StrongFocus);
  setFocus(Qt::OtherFocusReason);
  setMouseTracking(true);
  setCursor(Qt::OpenHandCursor);
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMaximumSize(ColorWidth, ColorHeight);
  setMinimumSize(ColorWidth / 8, ColorHeight / 8);
  HRESULT hr = GetDefaultKinectSensor(&d->kinectSensor);
  if (SUCCEEDED(hr) && d->kinectSensor != nullptr)
    hr = d->kinectSensor->get_CoordinateMapper(&d->coordinateMapper);
}


ThreeDWidget::~ThreeDWidget()
{
  // ...
}


void ThreeDWidget::makeShader(void)
{
  Q_D(ThreeDWidget);
  SafeRenew(d->shaderProgram, new QGLShaderProgram);
  d->shaderProgram->addShaderFromSourceFile(QGLShader::Fragment, ":/shaders/mix.fs.glsl");
  d->shaderProgram->addShaderFromSourceFile(QGLShader::Vertex, ":/shaders/mix.vs.glsl");
  d->shaderProgram->bindAttributeLocation("aVertex", PROGRAM_VERTEX_ATTRIBUTE);
  d->shaderProgram->bindAttributeLocation("aTexCoord", PROGRAM_TEXCOORD_ATTRIBUTE);
  d->shaderProgram->enableAttributeArray(PROGRAM_VERTEX_ATTRIBUTE);
  d->shaderProgram->enableAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE);
  d->shaderProgram->setAttributeArray(PROGRAM_TEXCOORD_ATTRIBUTE, TexCoords);
  d->shaderProgram->link();
  qDebug() << "Shader linker says:" << d->shaderProgram->log();
  Q_ASSERT_X(d->mixShaderProgramIsValid(), "ThreeDWidget::makeShader()", "error in shader program");
  d->shaderProgram->bind();

  d->videoTextureLocation = d->shaderProgram->uniformLocation("uVideoTexture");
  d->shaderProgram->setUniformValue(d->videoTextureLocation, 0);

  d->depthTextureLocation = d->shaderProgram->uniformLocation("uDepthTexture");
  d->shaderProgram->setUniformValue(d->depthTextureLocation, 1);

  d->mapTextureLocation = d->shaderProgram->uniformLocation("uMapTexture");
  d->shaderProgram->setUniformValue(d->mapTextureLocation, 2);

  d->imageTextureLocation = d->shaderProgram->uniformLocation("uImageTexture");
  d->shaderProgram->setUniformValue(d->imageTextureLocation, 3);

  d->gammaLocation = d->shaderProgram->uniformLocation("uGamma");
  d->contrastLocation = d->shaderProgram->uniformLocation("uContrast");
  d->saturationLocation = d->shaderProgram->uniformLocation("uSaturation");
  d->nearThresholdLocation = d->shaderProgram->uniformLocation("uNearThreshold");
  d->farThresholdLocation = d->shaderProgram->uniformLocation("uFarThreshold");
  d->mvMatrixLocation = d->shaderProgram->uniformLocation("uMatrix");
  d->haloLocation = d->shaderProgram->uniformLocation("uHalo");
  d->haloSizeLocation = d->shaderProgram->uniformLocation("uHaloSize");
  d->ignoreDepthLocation = d->shaderProgram->uniformLocation("uIgnoreDepth");

  d->shaderProgram->setUniformValue(d->ignoreDepthLocation, true);
}


void ThreeDWidget::initializeGL(void)
{
  Q_D(ThreeDWidget);

  initializeOpenGLFunctions();

  glDisable(GL_ALPHA_TEST);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  glGenTextures(1, &d->videoTextureHandle);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, d->videoTextureHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  glGenTextures(1, &d->depthTextureHandle);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, d->depthTextureHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  glGenTextures(1, &d->mapTextureHandle);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, d->mapTextureHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  d->imageFBO = new QGLFramebufferObject(ColorWidth, ColorHeight);
  d->lastFrameFBO = new QGLFramebufferObject(ColorWidth, ColorHeight);

  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, d->lastFrameFBO->texture());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, d->imageFBO->texture());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  makeWorldMatrix();
  makeShader();
  setHaloSize(3);
}


void ThreeDWidget::resizeGL(int width, int height)
{
  updateViewport(width, height);
}


void ThreeDWidget::paintGL(void)
{
  Q_D(ThreeDWidget);

  if (d->firstPaintEventPending) {
    d->firstPaintEventPending = false;
    GLint GLMajorVer, GLMinorVer;
    glGetIntegerv(GL_MAJOR_VERSION, &GLMajorVer);
    glGetIntegerv(GL_MINOR_VERSION, &GLMinorVer);
    qDebug() << QString("OpenGL %1.%2").arg(GLMajorVer).arg(GLMinorVer);
    qDebug() << "hasOpenGLFeature(QOpenGLFunctions::Multitexture) ==" << hasOpenGLFeature(QOpenGLFunctions::Multitexture);
    qDebug() << "hasOpenGLFeature(QOpenGLFunctions::Shaders) ==" << hasOpenGLFeature(QOpenGLFunctions::Shaders);
    qDebug() << "hasOpenGLFeature(QOpenGLFunctions::Framebuffers) ==" << hasOpenGLFeature(QOpenGLFunctions::Framebuffers);
    qDebug() << "QGLFramebufferObject::hasOpenGLFramebufferBlit() ==" << QGLFramebufferObject::hasOpenGLFramebufferBlit();
    qDebug() << "doubleBuffer() ==" << doubleBuffer();
    GLint h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h1);
    glActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h2);
    glActiveTexture(GL_TEXTURE3);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h3);
    glActiveTexture(GL_TEXTURE4);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &h4);
    emit ready();
  }
  else if (d->lastFrameFBO != nullptr && d->imageFBO != nullptr && d->shaderProgram != nullptr && d->timestamp > 0) {
    drawIntoFBO();
    drawOntoScreen();
  }
}


void ThreeDWidget::drawOntoScreen(void)
{
  Q_D(ThreeDWidget);
  d->shaderProgram->setAttributeArray(PROGRAM_VERTEX_ATTRIBUTE, Vertices);
  d->shaderProgram->setUniformValue(d->mvMatrixLocation, d->mvMatrix);
  glClearColor(.15f, .15f, .15f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glViewport(0, 0, width(), height());
  // glViewport(d->viewport.x(), d->viewport.y(), d->viewport.width(), d->viewport.height());
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}


void ThreeDWidget::drawIntoFBO(void)
{
  Q_D(ThreeDWidget);
  d->imageFBO->bind();
  d->shaderProgram->setAttributeArray(PROGRAM_VERTEX_ATTRIBUTE, Vertices4FBO);
  d->shaderProgram->setUniformValue(d->mvMatrixLocation, QMatrix4x4());
  glViewport(0, 0, d->imageFBO->width(), d->imageFBO->height());
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glActiveTexture(GL_TEXTURE3);
  glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, d->imageFBO->width(), d->imageFBO->height(), 0);
  d->imageFBO->release();

}


void ThreeDWidget::process(INT64 nTime, const uchar *pRGB, const UINT16 *pDepth, int nMinReliableDist, int nMaxDist)
{
  Q_D(ThreeDWidget);
  Q_UNUSED(nMinReliableDist);
  Q_UNUSED(nMaxDist);

  Q_ASSERT_X(pDepth != nullptr && pRGB != nullptr, "ThreeDWidget::process()", "RGB or depth pointer must not be null");

  d->timestamp = nTime;

  HRESULT hr = d->coordinateMapper->MapColorFrameToDepthSpace(DepthSize, pDepth, ColorSize, d->mapping);
  if (FAILED(hr))
    qWarning() << "MapColorFrameToDepthSpace() failed.";
  DSP *dst = d->intMapping;
  const DepthSpacePoint *src = d->mapping;
  const DepthSpacePoint *const srcEnd = d->mapping + ColorSize;
  while (src < srcEnd)
    *dst++ = src++;

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, d->videoTextureHandle);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ColorWidth, ColorHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, pRGB);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "ThreeDWidget::process()", "glTexImage2D() failed");

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, d->depthTextureHandle);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, DepthWidth, DepthHeight, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, pDepth);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "ThreeDWidget::process()", "glTexImage2D() failed");

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, d->mapTextureHandle);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16I, ColorWidth, ColorHeight, 0, GL_RG_INTEGER, GL_UNSIGNED_SHORT, d->intMapping);
  Q_ASSERT_X(glGetError() == GL_NO_ERROR, "ThreeDWidget::process()", "glTexImage2D() failed");

  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, d->lastFrameFBO->texture());

  if (++d->frameCount > 1)
    d->shaderProgram->setUniformValue(d->ignoreDepthLocation, false);

  updateGL();
}


void ThreeDWidget::mousePressEvent(QMouseEvent *e)
{
  Q_D(ThreeDWidget);
  setCursor(Qt::ClosedHandCursor);
  d->lastMousePos = e->pos();
}


void ThreeDWidget::mouseReleaseEvent(QMouseEvent *)
{
  setCursor(Qt::OpenHandCursor);
}


void ThreeDWidget::mouseMoveEvent(QMouseEvent *e)
{
  Q_D(ThreeDWidget);
  bool changed = false;
  if (e->buttons() & Qt::LeftButton) {
    d->xRot += .3f * (e->y() - d->lastMousePos.y());
    d->yRot += .3f * (e->x() - d->lastMousePos.x());
    changed = true;
  }
  else if (e->buttons() & Qt::RightButton) {
    d->xTrans += .01f * (e->x() - d->lastMousePos.x());
    d->yTrans -= .01f * (e->y() - d->lastMousePos.y());
    changed = true;
  }
  if (changed) {
    makeWorldMatrix();
    updateGL();
  }
  d->lastMousePos = e->pos();
}


void ThreeDWidget::wheelEvent(QWheelEvent *e)
{
  Q_D(ThreeDWidget);
  d->zTrans += (e->delta() < 0 ? -1 : 1 ) * ((e->modifiers() & Qt::ShiftModifier)? .04f : .2f);
  updateViewport();
  makeWorldMatrix();
  updateGL();
}


void ThreeDWidget::makeWorldMatrix(void)
{
  Q_D(ThreeDWidget);
  d->mvMatrix.setToIdentity();
  d->mvMatrix.perspective(VFOV, float(width()) / float(height()), 0.001f, 20.f);
  d->mvMatrix.translate(0.f, 0.f, d->zTrans);
  d->mvMatrix.rotate(d->xRot, XAxis);
  d->mvMatrix.rotate(d->yRot, YAxis);
  d->mvMatrix.rotate(d->zRot, ZAxis);
  d->mvMatrix.translate(d->xTrans, d->yTrans, 0.f);
  qDebug() << d->xRot << d->yRot << d->zTrans;
}


void ThreeDWidget::updateViewport(void)
{
  updateViewport(size());
}


void ThreeDWidget::updateViewport(int w, int h)
{
  Q_D(ThreeDWidget);
  const QSizeF &glSize = d->scale * QSizeF(d->imageFBO->size());
  const QPoint &topLeft = QPoint(w - int(glSize.width()), h - int(glSize.height())) / 2;
  d->viewport = QRect(topLeft + d->offset, glSize.toSize());
  d->resolution = d->viewport.size();
  updateGL();
}


void ThreeDWidget::updateViewport(const QSize &sz)
{
  updateViewport(sz.width(), sz.height());
}


void ThreeDWidget::setContrast(GLfloat contrast)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->shaderProgram->setUniformValue(d->contrastLocation, contrast);
  updateGL();
}


void ThreeDWidget::setSaturation(GLfloat saturation)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->shaderProgram->setUniformValue(d->saturationLocation, saturation);
  updateGL();
}


void ThreeDWidget::setGamma(GLfloat gamma)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  d->shaderProgram->setUniformValue(d->gammaLocation, gamma);
  updateGL();
}


void ThreeDWidget::setNearThreshold(GLfloat nearThreshold)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  qDebug() << "ThreeDWidget::setNearThreshold(" << nearThreshold << ")";
  d->shaderProgram->setUniformValue(d->nearThresholdLocation, nearThreshold);
  updateGL();
}


void ThreeDWidget::setFarThreshold(GLfloat farThreshold)
{
  Q_D(ThreeDWidget);
  makeCurrent();
  qDebug() << "ThreeDWidget::setFarThreshold(" << farThreshold << ")";
  d->shaderProgram->setUniformValue(d->farThresholdLocation, farThreshold);
  updateGL();
}


void ThreeDWidget::setHaloSize(int s)
{
  Q_D(ThreeDWidget);
  d->haloSize = 0;
  const int xd = s;
  const int yd = s / 2;
  const int S = (xd + yd) / 2;
  const int x0 = -xd;
  const int x1 = +xd;
  const int y0 = -yd;
  const int y1 = +yd;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x)
      if (qAbs(x) + qAbs(y) <= S)
        d->halo[d->haloSize++] = QVector2D(float(x) / DepthWidth, float(y) / DepthHeight);
  d->shaderProgram->setUniformValueArray(d->haloLocation, d->halo, d->haloSize);
  d->shaderProgram->setUniformValue(d->haloSizeLocation, d->haloSize);
  updateGL();
}


void ThreeDWidget::setRefPoints(const QVector<QVector3D>& refPoints)
{
  Q_D(ThreeDWidget);
  Q_ASSERT_X(refPoints.count() == 3, "ThreeDWidget::setRefPoints()", "refPoints.count() must equal 3");
  const QVector3D &P = refPoints.at(0);
  const QVector3D &Q = refPoints.at(1);
  const QVector3D &R = refPoints.at(2);
  const QVector3D &norm = QVector3D::normal(Q - P, R - P);
  d->xRot = qRadiansToDegrees(qAcos(norm.z()));
  d->yRot = 0.f;
  d->zRot = 0.f;
  makeWorldMatrix();
  updateGL();
}

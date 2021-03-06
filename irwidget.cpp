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

#include "globals.h"
#include "util.h"
#include "irwidget.h"

#include <QPainter>

class IRWidgetPrivate {
public:
  IRWidgetPrivate(void)
    : irFrame(IRWidth, IRHeight, QImage::Format_ARGB32)
    , windowAspectRatio(1.0)
    , imageAspectRatio(qreal(IRWidth) / qreal(IRHeight))
  { /* ... */ }
  ~IRWidgetPrivate(void)
  { /* ... */ }

  QRect destRect;
  QImage irFrame;
  qreal imageAspectRatio;
  qreal windowAspectRatio;
};


// InfraredSourceValueMaximum is the highest value that can be returned in the InfraredFrame.
// It is cast to a float for readability in the visualization code.
static const float InfraredSourceValueMaximum = float(USHRT_MAX);

// The InfraredOutputValueMinimum value is used to set the lower limit, post processing, of the
// infrared data that we will render.
// Increasing or decreasing this value sets a brightness "wall" either closer or further away.
static const float InfraredOutputValueMinimum = 0.f;

// The InfraredOutputValueMaximum value is the upper limit, post processing, of the
// infrared data that we will render.
static const float InfraredOutputValueMaximum = 1.f;

// The InfraredSceneValueAverage value specifies the average infrared value of the scene.
// This value was selected by analyzing the average pixel intensity for a given scene.
// Depending on the visualization requirements for a given application, this value can be
// hard coded, as was done here, or calculated by averaging the intensity for each pixel prior
// to rendering.
static const float InfraredSceneValueAverage = .1f;

// The InfraredSceneStandardDeviations value specifies the number of standard deviations
// to apply to InfraredSceneValueAverage. This value was selected by analyzing data
// from a given scene.
// Depending on the visualization requirements for a given application, this value can be
// hard coded, as was done here, or calculated at runtime.
static const float InfraredSceneStandardDeviations = 3.f;


IRWidget::IRWidget(QWidget *parent)
  : QWidget(parent)
  , d_ptr(new IRWidgetPrivate)
{
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMaximumSize(IRWidth, IRHeight);
  setMinimumSize(IRWidth / 2, IRHeight / 2);
}


void IRWidget::setIRData(INT64 nTime, const UINT16 *pBuffer, int nWidth, int nHeight)
{
  Q_D(IRWidget);
  Q_UNUSED(nTime);

  if (nWidth != IRWidth || nHeight != IRHeight || pBuffer == nullptr)
    return;

  const UINT16* pBufferEnd = pBuffer + nWidth * nHeight;
  QRgb *dst = reinterpret_cast<QRgb*>(d->irFrame.bits());
  while (pBuffer < pBufferEnd) {
    float intensityRatio = float(*pBuffer)
        / InfraredSourceValueMaximum
        / InfraredSceneValueAverage
        / InfraredSceneStandardDeviations;
    intensityRatio = clamp(intensityRatio, InfraredOutputValueMinimum, InfraredOutputValueMaximum);
    BYTE intensity = BYTE(intensityRatio * 0xff);
    *dst = qRgb(intensity, intensity, intensity);
    ++dst;
    ++pBuffer;
  }
  update();
}


void IRWidget::resizeEvent(QResizeEvent* e)
{
  Q_D(IRWidget);
  d->windowAspectRatio = qreal(e->size().width()) / e->size().height();
  if (d->windowAspectRatio < d->imageAspectRatio) {
    const int h = qRound(width() / d->imageAspectRatio);
    d->destRect = QRect(0, (height()-h)/2, width(), h);
  }
  else {
    const int w = qRound(height() * d->imageAspectRatio);
    d->destRect = QRect((width()-w)/2, 0, w, height());
  }
}


void IRWidget::paintEvent(QPaintEvent *)
{
  Q_D(IRWidget);

  if (d->irFrame.isNull() || qFuzzyIsNull(d->imageAspectRatio) || qFuzzyIsNull(d->windowAspectRatio))
    return;

  QPainter p(this);
  p.fillRect(rect(), Qt::gray);
  p.drawImage(d->destRect, d->irFrame);
}




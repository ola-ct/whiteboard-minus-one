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
#include "depthwidget.h"

#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QElapsedTimer>
#include <QFont>

class DepthWidgetPrivate
{
public:
  DepthWidgetPrivate(void)
    : depthFrame(DepthWidth, DepthHeight, QImage::Format_ARGB32)
    , windowAspectRatio(1.0)
    , imageAspectRatio(qreal(DepthWidth) / qreal(DepthHeight))
    , fpsArray(10)
    , fpsIndex(0)
    , fps(25.f)
  {
    for (int h = 0; h < NCOLORS; ++h) {
      QColor c = QColor::fromHsl(NCOLORS * (NCOLORS - h) / 360, 128 , 128);
      hue[h].rgbRed = c.red();
      hue[h].rgbGreen = c.green();
      hue[h].rgbBlue = c.blue();
      hue[h].rgbReserved = 0xffU;
    }
  }
  ~DepthWidgetPrivate()
  {
    // ...
  }

  static const int NCOLORS = 360;

  QImage depthFrame;
  RGBQUAD hue[NCOLORS];

  qreal windowAspectRatio;
  qreal imageAspectRatio;
  QElapsedTimer timer;
  QVector<float> fpsArray;
  int fpsIndex;
  float fps;
};

DepthWidget::DepthWidget(QWidget *parent)
  : QWidget(parent)
  , d_ptr(new DepthWidgetPrivate)
{
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  setMaximumSize(DepthWidth, DepthHeight);
  setMinimumSize(DepthWidth / 2, DepthHeight / 2);
}


void DepthWidget::resizeEvent(QResizeEvent* e)
{
  Q_D(DepthWidget);
  d->windowAspectRatio = (qreal)e->size().width() / e->size().height();
}


void DepthWidget::paintEvent(QPaintEvent *)
{
  Q_D(DepthWidget);
  QPainter p(this);

  p.fillRect(rect(), Qt::gray);
  QRect destRect;
  if (d->depthFrame.isNull() || qFuzzyIsNull(d->imageAspectRatio) || qFuzzyIsNull(d->windowAspectRatio))
    return;
  if (d->windowAspectRatio < d->imageAspectRatio) {
    const int h = int(width() / d->imageAspectRatio);
    destRect = QRect(0, (height()-h)/2, width(), h);
  }
  else {
    const int w = int(height() * d->imageAspectRatio);
    destRect = QRect((width()-w)/2, 0, w, height());
  }

  p.drawImage(destRect, d->depthFrame);
  p.setPen(Qt::white);
  p.setBrush(Qt::transparent);
  static const QFont defaultFont("system, sans-serif", 8);
  p.setFont(defaultFont);
  p.drawText(4, height() - 4, QString("%1 fps").arg(d->fps, 0, 'f', 1));
}


void DepthWidget::setDepthData(INT64 nTime, const UINT16 *pBuffer, int nWidth, int nHeight, int nMinDepth, int nMaxDepth)
{
  Q_D(DepthWidget);
  Q_UNUSED(nTime);
  Q_UNUSED(nMinDepth);

  qint64 ms = d->timer.elapsed();
  d->timer.start();
  d->fpsArray[d->fpsIndex] = 1e3f / ms;
  if (++d->fpsIndex >= d->fpsArray.count())
    d->fpsIndex = 0;
  float fpsSum = 0.f;
  for (int i = 0; i < d->fpsArray.count(); ++i)
    fpsSum += d->fpsArray.at(i);
  d->fps = fpsSum / d->fpsArray.count();

  if (nWidth != DepthWidth || nHeight != DepthHeight || pBuffer == nullptr)
    return;

  const UINT16* pBufferEnd = pBuffer + nWidth * nHeight;
  BYTE *dst = d->depthFrame.bits();
  while (pBuffer < pBufferEnd) {
    USHORT depth = *pBuffer;
    if (depth == 0 || depth == USHRT_MAX) {
      dst[0] = 0x00U;
      dst[1] = 0x00U;
      dst[2] = 0x00U;
      dst[3] = 0xffU;
    }
    else {
      const RGBQUAD &color = d->hue[DepthWidgetPrivate::NCOLORS * depth / nMaxDepth];
      dst[0] = color.rgbRed;
      dst[1] = color.rgbGreen;
      dst[2] = color.rgbBlue;
      dst[3] = 0xffU;
    }
    dst += 4;
    ++pBuffer;
  }
  update();
}


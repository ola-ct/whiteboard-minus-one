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

#include <Kinect.h>

#include <QDebug>
#include <QBoxLayout>

#include "globals.h"
#include "util.h"
#include "depthwidget.h"
#include "videowidget.h"
#include "rgbdwidget.h"
#include "threedwidget.h"
#include "irwidget.h"
#include "mainwindow.h"

#include "ui_mainwindow.h"



class MainWindowPrivate {
public:
  MainWindowPrivate(QWidget *parent = nullptr)
    : kinectSensor(nullptr)
    , depthFrameReader(nullptr)
    , colorFrameReader(nullptr)
    , irFrameReader(nullptr)
    , depthWidget(nullptr)
    , videoWidget(nullptr)
    , rgbdWidget(nullptr)
    , threeDWidget(nullptr)
    , irWidget(nullptr)
    , colorBuffer(new RGBQUAD[ColorSize])
  {
    Q_UNUSED(parent);
    // ...
  }
  ~MainWindowPrivate()
  {
    if (kinectSensor)
      kinectSensor->Close();
    SafeRelease(kinectSensor);
    SafeDelete(colorBuffer);
  }

  IKinectSensor *kinectSensor;
  IDepthFrameReader *depthFrameReader;
  IColorFrameReader *colorFrameReader;
  IInfraredFrameReader *irFrameReader;

  DepthWidget *depthWidget;
  VideoWidget *videoWidget;
  RGBDWidget *rgbdWidget;
  ThreeDWidget *threeDWidget;
  IRWidget *irWidget;

  RGBQUAD *colorBuffer;
};


MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , d_ptr(new MainWindowPrivate(this))
{
  Q_D(MainWindow);
  ui->setupUi(this);

  initKinect();

  d->depthWidget = new DepthWidget;
  d->rgbdWidget = new RGBDWidget;
  d->videoWidget = new VideoWidget;
  d->threeDWidget = new ThreeDWidget;
  d->irWidget = new IRWidget;

  QBoxLayout *hbox = new QBoxLayout(QBoxLayout::LeftToRight);
  hbox->addWidget(d->videoWidget);
  hbox->addWidget(d->depthWidget);
  hbox->addWidget(d->rgbdWidget);
  hbox->addWidget(d->irWidget);

  QBoxLayout *vbox = new QBoxLayout(QBoxLayout::TopToBottom);
  vbox->addLayout(hbox);
  vbox->addWidget(d->threeDWidget);

  ui->gridLayout->addLayout(vbox, 0, 0);

  QObject::connect(d->threeDWidget, SIGNAL(ready()), SLOT(initAfterGL()));

  QObject::connect(ui->gammaDoubleSpinBox, SIGNAL(valueChanged(double)), SLOT(gammaChanged(double)));
  QObject::connect(ui->contrastDoubleSpinBox, SIGNAL(valueChanged(double)), SLOT(contrastChanged(double)));
  QObject::connect(ui->saturationDoubleSpinBox, SIGNAL(valueChanged(double)), SLOT(saturationChanged(double)));
  QObject::connect(ui->actionExit, SIGNAL(triggered(bool)),SLOT(close()));
  QObject::connect(ui->farVerticalSlider, SIGNAL(valueChanged(int)), SLOT(setFarThreshold(int)));
  QObject::connect(ui->nearVerticalSlider, SIGNAL(valueChanged(int)), SLOT(setNearThreshold(int)));
  QObject::connect(ui->haloRadiusVerticalSlider, SIGNAL(valueChanged(int)), d->threeDWidget, SLOT(setHaloRadius(int)));
  QObject::connect(d->rgbdWidget, SIGNAL(refPointsSet(QVector<QVector3D>)), d->threeDWidget, SLOT(setRefPoints(QVector<QVector3D>)));

  // showMaximized();
}


MainWindow::~MainWindow()
{
  delete ui;
}


void MainWindow::initAfterGL(void)
{
  // Q_D(MainWindow);
  qDebug() << "MainWindow::initAfterGL()";
  ui->actionMapFromColorToDepth->setChecked(true);
  ui->actionMatchColorAndDepthSpace->setChecked(true);
  ui->haloRadiusVerticalSlider->setValue(10);
  ui->nearVerticalSlider->setValue(1589);
  ui->farVerticalSlider->setValue(1903);
  ui->saturationDoubleSpinBox->setValue(1.3);
  ui->gammaDoubleSpinBox->setValue(1.4);
  ui->contrastDoubleSpinBox->setValue(1.1);
  startTimer(1000 / 25, Qt::PreciseTimer);
}


void MainWindow::timerEvent(QTimerEvent*)
{
  Q_D(MainWindow);
  bool depthReady = false;
  bool rgbReady = false;
  bool irReady = false;
  INT64 timestamp = 0;
  UINT16 *depthBuffer = nullptr;
  UINT16 *irBuffer = nullptr;
  USHORT minDistance = 0;
  USHORT maxDistance = 0;
  int width = 0;
  int weight = 0;
  UINT bufferSize = 0;

  IDepthFrame *depthFrame = nullptr;
  if (d->depthFrameReader != nullptr) {
    HRESULT hr = d->depthFrameReader->AcquireLatestFrame(&depthFrame);
    if (SUCCEEDED(hr)) {
      IFrameDescription *depthFrameDescription = nullptr;
      hr = depthFrame->get_RelativeTime(&timestamp);
      if (SUCCEEDED(hr))
        hr = depthFrame->get_FrameDescription(&depthFrameDescription);
      if (SUCCEEDED(hr))
        hr = depthFrameDescription->get_Width(&width);
      if (SUCCEEDED(hr))
        hr = depthFrameDescription->get_Height(&weight);
      if (SUCCEEDED(hr))
        hr = depthFrame->get_DepthMinReliableDistance(&minDistance);
      if (SUCCEEDED(hr)) {
        maxDistance = USHRT_MAX;
        hr = depthFrame->get_DepthMaxReliableDistance(&maxDistance);
      }
      if (SUCCEEDED(hr))
        hr = depthFrame->AccessUnderlyingBuffer(&bufferSize, &depthBuffer);
      if (SUCCEEDED(hr)) {
        d->depthWidget->setDepthData(timestamp, depthBuffer, width, weight, minDistance, maxDistance);
        d->rgbdWidget->setDepthData(timestamp, depthBuffer, width, weight, minDistance, maxDistance);
        depthReady = true;
      }
      SafeRelease(depthFrameDescription);
    }
  }

  IInfraredFrame *irFrame = nullptr;
  if (d->irFrameReader != nullptr) {
    HRESULT hr = d->irFrameReader->AcquireLatestFrame(&irFrame);
    if (SUCCEEDED(hr)) {
      IFrameDescription *irFrameDescription = nullptr;
      hr = irFrame->get_RelativeTime(&timestamp);
      if (SUCCEEDED(hr))
        hr = irFrame->get_FrameDescription(&irFrameDescription);
      if (SUCCEEDED(hr))
        hr = irFrameDescription->get_Width(&width);
      if (SUCCEEDED(hr))
        hr = irFrameDescription->get_Height(&weight);
      if (SUCCEEDED(hr))
        hr = irFrame->AccessUnderlyingBuffer(&bufferSize, &irBuffer);
      if (SUCCEEDED(hr)) {
        d->irWidget->setIRData(timestamp, irBuffer, width, weight);
        irReady = true;
      }
      SafeRelease(irFrameDescription);
    }
  }

  IColorFrame* colorFrame = nullptr;
  if (d->colorFrameReader != nullptr) {
    HRESULT hr = d->colorFrameReader->AcquireLatestFrame(&colorFrame);
    if (SUCCEEDED(hr)) {
      IFrameDescription *colorFrameDescription = nullptr;
      ColorImageFormat imageFormat = ColorImageFormat_None;
      hr = colorFrame->get_RelativeTime(&timestamp);
      if (SUCCEEDED(hr))
        hr = colorFrame->get_FrameDescription(&colorFrameDescription);
      qDebug() << colorFrameDescription;
      if (SUCCEEDED(hr))
        hr = colorFrameDescription->get_Width(&width);
      if (SUCCEEDED(hr))
        hr = colorFrameDescription->get_Height(&weight);
      if (SUCCEEDED(hr))
        hr = colorFrame->get_RawColorImageFormat(&imageFormat);
      if (SUCCEEDED(hr)) {
        if (imageFormat == ColorImageFormat_Bgra) {
          hr = colorFrame->AccessRawUnderlyingBuffer(&bufferSize, reinterpret_cast<BYTE**>(&d->colorBuffer));
        }
        else if (d->colorBuffer != nullptr) { // regular case: imageFormat == ColorImageFormat_Yuy2
          bufferSize = ColorSize * sizeof(RGBQUAD);
          hr = colorFrame->CopyConvertedFrameDataToArray(bufferSize, reinterpret_cast<BYTE*>(d->colorBuffer), ColorImageFormat_Bgra);
        }
        else {
          hr = E_FAIL;
        }
      }
      if (SUCCEEDED(hr)) {
        d->videoWidget->setVideoData(timestamp, reinterpret_cast<const QRgb*>(d->colorBuffer), width, weight);
        d->rgbdWidget->setColorData(timestamp, reinterpret_cast<const QRgb*>(d->colorBuffer), width, weight);
        rgbReady = true;
      }
      SafeRelease(colorFrameDescription);
    }
  }

  if (rgbReady && depthReady && irReady)
    d->threeDWidget->process(timestamp, reinterpret_cast<const uchar*>(d->colorBuffer), depthBuffer, minDistance, maxDistance);

  SafeRelease(depthFrame);
  SafeRelease(colorFrame);
  SafeRelease(irFrame);
}


bool MainWindow::initKinect(void)
{
  Q_D(MainWindow);

  qDebug() << "MainWindow::initKinect()";

  HRESULT hr;

  hr = GetDefaultKinectSensor(&d->kinectSensor);
  if (FAILED(hr))
    return false;

  if (d->kinectSensor != nullptr) {
    IDepthFrameSource *pDepthFrameSource = nullptr;
    hr = d->kinectSensor->Open();

    if (SUCCEEDED(hr))
      hr = d->kinectSensor->get_DepthFrameSource(&pDepthFrameSource);
    if (SUCCEEDED(hr))
      hr = pDepthFrameSource->OpenReader(&d->depthFrameReader);
    SafeRelease(pDepthFrameSource);

    IColorFrameSource *pColorFrameSource = nullptr;
    if (SUCCEEDED(hr))
      hr = d->kinectSensor->get_ColorFrameSource(&pColorFrameSource);
    if (SUCCEEDED(hr))
      hr = pColorFrameSource->OpenReader(&d->colorFrameReader);
    SafeRelease(pColorFrameSource);

    IInfraredFrameSource *pIRFrameSource = nullptr;
    if (SUCCEEDED(hr))
      hr = d->kinectSensor->get_InfraredFrameSource(&pIRFrameSource);
    if (SUCCEEDED(hr))
      hr = pIRFrameSource->OpenReader(&d->irFrameReader);
    SafeRelease(pIRFrameSource);
  }

  if (!d->kinectSensor || FAILED(hr)) {
    qWarning() << "No ready Kinect found!";
    return false;
  }

  return true;
}


void MainWindow::contrastChanged(double contrast)
{
  Q_D(MainWindow);
  d->threeDWidget->setContrast(GLfloat(contrast));
}


void MainWindow::gammaChanged(double gamma)
{
  Q_D(MainWindow);
  d->threeDWidget->setGamma(GLfloat(gamma));
}


void MainWindow::saturationChanged(double saturation)
{
  Q_D(MainWindow);
  d->threeDWidget->setSaturation(GLfloat(saturation));
}


void MainWindow::setNearThreshold(int value)
{
  Q_D(MainWindow);
  if (value < ui->farVerticalSlider->value()) {
    d->rgbdWidget->setNearThreshold(value);
    d->threeDWidget->setNearThreshold(GLfloat(value));
  }
  else {
    ui->farVerticalSlider->setValue(value);
  }
}


void MainWindow::setFarThreshold(int value)
{
  Q_D(MainWindow);
  if (value > ui->nearVerticalSlider->value()) {
    d->rgbdWidget->setFarThreshold(value);
    d->threeDWidget->setFarThreshold(GLfloat(value));
  }
  else {
    ui->nearVerticalSlider->setValue(value);
  }
}

//
// Created by rika on 06/12/2019.
//

#ifndef SPEECH_ANALYSIS_MAINWINDOW_H
#define SPEECH_ANALYSIS_MAINWINDOW_H

#include "../audio/miniaudio.h"
#include <QtWidgets>
#include <QSharedPointer>
#include <utility>
#include "../audio/AudioDevices.h"
#include "AnalyserCanvas.h"

using DevicePair = std::pair<bool, const ma_device_id *>;

Q_DECLARE_METATYPE(DevicePair);

extern QFont * appFont;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent * event) override;
    void keyPressEvent(QKeyEvent * event) override;

private:
    void loadSettings();

    void updateDevices();
    void updateFields();

    void toggleAnalyser();

    ma_context maCtx;
    AudioDevices * devs;
    Analyser * analyser;

    QTimer timer;

    QWidget * central;
    QVBoxLayout * vLayout1;
    QHBoxLayout * hLayout2;
    QHBoxLayout * hLayout3;
    QFormLayout * fLayout4;
    QHBoxLayout * hLayout5;
    QVBoxLayout * vLayout6;

    QDockWidget * fieldsDock;
    QDockWidget * settingsDock;

    QComboBox * inputDevIn;
    QComboBox * inputDevOut;
    QPushButton * inputDevRefresh;

    QPushButton * inputPause;

    QCheckBox * inputToggleSpectrum;
    QComboBox * inputFftSize;

    QSpinBox * inputLpOrder;
    QSpinBox * inputMaxFreq;

    QComboBox * inputFreqScale;
    QSpinBox * inputFrameSpace;
    QDoubleSpinBox * inputWindowSpan;

    std::array<QPushButton *, 4> inputFormantColor;
    QSpinBox * inputMinGain;
    QSpinBox * inputMaxGain;

    QComboBox * inputPitchAlg;
    QComboBox * inputFormantAlg;

    AnalyserCanvas * canvas;

    QLineEdit * fieldPitch;
    std::vector<QLineEdit *> fieldFormant;

};

#endif //SPEECH_ANALYSIS_MAINWINDOW_H

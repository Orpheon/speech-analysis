//
// Created by rika on 16/11/2019.
//

#include <QSettings>
#include "Analyser.h"
#include "../log/simpleQtLogger.h"

using namespace Eigen;

static const Formant::Frame defaultFrame = {
    .nFormants = 5,
    .formant = {{550, 60}, {1650, 60}, {2750, 60}, {3850, 60}, {4950, 60}},
    .intensity = 1.0,
};

static const SpecFrame defaultSpec = {
    .fs = 0,
    .nfft = 512,
    .spec = ArrayXd::Zero(512),
};

Analyser::Analyser(ma_context * ctx)
    : audioCapture(new AudioCapture(ctx)),
      doAnalyse(true),
      running(false),
      lpFailed(true),
      nbNewFrames(0),
      frameCount(0)
{
    loadSettings();

    // Initialize the audio frames to zero.
    x.setZero(nfft);

    setInputDevice(nullptr);
}

Analyser::~Analyser() {
    delete audioCapture;
    saveSettings();
}

void Analyser::startThread() {
    running.store(true);
    thread = std::thread(&Analyser::mainLoop, this);
}

void Analyser::stopThread() {
    running.store(false);
    thread.join();
}

void Analyser::toggle() {
    std::lock_guard<std::mutex> lock(mutex);
    doAnalyse = !doAnalyse;
}

bool Analyser::isAnalysing() {
    std::lock_guard<std::mutex> lock(mutex);
    return doAnalyse;
}

void Analyser::setInputDevice(const ma_device_id * id) {
    std::lock_guard<std::mutex> guard(audioLock);
    audioCapture->closeStream();
    audioCapture->openInputDevice(id);
    fs = audioCapture->getSampleRate();
    x.setZero(CAPTURE_SAMPLE_COUNT(fs));
    audioCapture->startStream();
}

void Analyser::setOutputDevice(const ma_device_id * id) {
    std::lock_guard<std::mutex> guard(audioLock);
    audioCapture->closeStream();
    audioCapture->openOutputDevice(id);
    fs = audioCapture->getSampleRate();
    x.setZero(CAPTURE_SAMPLE_COUNT(fs));
    audioCapture->startStream();
}

void Analyser::setFftSize(int _nfft) {
    std::lock_guard<std::mutex> lock(mutex);
    nfft = _nfft;
    LS_INFO("Set FFT size to " << nfft);
}

int Analyser::getFftSize() {
    std::lock_guard<std::mutex> lock(mutex);
    return nfft;
}

void Analyser::setLinearPredictionOrder(int _lpOrder) {
    std::lock_guard<std::mutex> lock(mutex);
    lpOrder = std::clamp(_lpOrder, 5, 22);
    LS_INFO("Set LP order to " << lpOrder);
}

int Analyser::getLinearPredictionOrder() {
    std::lock_guard<std::mutex> lock(mutex);
    return lpOrder;
}

void Analyser::setCepstralOrder(int _cepOrder) {
    std::lock_guard<std::mutex> lock(mutex);
    
    cepOrder = std::clamp(_cepOrder, 7, 25);
    _initEkfState();
    
    LS_INFO("Set LPCC order to " << cepOrder);
}

int Analyser::getCepstralOrder() {
    std::lock_guard<std::mutex> lock(mutex);
    return cepOrder;
}

void Analyser::setMaximumFrequency(double _maximumFrequency) {
    std::lock_guard<std::mutex> lock(mutex);
    maximumFrequency = std::clamp(_maximumFrequency, 2500.0, 7000.0);
    
    LS_INFO("Set maximum frequency to " << maximumFrequency);
}

double Analyser::getMaximumFrequency() {
    std::lock_guard<std::mutex> lock(mutex);
    return maximumFrequency;
}

void Analyser::setFrameSpace(const std::chrono::duration<double, std::milli> & _frameSpace) {
    std::lock_guard<std::mutex> lock(mutex);
    frameSpace = _frameSpace;
    LS_INFO("Set frame space to " << frameSpace.count() << " ms");
    
    _updateFrameCount();
}

const std::chrono::duration<double, std::milli> & Analyser::getFrameSpace() {
    std::lock_guard<std::mutex> lock(mutex);
    return frameSpace;
}

void Analyser::setWindowSpan(const std::chrono::duration<double> & _windowSpan) {
    std::lock_guard<std::mutex> lock(mutex);
    windowSpan = _windowSpan;
    LS_INFO("Set window span to " << windowSpan.count() << " ms");
    
    _updateFrameCount();
}

const std::chrono::duration<double> & Analyser::getWindowSpan() {
    std::lock_guard<std::mutex> lock(mutex);
    return windowSpan;
}

void Analyser::setPitchAlgorithm(enum PitchAlg _pitchAlg) {
    std::lock_guard<std::mutex> lock(mutex);
    pitchAlg = _pitchAlg;

    switch (pitchAlg) {
        case Wavelet:
            L_INFO("Set pitch algorithm to DynamicWavelet");
            break;
        case McLeod:
            L_INFO("Set pitch algorithm to McLeod");
            break;
        case YIN:
            L_INFO("Set pitch algorithm to Yin");
            break;
        case AMDF:
            L_INFO("Set pitch algorithm to AMDF");
            break;
    }
}

PitchAlg Analyser::getPitchAlgorithm() {
    std::lock_guard<std::mutex> lock(mutex);
    return pitchAlg;
}

void Analyser::setFormantMethod(enum FormantMethod _method) {
    std::lock_guard<std::mutex> lock(mutex);
    formantMethod = _method;

    switch (formantMethod) {
        case LP:
            L_INFO("Set formant algorithm to Linear Prediction");
            break;
        case KARMA:
            L_INFO("Set formant algorithm to KARMA");
            break;
    }
}

FormantMethod Analyser::getFormantMethod() {
    std::lock_guard<std::mutex> lock(mutex);
    return formantMethod;
}

int Analyser::getFrameCount() {
    std::lock_guard<std::mutex> lock(mutex);

    return frameCount;
}

const SpecFrame & Analyser::getSpectrumFrame(int _iframe) {
    std::lock_guard<std::mutex> lock(mutex);

    int iframe = std::clamp(_iframe, 0, frameCount - 1);
    return spectra.at(iframe);
}

const SpecFrame & Analyser::getLastSpectrumFrame() {
    std::lock_guard<std::mutex> lock(mutex);

    return spectra.back();
}

const Formant::Frame & Analyser::getFormantFrame(int _iframe) {
    std::lock_guard<std::mutex> lock(mutex);

    int iframe = std::clamp(_iframe, 0, frameCount - 1);
    return smoothedFormants.at(iframe);
}

const Formant::Frame & Analyser::getLastFormantFrame() {
    std::lock_guard<std::mutex> lock(mutex);

    return formantTrack.back();
}

double Analyser::getPitchFrame(int _iframe) {
    std::lock_guard<std::mutex> lock(mutex);

    int iframe = std::clamp(_iframe, 0, frameCount - 1);
    return smoothedPitch.at(iframe);
}

double Analyser::getLastPitchFrame() {
    std::lock_guard<std::mutex> lock(mutex);

    return pitchTrack.back();
}

void Analyser::_updateFrameCount() {
    const int newFrameCount = (1000 * windowSpan.count()) / frameSpace.count();

    if (frameCount < newFrameCount) {
        int diff = newFrameCount - frameCount;

        pitchTrack.insert(pitchTrack.begin(), diff, 0.0);
        formantTrack.insert(formantTrack.begin(), diff, defaultFrame);

        spectra.insert(spectra.begin(), diff, defaultSpec);
        smoothedPitch.insert(smoothedPitch.begin(), diff, 0.0);
        smoothedFormants.insert(smoothedFormants.begin(), diff, defaultFrame);
    }
    else if (frameCount > newFrameCount) {
        int diff = frameCount - newFrameCount;

        pitchTrack.erase(pitchTrack.begin(), pitchTrack.begin() + diff);
        formantTrack.erase(formantTrack.begin(), formantTrack.begin() + diff);
        
        spectra.erase(spectra.begin(), spectra.begin() + diff);
        smoothedPitch.erase(smoothedPitch.begin(), smoothedPitch.begin() + diff);
        smoothedFormants.erase(smoothedFormants.begin(), smoothedFormants.begin() + diff);
    }

    LS_INFO("Resized tracks from " << frameCount << " to " << newFrameCount);

    frameCount = newFrameCount;
}

void Analyser::_initEkfState()
{
    int numF = 4;

    VectorXd x0(2 * numF);
    x0.setZero();

    // Average over the last 20 frames.
    
    for (int i = 1; i <= 20; ++i) {
        VectorXd x(2 * numF);
        
        auto & frm = smoothedFormants[frameCount - i];

        for (int k = 0; k < std::min(frm.nFormants, numF); ++k) {
            x(k) = frm.formant[k].frequency;
            x(numF + k) = frm.formant[k].bandwidth;
        }
        
        x0 += x;
    }

    x0 /= 20.0;

    ekfState.cepOrder = this->cepOrder;

    EKF::init(ekfState, x0);
}

void Analyser::loadSettings()
{
    QSettings settings;

    L_INFO("Loading analysis settings...");

    settings.beginGroup("analysis");

    setFftSize(settings.value("fftSize", 512).value<int>());
    setLinearPredictionOrder(settings.value("lpOrder", 12).value<int>());
    setMaximumFrequency(settings.value("maxFreq", 4700.0).value<double>());
    setFrameSpace(std::chrono::milliseconds(settings.value("frameSpace", 15).value<int>()));
    setWindowSpan(std::chrono::milliseconds(int(1000 * settings.value("windowSpan", 5.0).value<double>())));
    setPitchAlgorithm((PitchAlg) settings.value("pitchAlg", static_cast<int>(Wavelet)).value<int>());
    setFormantMethod((FormantMethod) settings.value("formantMethod", static_cast<int>(KARMA)).value<int>());
    setCepstralOrder(settings.value("cepOrder", 15).value<int>());

    settings.endGroup();
}

void Analyser::saveSettings()
{
    QSettings settings;

    L_INFO("Saving analysis settings...");

    settings.beginGroup("analysis");

    settings.setValue("fftSize", nfft);
    settings.setValue("lpOrder", lpOrder);
    settings.setValue("maxFreq", maximumFrequency);
    settings.setValue("cepOrder", cepOrder);
    settings.setValue("frameSpace", frameSpace.count());
    settings.setValue("windowSpan", windowSpan.count());
    settings.setValue("pitchAlg", static_cast<int>(pitchAlg));
    settings.setValue("formantMethod", static_cast<int>(formantMethod));

    settings.endGroup();

}

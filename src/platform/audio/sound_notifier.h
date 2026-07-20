#pragma once

#include <QByteArray>

class QWindow;
struct cs_audio_source_t;

class SoundNotifier {
public:
  SoundNotifier();
  ~SoundNotifier();

  bool initialize(QWindow *window);
  void playMessageAlert();
  bool isInitialized() const { return initialized_; }

private:
  bool loadAlertSample();

  // Loudness multiplier for the reminder wav (1.0 = original amplitude).
  float alertVolume_ = 2.0f;
  bool initialized_ = false;
  QByteArray alertBuffer_;
  cs_audio_source_t *alertAudio_ = nullptr;
};

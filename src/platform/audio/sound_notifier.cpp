#include "sound_notifier.h"

#include <QDebug>
#include <QFile>
#include <QResource>
#include <QWindow>

#ifdef slots
#define CUTE_SOUND_RESTORE_SLOTS
#undef slots
#endif
#define CUTE_SOUND_SCALAR_MODE
#define CUTE_SOUND_IMPLEMENTATION
#include "cute_sound.h"
#ifdef CUTE_SOUND_RESTORE_SLOTS
#define slots Q_SLOTS
#undef CUTE_SOUND_RESTORE_SLOTS
#endif

SoundNotifier::SoundNotifier() = default;

SoundNotifier::~SoundNotifier() {
  if (alertAudio_) {
    cs_free_audio_source(alertAudio_);
    alertAudio_ = nullptr;
  }
  if (initialized_) {
    cs_shutdown();
    initialized_ = false;
  }
  alertBuffer_.clear();
}

bool SoundNotifier::initialize(QWindow *window) {
  if (initialized_) {
    return true;
  }

#ifdef Q_OS_WIN
  if (!window) {
    qWarning() << "[Sound] Window handle is required on Windows.";
    return false;
  }
  void *nativeHandle = reinterpret_cast<void *>(window->winId());
#else
  Q_UNUSED(window);
  void *nativeHandle = nullptr;
#endif

  const cs_error_t err = cs_init(nativeHandle, 44100, 4096, nullptr);
  if (err != CUTE_SOUND_ERROR_NONE) {
    qWarning() << "[Sound] Init failed:" << cs_error_as_string(err);
    return false;
  }

  cs_spawn_mix_thread();
  cs_mix_thread_sleep_delay(2);

  if (!loadAlertSample()) {
    cs_shutdown();
    return false;
  }

  initialized_ = true;
  return true;
}

bool SoundNotifier::loadAlertSample() {
  const QStringList candidates = {
      QStringLiteral(":/qt/qml/ConnectTool/notify.wav"),
      QStringLiteral(":/ConnectTool/notify.wav"),
  };

  alertBuffer_.clear();
  for (const auto &path : candidates) {
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QByteArray data = f.readAll();
    if (!data.isEmpty()) {
      alertBuffer_ = data;
      break;
    }
  }

  if (alertBuffer_.isEmpty()) {
    qWarning() << "[Sound] Reminder wav missing from resources.";
    return false;
  }

  cs_error_t err = CUTE_SOUND_ERROR_NONE;
  alertAudio_ = cs_read_mem_wav(alertBuffer_.constData(),
                                static_cast<size_t>(alertBuffer_.size()), &err);
  if (!alertAudio_) {
    qWarning() << "[Sound] Failed to load reminder wav:"
               << cs_error_as_string(err);
    return false;
  }

  return true;
}

void SoundNotifier::playMessageAlert() {
  if (!initialized_ || !alertAudio_) {
    return;
  }

  cs_sound_params_t params = cs_sound_params_default();
  params.volume = alertVolume_;
  cs_play_sound(alertAudio_, params);
}

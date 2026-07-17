#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

class QNetworkReply;

class UpdateController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
  Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY infoChanged)
  Q_PROPERTY(QString releasePage READ releasePage NOTIFY infoChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY infoChanged)
  Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY infoChanged)
  Q_PROPERTY(bool checking READ checking NOTIFY infoChanged)
  Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged)
  Q_PROPERTY(double progress READ progress NOTIFY downloadChanged)
  Q_PROPERTY(QString savedPath READ savedPath NOTIFY downloadChanged)

public:
  explicit UpdateController(QString currentVersion, QObject *parent = nullptr);
  ~UpdateController() override;

  [[nodiscard]] QString currentVersion() const { return currentVersion_; }
  [[nodiscard]] QString latestVersion() const { return latestVersion_; }
  [[nodiscard]] QString releasePage() const { return releasePage_; }
  [[nodiscard]] QString statusText() const { return statusText_; }
  [[nodiscard]] bool updateAvailable() const { return updateAvailable_; }
  [[nodiscard]] bool checking() const { return checking_; }
  [[nodiscard]] bool downloading() const { return downloading_; }
  [[nodiscard]] double progress() const { return progress_; }
  [[nodiscard]] QString savedPath() const { return savedPath_; }

  Q_INVOKABLE void check(bool useProxy = false);
  Q_INVOKABLE void download(bool useProxy, const QString &targetPath);

  [[nodiscard]] static bool isVersionNewer(const QString &candidate, const QString &current);
  [[nodiscard]] static QString normalizeVersion(const QString &input);

signals:
  void infoChanged();
  void downloadChanged();

private:
  struct DownloadRequest {
    bool useProxy = false;
    QString targetPath;
  };

  void startDownload(DownloadRequest request);
  void handleCheckFinished();
  void handleDownloadFinished();
  void resetCheck();
  void resetDownload();
  [[nodiscard]] QString preferredDownloadUrl(bool useProxy) const;

  QString currentVersion_;
  QString latestVersion_;
  QString latestDownloadUrl_;
  QString releasePage_;
  QString statusText_;
  QString savedPath_;
  QString downloadDirectory_;
  QString requestedTarget_;
  bool updateAvailable_ = false;
  bool checking_ = false;
  bool downloading_ = false;
  double progress_ = 0.0;

  QNetworkAccessManager network_;
  QPointer<QNetworkReply> checkReply_;
  QPointer<QNetworkReply> downloadReply_;
};

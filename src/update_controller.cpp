#include "update_controller.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <array>

namespace {

constexpr auto kReleaseApi = "https://api.github.com/repos/moeleak/connecttool-qt/releases/latest";
constexpr auto kProxyPrefix = "https://gh-proxy.org/";

QString withoutProxy(const QString &url) {
  const QString prefix = QString::fromLatin1(kProxyPrefix);
  return url.startsWith(prefix) ? url.sliced(prefix.size()) : url;
}

} // namespace

UpdateController::UpdateController(QString currentVersion, QObject *parent)
    : QObject(parent), currentVersion_(std::move(currentVersion)) {}

UpdateController::~UpdateController() {
  if (checkReply_) {
    checkReply_->abort();
  }
  if (downloadReply_) {
    downloadReply_->abort();
  }
}

void UpdateController::check(bool useProxy) {
  if (checking_) {
    return;
  }

  resetCheck();
  statusText_ = tr("正在检查更新…");
  checking_ = true;
  emit infoChanged();

  const QString endpoint = QString::fromLatin1(kReleaseApi);
  const QUrl url(useProxy ? QString::fromLatin1(kProxyPrefix) + endpoint : endpoint);
  QNetworkRequest request{url};
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("connecttool-qt"));
  checkReply_ = network_.get(request);
  connect(checkReply_, &QNetworkReply::finished, this, &UpdateController::handleCheckFinished);
}

void UpdateController::download(bool useProxy, const QString &targetPath) {
  startDownload(DownloadRequest{.useProxy = useProxy, .targetPath = targetPath});
}

void UpdateController::startDownload(DownloadRequest request) {
  if (downloading_) {
    return;
  }
  if (latestDownloadUrl_.isEmpty()) {
    statusText_ = tr("没有可用的下载链接，请先检查更新。");
    emit infoChanged();
    return;
  }

  QString requestedPath = request.targetPath.trimmed();
  const QUrl parsed = QUrl::fromUserInput(requestedPath);
  if (parsed.isLocalFile() && !parsed.toLocalFile().isEmpty()) {
    requestedPath = parsed.toLocalFile();
  }

#ifdef Q_OS_WIN
  if (requestedPath.startsWith('/') && requestedPath.size() > 2 && requestedPath[1].isLetter() &&
      requestedPath[2] == QLatin1Char(':')) {
    requestedPath.remove(0, 1);
  }
#endif

  requestedPath = QDir::fromNativeSeparators(requestedPath);
  if (requestedPath.isEmpty()) {
    statusText_ = tr("请选择下载目录。");
    emit infoChanged();
    return;
  }

  const QFileInfo targetInfo(requestedPath);
  const bool endsWithSlash = requestedPath.endsWith('/') || requestedPath.endsWith('\\');
  QString chosenDirectory;
  if (!endsWithSlash && (!targetInfo.exists() || targetInfo.isFile())) {
    chosenDirectory = targetInfo.dir().absolutePath();
  }
  if (chosenDirectory.isEmpty()) {
    chosenDirectory = targetInfo.absoluteFilePath();
  }

  QDir directory(chosenDirectory);
  if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
    statusText_ = tr("无法创建下载目录：%1").arg(chosenDirectory);
    emit infoChanged();
    return;
  }

  const QString url = preferredDownloadUrl(request.useProxy);
  if (url.isEmpty()) {
    statusText_ = tr("下载链接无效。");
    emit infoChanged();
    return;
  }

  resetDownload();
  downloadDirectory_ = directory.absolutePath();
  requestedTarget_ = requestedPath;
  downloading_ = true;
  statusText_ = tr("正在下载更新…");
  emit infoChanged();
  emit downloadChanged();

  QNetworkRequest networkRequest{QUrl(url)};
  networkRequest.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("connecttool-qt"));
  downloadReply_ = network_.get(networkRequest);
  connect(downloadReply_, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64 total) {
            progress_ =
                total > 0 ? static_cast<double>(received) / static_cast<double>(total) : 0.0;
            emit downloadChanged();
          });
  connect(downloadReply_, &QNetworkReply::finished, this,
          &UpdateController::handleDownloadFinished);
}

void UpdateController::handleCheckFinished() {
  checking_ = false;
  const QPointer<QNetworkReply> reply = checkReply_;
  checkReply_.clear();

  if (!reply) {
    statusText_ = tr("检查失败。");
    updateAvailable_ = false;
    latestDownloadUrl_.clear();
    releasePage_.clear();
    emit infoChanged();
    return;
  }

  const QByteArray payload = reply->readAll();
  const auto error = reply->error();
  const QString errorText = reply->errorString();
  reply->deleteLater();
  if (error != QNetworkReply::NoError) {
    statusText_ = tr("检查失败：%1").arg(errorText);
    updateAvailable_ = false;
    latestDownloadUrl_.clear();
    releasePage_.clear();
    emit infoChanged();
    return;
  }

  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    statusText_ = tr("更新信息解析失败。");
    updateAvailable_ = false;
    latestDownloadUrl_.clear();
    releasePage_.clear();
    emit infoChanged();
    return;
  }

  const QJsonObject release = document.object();
  const QString tag = release.value(QStringLiteral("tag_name")).toString();
  releasePage_ = withoutProxy(release.value(QStringLiteral("html_url")).toString());
  latestDownloadUrl_.clear();
  for (const QJsonValue assetValue : release.value(QStringLiteral("assets")).toArray()) {
    if (!assetValue.isObject()) {
      continue;
    }
    const QString candidate = withoutProxy(
        assetValue.toObject().value(QStringLiteral("browser_download_url")).toString());
    if (!candidate.isEmpty()) {
      latestDownloadUrl_ = candidate;
      break;
    }
  }

  latestVersion_ = normalizeVersion(tag.isEmpty() ? QStringLiteral("0.0.0") : tag);
  updateAvailable_ = isVersionNewer(latestVersion_, normalizeVersion(currentVersion_));

  if (updateAvailable_) {
    statusText_ = latestDownloadUrl_.isEmpty()
                      ? tr("发现新版本 %1，暂未找到下载链接。").arg(latestVersion_)
                      : tr("发现新版本 %1，可下载更新。").arg(latestVersion_);
  } else {
    statusText_ = tr("当前已是最新版本（%1）。").arg(currentVersion_);
  }
  emit infoChanged();
}

void UpdateController::handleDownloadFinished() {
  const QPointer<QNetworkReply> reply = downloadReply_;
  downloadReply_.clear();
  downloading_ = false;

  if (!reply) {
    statusText_ = tr("下载已取消。");
    emit infoChanged();
    emit downloadChanged();
    return;
  }

  const auto error = reply->error();
  const QString errorText = reply->errorString();
  const QByteArray data = reply->readAll();
  const QUrl finalUrl = reply->url();
  reply->deleteLater();
  if (error != QNetworkReply::NoError) {
    statusText_ = tr("下载失败：%1").arg(errorText);
    emit infoChanged();
    emit downloadChanged();
    return;
  }

  const QFileInfo requested(requestedTarget_.isEmpty() ? downloadDirectory_ : requestedTarget_);
  QString fileName = QFileInfo(finalUrl.path()).fileName();
  if (fileName.isEmpty()) {
    fileName = latestVersion_.isEmpty()
                   ? QStringLiteral("connecttool-qt-release.bin")
                   : QStringLiteral("connecttool-qt-%1.zip").arg(latestVersion_);
  }

  QString targetDirectory = downloadDirectory_;
  if (targetDirectory.isEmpty()) {
    targetDirectory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  }
  if (requested.exists() && requested.isFile()) {
    targetDirectory = requested.dir().absolutePath();
    fileName = requested.fileName();
  } else if (!requested.exists() && !requested.fileName().isEmpty() &&
             !downloadDirectory_.endsWith('/') && !downloadDirectory_.endsWith('\\')) {
    targetDirectory = requested.dir().absolutePath();
    fileName = requested.fileName();
  }
  if (!fileName.contains('.')) {
    fileName.append(QStringLiteral(".zip"));
  }

  const QString targetPath = QDir(targetDirectory).filePath(fileName);
  QSaveFile file(targetPath);
  if (!file.open(QIODevice::WriteOnly)) {
    statusText_ = tr("保存文件失败：%1").arg(file.errorString());
    emit infoChanged();
    emit downloadChanged();
    return;
  }
  if (file.write(data) != data.size() || !file.commit()) {
    statusText_ = tr("保存文件失败：%1").arg(file.errorString());
    emit infoChanged();
    emit downloadChanged();
    return;
  }

  progress_ = 1.0;
  savedPath_ = targetPath;
  statusText_ = tr("已下载到 %1").arg(savedPath_);
  emit infoChanged();
  emit downloadChanged();
}

void UpdateController::resetCheck() {
  if (checkReply_) {
    checkReply_->disconnect(this);
    checkReply_->deleteLater();
    checkReply_.clear();
  }
  checking_ = false;
}

void UpdateController::resetDownload() {
  if (downloadReply_) {
    downloadReply_->disconnect(this);
    downloadReply_->deleteLater();
    downloadReply_.clear();
  }
  downloading_ = false;
  progress_ = 0.0;
  savedPath_.clear();
  downloadDirectory_.clear();
  requestedTarget_.clear();
}

bool UpdateController::isVersionNewer(const QString &candidate, const QString &current) {
  const auto parse = [](const QString &version) {
    QStringList parts = version.split('.');
    while (parts.size() < 3) {
      parts.append(QStringLiteral("0"));
    }
    std::array<int, 3> values{};
    for (qsizetype index = 0; index < 3 && index < parts.size(); ++index) {
      bool valid = false;
      const int value = parts[index].toInt(&valid);
      values[static_cast<std::size_t>(index)] = valid ? value : 0;
    }
    return values;
  };

  return parse(candidate) > parse(current);
}

QString UpdateController::normalizeVersion(const QString &input) {
  QString version = input.trimmed();
  if (version.startsWith('v', Qt::CaseInsensitive)) {
    version.remove(0, 1);
  }
  return version.isEmpty() ? QStringLiteral("0.0.0") : version;
}

QString UpdateController::preferredDownloadUrl(bool useProxy) const {
  if (latestDownloadUrl_.isEmpty()) {
    return {};
  }
  const QString baseUrl = withoutProxy(latestDownloadUrl_);
  return useProxy ? QString::fromLatin1(kProxyPrefix) + baseUrl : baseUrl;
}

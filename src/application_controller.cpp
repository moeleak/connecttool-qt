#include "application_controller.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QThread>

SessionController::SessionController(Backend &backend) : backend_(backend) {
  connect(&backend_, &Backend::stateChanged, this, &SessionController::changed);
  connect(&backend_, &Backend::joinTargetChanged, this, &SessionController::changed);
  connect(&backend_, &Backend::localPortChanged, this, &SessionController::changed);
  connect(&backend_, &Backend::localBindPortChanged, this, &SessionController::changed);
  connect(&backend_, &Backend::serverChanged, this, &SessionController::changed);
  connect(&backend_, &Backend::hostSteamIdChanged, this, &SessionController::changed);
  connect(&backend_, &Backend::roomNameChanged, this, &SessionController::changed);
  connect(&backend_, &Backend::publishLobbyChanged, this, &SessionController::changed);
  connect(&backend_, &Backend::adminPrivilegesRequired, this,
          &SessionController::adminPrivilegesRequired);
  connect(&backend_, &Backend::tunStartDenied, this, &SessionController::tunStartDenied);
}

LobbyController::LobbyController(Backend &backend) : backend_(backend) {
  connect(&backend_, &Backend::lobbyRefreshingChanged, this, &LobbyController::changed);
  connect(&backend_, &Backend::lobbyFilterChanged, this, &LobbyController::changed);
  connect(&backend_, &Backend::lobbySortModeChanged, this, &LobbyController::changed);
}

SocialController::SocialController(Backend &backend) : backend_(backend) {
  connect(&backend_, &Backend::friendsRefreshingChanged, this, &SocialController::changed);
  connect(&backend_, &Backend::friendFilterChanged, this, &SocialController::changed);
  connect(&backend_, &Backend::inviteCooldownChanged, this, &SocialController::changed);
}

ChatController::ChatController(Backend &backend) : backend_(backend) {
  connect(&backend_, &Backend::chatReminderEnabledChanged, this, &ChatController::changed);
}

NetworkController::NetworkController(Backend &backend) : backend_(backend) {
  connect(&backend_, &Backend::relayPingChanged, this, &NetworkController::changed);
  connect(&backend_, &Backend::relayPopsChanged, this, &NetworkController::changed);
}

UpdaterController::UpdaterController(UpdateController &updater) : updater_(updater) {
  connect(&updater_, &UpdateController::infoChanged, this, &UpdaterController::changed);
  connect(&updater_, &UpdateController::downloadChanged, this, &UpdaterController::changed);
}

ApplicationController::ApplicationController(Backend &backend)
    : backend_(backend), session_(backend), lobbies_(backend), social_(backend), chat_(backend),
      network_(backend), updater_(*backend.updater()) {}

void ApplicationControllerRegistration::bind(ApplicationController &application) {
  Q_ASSERT(instance_ == nullptr || instance_ == &application);
  instance_ = &application;
}

ApplicationController *ApplicationControllerRegistration::create(QQmlEngine *engine,
                                                                 QJSEngine *scriptEngine) {
  Q_ASSERT(engine != nullptr);
  Q_ASSERT(scriptEngine != nullptr);
  Q_ASSERT(engine->thread() == scriptEngine->thread());
  Q_ASSERT(instance_ != nullptr);
  Q_ASSERT(instance_->thread() == engine->thread());
  QQmlEngine::setObjectOwnership(instance_, QQmlEngine::CppOwnership);
  return instance_;
}

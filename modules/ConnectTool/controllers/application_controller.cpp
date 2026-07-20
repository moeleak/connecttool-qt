#include "application_controller.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QThread>

SessionController::SessionController(ApplicationRuntime &runtime) : runtime_(runtime) {
  connect(&runtime_, &ApplicationRuntime::stateChanged, this, &SessionController::changed);
  connect(&runtime_, &ApplicationRuntime::joinTargetChanged, this, &SessionController::changed);
  connect(&runtime_, &ApplicationRuntime::localPortChanged, this, &SessionController::changed);
  connect(&runtime_, &ApplicationRuntime::localBindPortChanged, this, &SessionController::changed);
  connect(&runtime_, &ApplicationRuntime::serverChanged, this, &SessionController::changed);
  connect(&runtime_, &ApplicationRuntime::hostSteamIdChanged, this, &SessionController::changed);
  connect(&runtime_, &ApplicationRuntime::roomNameChanged, this, &SessionController::changed);
  connect(&runtime_, &ApplicationRuntime::publishLobbyChanged, this, &SessionController::changed);
  connect(&runtime_, &ApplicationRuntime::adminPrivilegesRequired, this,
          &SessionController::adminPrivilegesRequired);
  connect(&runtime_, &ApplicationRuntime::tunStartDenied, this, &SessionController::tunStartDenied);
}

LobbyController::LobbyController(ApplicationRuntime &runtime) : runtime_(runtime) {
  connect(&runtime_, &ApplicationRuntime::lobbyRefreshingChanged, this, &LobbyController::changed);
  connect(&runtime_, &ApplicationRuntime::lobbyFilterChanged, this, &LobbyController::changed);
  connect(&runtime_, &ApplicationRuntime::lobbySortModeChanged, this, &LobbyController::changed);
}

SocialController::SocialController(ApplicationRuntime &runtime) : runtime_(runtime) {
  connect(&runtime_, &ApplicationRuntime::friendsRefreshingChanged, this, &SocialController::changed);
  connect(&runtime_, &ApplicationRuntime::friendFilterChanged, this, &SocialController::changed);
  connect(&runtime_, &ApplicationRuntime::inviteCooldownChanged, this, &SocialController::changed);
}

ChatController::ChatController(ApplicationRuntime &runtime) : runtime_(runtime) {
  connect(&runtime_, &ApplicationRuntime::chatReminderEnabledChanged, this, &ChatController::changed);
}

NetworkController::NetworkController(ApplicationRuntime &runtime) : runtime_(runtime) {
  connect(&runtime_, &ApplicationRuntime::relayPingChanged, this, &NetworkController::changed);
  connect(&runtime_, &ApplicationRuntime::relayPopsChanged, this, &NetworkController::changed);
}

UpdaterController::UpdaterController(UpdateController &updater) : updater_(updater) {
  connect(&updater_, &UpdateController::infoChanged, this, &UpdaterController::changed);
  connect(&updater_, &UpdateController::downloadChanged, this, &UpdaterController::changed);
}

ApplicationController::ApplicationController(ApplicationRuntime &runtime)
    : runtime_(runtime), session_(runtime), lobbies_(runtime), social_(runtime), chat_(runtime),
      network_(runtime), updater_(*runtime.updater()) {}

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

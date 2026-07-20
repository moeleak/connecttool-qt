set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(connecttool_ui
    STATIC
    URI ConnectTool
    VERSION 1.0
    CLASS_NAME ConnectToolPlugin
    SOURCES
        controllers/application_controller.h
        controllers/application_controller.cpp
        models/chat_model.h
        models/friends_model.h
        models/lobbies_model.h
        models/members_model.h
    IMPORTS
        QtQuick.Effects
        QtQuick.Shapes
        QtQuick.Window
    DEPENDENCIES
        TARGET qml_material
    QML_FILES
        Main.qml
        Theme.qml
        NavigationDrawer.qml
        AppCard.qml
        LinkCard.qml
        StatusPill.qml
        Avatar.qml
        ConnectionPanel.qml
        PortField.qml
        ChatPanel.qml
        ChatMessageDelegate.qml
        PeoplePanel.qml
        MemberDelegate.qml
        FriendDelegate.qml
        RoomPage.qml
        LobbyPage.qml
        NetworkPage.qml
        AboutPage.qml
    RESOURCES
        GitHub.svg
        QQ.svg
        AppIcon.png
        notify.wav)

add_library(ConnectTool::Ui ALIAS connecttool_ui)

target_include_directories(connecttool_ui PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/modules
    ${CMAKE_CURRENT_SOURCE_DIR}/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/models)
target_link_libraries(connecttool_ui
    PUBLIC connecttool_application
    PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick qml_materialplugin)
target_compile_features(connecttool_ui PUBLIC cxx_std_23)

connecttool_enable_strict_warnings(connecttool_ui)

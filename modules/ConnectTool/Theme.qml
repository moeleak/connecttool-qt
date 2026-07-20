pragma Singleton

import QtQuick
import Qcm.Material as MD

QtObject {
    // QmlMaterial owns the generated Material 3 scheme. This singleton keeps
    // short semantic names for application-specific status colors and spacing.
    readonly property color background: MD.Token.color.surface
    readonly property color surface: MD.Token.color.surface_container_lowest
    readonly property color surfaceContainerLow: MD.Token.color.surface_container_low
    readonly property color surfaceContainer: MD.Token.color.surface_container
    readonly property color surfaceContainerHigh: MD.Token.color.surface_container_high
    readonly property color primary: MD.Token.color.primary
    readonly property color primaryInk: MD.Token.color.on_primary
    readonly property color primaryContainer: MD.Token.color.primary_container
    readonly property color primaryContainerInk: MD.Token.color.on_primary_container
    readonly property color secondary: MD.Token.color.tertiary
    readonly property color foreground: MD.Token.color.on_surface
    readonly property color foregroundMuted: MD.Token.color.on_surface_variant
    readonly property color outline: MD.Token.color.outline_variant
    readonly property color warning: "#ffcf70"
    readonly property color danger: MD.Token.color.error
    readonly property color success: "#67d99a"

    readonly property int pagePadding: 18
    readonly property int spacing: 14
    readonly property int compactSpacing: 8
    readonly property int sectionSpacing: 12

    function alpha(color, opacity) {
        return Qt.rgba(color.r, color.g, color.b, opacity)
    }
}

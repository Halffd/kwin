/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import org.kde.kwin 3.0 as KWin

Rectangle {
    id: root

    // Properties set from C++ only - no bindings
    property int windowCount: 0
    property int selectedIndex: 0
    property string selectedWindowTitle: ""

    color: Qt.rgba(0.1, 0.1, 0.1, 0.9)

    Rectangle {
        id: switcherContainer
        anchors.centerIn: parent
        width: parent.width * 0.8
        height: parent.height * 0.6
        color: Qt.rgba(0.2, 0.2, 0.2, 1.0)
        radius: 8

        // Grid layout for window thumbnails
        Grid {
            id: thumbnailGrid
            anchors.centerIn: parent
            rows: 1
            spacing: 10

            Repeater {
                model: root.windowCount

                Rectangle {
                    width: 200
                    height: 150
                    color: index === root.selectedIndex ? Qt.rgba(0.15, 0.35, 0.6, 1.0) : Qt.rgba(0.25, 0.25, 0.25, 1.0)
                    radius: 4

                    border.width: index === root.selectedIndex ? 3 : 1
                    border.color: index === root.selectedIndex ? Qt.rgba(0.4, 0.7, 1.0, 1.0) : Qt.rgba(0.5, 0.5, 0.5, 1.0)

                    Column {
                        anchors.fill: parent
                        anchors.margins: 5
                        spacing: 5

                        // Window thumbnail using KWin's built-in type
                        KWin.WindowThumbnail {
                            id: thumbnail
                            width: parent.width
                            height: parent.width * 0.56 // 16:9 aspect ratio
                            wId: effect.windowIdAt(index)
                        }

                        // Window title
                        Text {
                            width: parent.width
                            color: "white"
                            font.pixelSize: 12
                            text: index === root.selectedIndex ? root.selectedWindowTitle : ("Window " + (index + 1))
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }

        // Debug info
        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
            color: "yellow"
            font.pixelSize: 10
            text: "Windows: " + root.windowCount + " | Selected: " + root.selectedIndex
        }
    }
}

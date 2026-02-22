/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2026 Your Name <your.email@example.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

Rectangle {
    id: root

    // Properties set from C++ only - no bindings
    property int desktopCount: 0
    property int selectedIndex: 0
    property string selectedDesktopName: ""

    color: Qt.rgba(0.05, 0.05, 0.1, 0.95)

    Rectangle {
        id: overviewContainer
        anchors.centerIn: parent
        width: parent.width * 0.9
        height: parent.height * 0.8
        color: Qt.rgba(0.15, 0.15, 0.2, 1.0)
        radius: 12

        // Grid layout for desktop/activity thumbnails
        Grid {
            id: desktopGrid
            anchors.centerIn: parent
            columns: 3
            rows: Math.ceil(root.desktopCount / 3)
            columnSpacing: 20
            rowSpacing: 20

            Repeater {
                model: root.desktopCount

                Rectangle {
                    width: 280
                    height: 180
                    color: index === root.selectedIndex ? Qt.rgba(0.2, 0.4, 0.7, 1.0) : Qt.rgba(0.25, 0.25, 0.3, 1.0)
                    radius: 8

                    border.width: index === root.selectedIndex ? 4 : 2
                    border.color: index === root.selectedIndex ? Qt.rgba(0.5, 0.7, 1.0, 1.0) : Qt.rgba(0.4, 0.4, 0.45, 1.0)

                    Column {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        // Desktop thumbnail placeholder (colored rectangle)
                        Rectangle {
                            width: parent.width
                            height: parent.width * 0.5 // 2:1 aspect ratio
                            color: Qt.rgba(0.3, 0.3, 0.35, 1.0)
                            radius: 4

                            // Desktop number
                            Text {
                                anchors.centerIn: parent
                                color: Qt.rgba(0.7, 0.7, 0.7, 1.0)
                                font.pixelSize: 48
                                font.bold: true
                                text: (index + 1)
                            }
                        }

                        // Desktop name
                        Text {
                            width: parent.width
                            color: "white"
                            font.pixelSize: 14
                            font.bold: index === root.selectedIndex
                            text: effect.desktopNameAt(index)
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }

        // Title
        Text {
            anchors.top: parent.top
            anchors.topMargin: 15
            anchors.horizontalCenter: parent.horizontalCenter
            color: Qt.rgba(0.8, 0.8, 0.8, 1.0)
            font.pixelSize: 18
            font.bold: true
            text: root.selectedDesktopName ? "Overview - " + root.selectedDesktopName : "Overview"
        }

        // Debug info
        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
            color: "yellow"
            font.pixelSize: 10
            text: "Desktops: " + root.desktopCount + " | Selected: " + root.selectedIndex
        }
    }
}

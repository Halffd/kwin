/*
 KWin - the KDE window manager
 This file is part of the KDE project.

 SPDX-FileCopyrightText: 2020 Chris Holland <zrenfire@gmail.com>
 SPDX-FileCopyrightText: 2023 Nate Graham <nate@kde.org>
 SPDX-FileCopyrightText: 2024 KWin Contributors

 SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Layouts 1.1
import org.kde.plasma.core as PlasmaCore
import org.kde.ksvg 1.0 as KSvg
import org.kde.plasma.components 3.0 as PlasmaComponents3
import org.kde.kwin 3.0 as KWin
import org.kde.kirigami 2.20 as Kirigami

KWin.TabBoxSwitcher {
    id: tabBox

    Instantiator {
        active: tabBox.visible
        delegate: PlasmaCore.Dialog {
            location: PlasmaCore.Types.Floating
            visible: true
            flags: Qt.Popup | Qt.X11BypassWindowManagerHint
            x: tabBox.screenGeometry.x + tabBox.screenGeometry.width * 0.5 - dialogMainItem.width * 0.5
            y: tabBox.screenGeometry.y + tabBox.screenGeometry.height * 0.5 - dialogMainItem.height * 0.5

            mainItem: FocusScope {
                id: dialogMainItem
                focus: true

                property int maxWidth: tabBox.screenGeometry.width * 0.7
                property int maxHeight: tabBox.screenGeometry.height * 0.6
                width: Math.min(listView.contentWidth, maxWidth)
                height: Math.min(listView.contentHeight, maxHeight)

                clip: true

                ListView {
                    id: listView
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    model: tabBox.model
                    currentIndex: tabBox.currentIndex
                    focus: true
                    highlightFollowsCurrentItem: true
                    highlightMoveDuration: 0
                    keyNavigationWraps: true

                    delegate: PlasmaComponents3.AbstractButton {
                        id: listItem
                        width: listView.width
                        height: Math.max(icon.implicitHeight, label.implicitHeight) + Kirigami.Units.smallSpacing * 2

                        Accessible.name: model.caption
                        Accessible.role: Accessible.ListItem

                        onClicked: {
                            tabBox.model.activate(index);
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Kirigami.Units.smallSpacing
                            anchors.rightMargin: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                id: icon
                                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                                source: model.icon
                            }

                            PlasmaComponents3.Label {
                                id: label
                                Layout.fillWidth: true
                                text: model.caption
                                elide: Text.ElideRight
                                font.weight: listItem.highlighted ? Font.Bold : Font.Normal
                            }

                            PlasmaComponents3.ToolButton {
                                Layout.preferredWidth: icon.implicitWidth
                                Layout.preferredHeight: icon.implicitHeight
                                visible: model.closeable && typeof tabBox.model.close !== 'undefined' &&
                                        (listItem.containsMouse || listItem.highlighted || Kirigami.Settings.tabletMode)
                                icon.name: 'window-close-symbolic'
                                onClicked: tabBox.model.close(index);
                            }
                        }
                    }

                    highlight: PlasmaComponents3.Highlight {
                        width: listView.width - Kirigami.Units.largeSpacing
                        x: Kirigami.Units.smallSpacing
                    }

                    onCurrentIndexChanged: tabBox.currentIndex = listView.currentIndex;
                }

                Keys.onPressed: {
                    if (event.key == Qt.Key_Up) {
                        listView.decrementCurrentIndex();
                    } else if (event.key == Qt.Key_Down) {
                        listView.incrementCurrentIndex();
                    } else if (event.key == Qt.Key_Delete) {
                        // Close the currently selected window
                        if (listView.currentIndex >= 0 && listView.currentIndex < listView.count) {
                            tabBox.model.close(listView.currentIndex);
                            // If there's only one item left after closing, exit the tab box
                            if (listView.count <= 1) {
                                tabBox.model.activate(listView.currentIndex);
                            }
                        }
                    } else {
                        return;
                    }

                    listView.currentIndexChanged(listView.currentIndex);
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 2
                    icon.source: "edit-none"
                    text: i18ndc("kwin_x11", "@info:placeholder no entries in the task switcher", "No open windows")
                    visible: listView.count === 0
                }
            }
        }
    }
}
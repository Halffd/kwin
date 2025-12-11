/*
 KWin - the KDE window manager
 This file is part of the KDE project.

 SPDX-FileCopyrightText: 2020 Chris Holland <zrenfire@gmail.com>
 SPDX-FileCopyrightText: 2023 Nate Graham <nate@kde.org>

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

                property int maxWidth: tabBox.screenGeometry.width * 0.9
                property int maxHeight: tabBox.screenGeometry.height * 0.7
                property real screenFactor: tabBox.screenGeometry.width / tabBox.screenGeometry.height
                property int maxGridColumnsByWidth: Math.floor(maxWidth / thumbnailGridView.cellWidth)

                property int gridColumns: {         // Simple greedy algorithm
                    // respect screenGeometry
                    const c = Math.min(thumbnailGridView.count, maxGridColumnsByWidth);
                    const residue = thumbnailGridView.count % c;
                    if (residue == 0) {
                        return c;
                    }
                    // start greedy recursion
                    return columnCountRecursion(c, c, c - residue);
                }

                property int gridRows: Math.ceil(thumbnailGridView.count / gridColumns)
                property int optimalWidth: thumbnailGridView.cellWidth * gridColumns
                property int optimalHeight: thumbnailGridView.cellHeight * gridRows
                width: Math.min(Math.max(thumbnailGridView.cellWidth, optimalWidth), maxWidth)
                height: Math.min(Math.max(thumbnailGridView.cellHeight, optimalHeight), maxHeight)

                clip: true

                // Step for greedy algorithm
                function columnCountRecursion(prevC, prevBestC, prevDiff) {
                    const c = prevC - 1;

                    // don't increase vertical extent more than horizontal
                    // and don't exceed maxHeight
                    if (prevC * prevC <= thumbnailGridView.count + prevDiff ||
                            maxHeight < Math.ceil(thumbnailGridView.count / c) * thumbnailGridView.cellHeight) {
                        return prevBestC;
                    }
                    const residue = thumbnailGridView.count % c;
                    // halts algorithm at some point
                    if (residue == 0) {
                        return c;
                    }
                    // empty slots
                    const diff = c - residue;

                    // compare it to previous count of empty slots
                    if (diff < prevDiff) {
                        return columnCountRecursion(c, c, diff);
                    } else if (diff == prevDiff) {
                        // when it's the same try again, we'll stop early enough thanks to the landscape mode condition
                        return columnCountRecursion(c, prevBestC, diff);
                    }
                    // when we've found a local minimum choose this one (greedy)
                    return columnCountRecursion(c, prevBestC, diff);
                }

                // Just to get the margin sizes
                KSvg.FrameSvgItem {
                    id: hoverItem
                    imagePath: "widgets/viewitem"
                    prefix: "hover"
                    visible: false
                }

                GridView {
                    id: thumbnailGridView
                    anchors.fill: parent
                    focus: true
                    model: tabBox.model
                    currentIndex: tabBox.currentIndex
                    cacheBuffer: cellWidth * 3 // Cache only nearby items to reduce memory usage
                    displaced: Transition {
                        NumberAnimation { properties: "x,y"; duration: 200 }
                    }
                    add: Transition {
                        NumberAnimation { properties: "opacity"; from: 0; to: 1; duration: 200 }
                    }

                    readonly property int iconSize: Kirigami.Units.iconSizes.huge
                    readonly property int captionRowHeight: Kirigami.Units.gridUnit * 2
                    readonly property int columnSpacing: Kirigami.Units.gridUnit * 0.4
                    readonly property int thumbnailWidth: Kirigami.Units.gridUnit * 26
                    readonly property int thumbnailHeight: thumbnailWidth * (1.0/dialogMainItem.screenFactor)
                    cellWidth: hoverItem.margins.left + thumbnailWidth + hoverItem.margins.right
                    cellHeight: hoverItem.margins.top + captionRowHeight + thumbnailHeight + hoverItem.margins.bottom

                    keyNavigationWraps: true
                    highlightMoveDuration: 0

                    delegate: Item {
                        id: thumbnailGridItem
                        width: thumbnailGridView.cellWidth
                        height: thumbnailGridView.cellHeight

                        Accessible.name: model.caption
                        Accessible.role: Accessible.ListItem

                        // Load all thumbnails for smooth switching
                        readonly property bool shouldLoadThumbnail: true
                        property bool hovered: hoverHandler.hovered

                        HoverHandler {
                            id: hoverHandler
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                tabBox.model.activate(index);
                            }
                        }

                        Item {
                            id: thumbnailItem
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing

                            Item {
                                id: thumbnailContainer
                                anchors {
                                    top: parent.top
                                    left: parent.left
                                    right: parent.right
                                    bottom: captionItem.top
                                }

                                KSvg.FrameSvgItem {
                                    id: thumbnailFrame
                                    anchors.fill: parent
                                    imagePath: "widgets/viewitem"
                                    prefix: thumbnailGridView.currentIndex == index ? "selected+hover" : ""
                                }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: Kirigami.Units.smallSpacing
                                    spacing: 0

                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true

                                        Item {
                                            anchors.fill: parent
                                            // Show icon as placeholder while thumbnail is loading
                                            Kirigami.Icon {
                                                anchors.centerIn: parent
                                                width: thumbnailGridView.iconSize / 2
                                                height: thumbnailGridView.iconSize / 2
                                                source: model.icon
                                                opacity: thumbnailGridItem.shouldLoadThumbnail ? 0 : 0.6
                                                Behavior on opacity { NumberAnimation { duration: 150 } }
                                            }

                                            KWin.WindowThumbnail {
                                                anchors.fill: parent
                                                wId: windowId
                                                opacity: thumbnailGridItem.shouldLoadThumbnail ? 1 : 0
                                                visible: thumbnailGridItem.shouldLoadThumbnail
                                                Behavior on opacity {
                                                    NumberAnimation {
                                                        duration: 400
                                                        easing.type: Easing.InOutQuad
                                                    }
                                                }
                                            }
                                        }

                                        Kirigami.Icon {
                                            anchors {
                                                left: parent.left
                                                bottom: parent.bottom
                                                margins: Kirigami.Units.smallSpacing
                                            }
                                            width: thumbnailGridView.iconSize * 0.3
                                            height: thumbnailGridView.iconSize * 0.3
                                            source: model.icon
                                            visible: false
                                        }
                                    }
                                }
                            }

                            // Close button (top-right corner) - positioned at the container level
                            PlasmaComponents3.ToolButton {
                                anchors {
                                    top: thumbnailContainer.top
                                    right: thumbnailContainer.right
                                    topMargin: Kirigami.Units.smallSpacing
                                    rightMargin: Kirigami.Units.smallSpacing
                                }
                                z: 1000

                                visible: model.closeable &&
                                        typeof tabBox.model.close !== 'undefined' &&
                                        (thumbnailGridItem.hovered ||
                                         thumbnailGridView.currentIndex === index ||
                                         Kirigami.Settings.tabletMode)

                                width: Kirigami.Units.iconSizes.smallMedium
                                height: Kirigami.Units.iconSizes.smallMedium

                                icon.name: "window-close-symbolic"

                                onClicked: {
                                    tabBox.model.close(index);
                                }

                                // Tooltip
                                PlasmaComponents3.ToolTip {
                                    text: i18nd("kwin_x11", "Close window")
                                }
                            }

                            Item {
                                id: captionItem
                                anchors {
                                    left: parent.left
                                    right: parent.right
                                    bottom: parent.bottom
                                }
                                height: thumbnailGridView.captionRowHeight

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: Kirigami.Units.smallSpacing

                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        Layout.leftMargin: Kirigami.Units.smallSpacing
                                        Layout.rightMargin: Kirigami.Units.smallSpacing

                                        PlasmaComponents3.Label {
                                            id: captionLabel
                                            anchors.fill: parent
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            text: model.caption
                                            font.weight: thumbnailGridView.currentIndex == index ? Font.Bold : Font.Normal
                                            elide: Text.ElideMiddle
                                            color: thumbnailGridView.currentIndex == index ?
                                                   Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                        }
                                    }
                                }
                            }
                        }
                    } // GridView.delegate

                    highlight: KSvg.FrameSvgItem {
                        imagePath: "widgets/viewitem"
                        prefix: "hover"
                    }

                    // Animate thumbnail loading when current index changes
                    Behavior on currentIndex {
                        NumberAnimation { duration: 150 }
                    }

                    onCurrentIndexChanged: tabBox.currentIndex = thumbnailGridView.currentIndex;
                } // GridView

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 2
                    icon.source: "edit-none"
                    text: i18ndc("kwin_x11", "@info:placeholder no entries in the task switcher", "No open windows")
                    visible: thumbnailGridView.count === 0
                }

                Keys.onPressed: {
                    if (event.key == Qt.Key_Left) {
                        thumbnailGridView.moveCurrentIndexLeft();
                    } else if (event.key == Qt.Key_Right) {
                        thumbnailGridView.moveCurrentIndexRight();
                    } else if (event.key == Qt.Key_Up) {
                        thumbnailGridView.moveCurrentIndexUp();
                    } else if (event.key == Qt.Key_Down) {
                        thumbnailGridView.moveCurrentIndexDown();
                    } else if (event.key == Qt.Key_Delete) {
                        // Close the currently selected window
                        if (thumbnailGridView.currentIndex >= 0 && thumbnailGridView.currentIndex < thumbnailGridView.count) {
                            tabBox.model.close(thumbnailGridView.currentIndex);
                            // If there's only one item left after closing, exit the tab box
                            if (thumbnailGridView.count <= 1) {
                                tabBox.model.activate(thumbnailGridView.currentIndex);
                            }
                        }
                    } else {
                        return;
                    }

                    thumbnailGridView.currentIndexChanged(thumbnailGridView.currentIndex);
                }
            } // Dialog.mainItem
        } // Dialog
    } // Instantiator
}

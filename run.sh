#!/bin/bash
cd ~/kde/
export KDE_PREFIX=/home/half-arch/kde/usr

# IMPORTANT: Don't append system paths, REPLACE them
export LD_LIBRARY_PATH="$KDE_PREFIX/lib:$KDE_PREFIX/lib64"
export QT_PLUGIN_PATH="$KDE_PREFIX/lib/plugins:$KDE_PREFIX/lib/qt6/plugins"
#export QT_PLUGIN_PATH="$KDE_PREFIX/lib/plugins:$KDE_PREFIX/lib/qt6/plugins:/usr/lib/qt6/plugins"
export QML2_IMPORT_PATH="$KDE_PREFIX/lib/qml:$KDE_PREFIX/lib/qt6/qml"
export XDG_DATA_DIRS="$KDE_PREFIX/share:/usr/local/share:/usr/share"
QT_QUICK_CONTROLS_STYLE=org.kde.desktop
export KDE_SESSION_VERSION=6
export XDG_CURRENT_DESKTOP=KDE

# Point to your actual kdeglobals config
export KDEHOME=$HOME/.config
export KDE_CONFIG_DIR=$HOME/.config
# Clear Qt's internal cache
export QML_DISABLE_DISK_CACHE=1

echo "Using LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
echo "Using QML2_IMPORT_PATH=$QML2_IMPORT_PATH"
echo "Using QT_PLUGIN_PATH=$QT_PLUGIN_PATH"

$KDE_PREFIX/bin/kwin_x11 --replace

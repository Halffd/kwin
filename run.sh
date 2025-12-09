#!/bin/bash
cd ~/kde/build/kwin/
export KDE_PREFIX=/home/half-arch/kde/usr

# Put local paths BEFORE system paths
export LD_LIBRARY_PATH="$KDE_PREFIX/lib:$KDE_PREFIX/lib64"
export QT_PLUGIN_PATH="$KDE_PREFIX/lib/plugins:$KDE_PREFIX/lib/plugins/platforms:$QT_PLUGIN_PATH"
export QT_QPA_PLATFORM_PLUGIN_PATH="$KDE_PREFIX/lib/plugins/platforms"
export QML2_IMPORT_PATH="$KDE_PREFIX/lib/qml:$QML2_IMPORT_PATH"
export QML_IMPORT_PATH="$KDE_PREFIX/lib/qml:$QML_IMPORT_PATH"
export XDG_DATA_DIRS="$KDE_PREFIX/share:$XDG_DATA_DIRS"

# Optional debug to confirm loads
echo "Using LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
echo "Using QML2_IMPORT_PATH=$QML2_IMPORT_PATH"
echo "Using QT_PLUGIN_PATH=$QT_PLUGIN_PATH"
# Run from build dir
exec ./bin/kwin_x11 --replace

# KWin DBus Interface Documentation

This document provides comprehensive documentation for all DBus interfaces available in KWin, including the new window management interface.

## Table of Contents

1. [Night Light DBus Interface](#night-light-dbus-interface)
2. [Window Manager DBus Interface](#window-manager-dbus-interface)
3. [Effects DBus Interface](#effects-dbus-interface)
4. [Usage Examples](#usage-examples)
5. [Testing Scripts](#testing-scripts)

---

## Night Light DBus Interface

**Service:** `org.kde.KWin`  
**Object Path:** `/NightLight`  
**Interface:** `org.kde.KWin.NightLight`

### Properties

| Property | Type | Description |
|----------|-------|-------------|
| `enabled` | Boolean | Whether Night Light is enabled |
| `running` | Boolean | Whether Night Light is currently running |
| `currentTemperature` | Integer | Current color temperature (Kelvin) |
| `targetTemperature` | Integer | Target color temperature (Kelvin) |
| `mode` | String | Current mode (`Automatic`, `Manual`, `Location`) |
| `currentBrightness` | Double | Current brightness level (0.0-1.0) |
| `targetBrightness` | Double | Target brightness level (0.0-1.0) |
| `gammaRed` | Double | Red gamma value |
| `gammaGreen` | Double | Green gamma value |
| `gammaBlue` | Double | Blue gamma value |

### Methods

#### Basic Control
- `toggle()` - Toggle Night Light on/off
- `setEnabled(enabled)` - Enable/disable Night Light
- `setTemperature(temperature)` - Set color temperature (Kelvin)
- `setMode(mode)` - Set mode (`Automatic`, `Manual`, `Location`)

#### Brightness Control
- `increaseBrightness(step)` - Increase brightness by step (default: 0.1)
- `decreaseBrightness(step)` - Decrease brightness by step (default: 0.1)
- `setBrightness(brightness)` - Set brightness level (0.0-1.0)
- `getBrightness()` - Get current brightness level
- `resetBrightness()` - Reset brightness to default (1.0)

#### Gamma Control
- `setGamma(red, green, blue)` - Set RGB gamma values (0.1-10.0)
- `getGamma()` - Get current gamma values
- `resetGamma()` - Reset gamma to default (1.0, 1.0, 1.0)

#### Temperature Control
- `increaseTemperature(step)` - Increase temperature by step (default: 100K)
- `decreaseTemperature(step)` - Decrease temperature by step (default: 100K)
- `getTemperature()` - Get current temperature

### Usage Examples

```bash
# Toggle Night Light
qdbus org.kde.KWin /NightLight toggle

# Set temperature to 4000K
qdbus org.kde.KWin /NightLight setTemperature 4000

# Increase brightness by 0.2
qdbus org.kde.KWin /NightLight increaseBrightness 0.2

# Set custom gamma
qdbus org.kde.KWin /NightLight setGamma 1.2 0.9 0.8

# Get current settings
qdbus org.kde.KWin /NightLight currentTemperature
qdbus org.kde.KWin /NightLight currentBrightness
```

---

## Window Manager DBus Interface

**Service:** `org.kde.KWin`  
**Object Path:** `/WindowManager`  
**Interface:** `org.kde.KWin.WindowManager`

### Window Information Methods

#### Window Listing & Finding
| Method | Parameters | Returns | Description |
|---------|------------|----------|-------------|
| `listWindows()` | None | Array of QVariantMap | List all windows with full details |
| `getActiveWindow()` | None | QVariantMap | Get currently active window |
| `findWindowsByClass(windowClass)` | String (windowClass) | Array of QVariantMap | Find windows by WM_CLASS |
| `findWindowsByTitle(title)` | String (title) | Array of QVariantMap | Find windows by title (partial match) |
| `findWindowsByPid(pid)` | UInt32 (pid) | Array of QVariantMap | Find windows by process ID |
| `getWindowInfo(windowId)` | String (windowId) | QVariantMap | Get detailed info for specific window |

#### Window Property Getters
| Method | Parameters | Returns | Description |
|---------|------------|----------|-------------|
| `getWindowTitle(windowId)` | String (windowId) | String | Get window title |
| `getWindowClass(windowId)` | String (windowId) | String | Get window class (WM_CLASS) |
| `getWindowId(windowId)` | String (windowId) | String | Get unique window ID |
| `getWindowPid(windowId)` | String (windowId) | UInt32 | Get process ID |
| `getWindowExecutable(windowId)` | String (windowId) | String | Get executable name |
| `getWindowPosition(windowId)` | String (windowId) | QVariantMap | Get x,y position |
| `getWindowSize(windowId)` | String (windowId) | QVariantMap | Get width,height |
| `getWindowGeometry(windowId)` | String (windowId) | QVariantMap | Get full geometry |
| `getWindowState(windowId)` | String (windowId) | QVariantMap | Get window state |
| `getWindowTransparency(windowId)` | String (windowId) | Boolean | Check if window is transparent |

### Window Action Methods

#### Basic Actions
| Method | Parameters | Returns | Description |
|---------|------------|----------|-------------|
| `activateWindow(windowId)` | String (windowId) | Boolean | Activate/focus window |
| `closeWindow(windowId)` | String (windowId) | Boolean | Close window |
| `minimizeWindow(windowId)` | String (windowId) | Boolean | Minimize window |
| `unminimizeWindow(windowId)` | String (windowId) | Boolean | Restore from minimized |
| `maximizeWindow(windowId)` | String (windowId) | Boolean | Maximize window |
| `unmaximizeWindow(windowId)` | String (windowId) | Boolean | Unmaximize window |
| `fullscreenWindow(windowId)` | String (windowId) | Boolean | Make window fullscreen |
| `unfullscreenWindow(windowId)` | String (windowId) | Boolean | Exit fullscreen |

#### Advanced Actions
| Method | Parameters | Returns | Description |
|---------|------------|----------|-------------|
| `setAlwaysOnTop(windowId, enabled)` | String (windowId), Boolean | Boolean | Set always on top state |
| `setWindowOpacity(windowId, opacity)` | String (windowId), Double | Boolean | Set opacity (0.1-1.0) |

#### Toggle Actions
| Method | Parameters | Returns | Description |
|---------|------------|----------|-------------|
| `toggleMaximizeWindow(windowId)` | String (windowId) | Boolean | Toggle maximize state |
| `toggleFullscreenWindow(windowId)` | String (windowId) | Boolean | Toggle fullscreen |
| `toggleMinimizeWindow(windowId)` | String (windowId) | Boolean | Toggle minimize |
| `toggleAlwaysOnTop(windowId)` | String (windowId) | Boolean | Toggle always on top |

### Window Geometry Methods

| Method | Parameters | Returns | Description |
|---------|------------|----------|-------------|
| `moveWindow(windowId, x, y)` | String (windowId), Int32 (x), Int32 (y) | Boolean | Move window to position |
| `resizeWindow(windowId, width, height)` | String (windowId), Int32 (width), Int32 (height) | Boolean | Resize window |
| `moveAndResizeWindow(windowId, x, y, width, height)` | String (windowId), Int32 (x), Int32 (y), Int32 (width), Int32 (height) | Boolean | Move and resize |
| `centerWindow(windowId)` | String (windowId) | Boolean | Center window on current monitor |
| `sendWindowToMonitor(windowId, monitor)` | String (windowId), Int32 (monitor) | Boolean | Send to specific monitor |
| `sendWindowToDesktop(windowId, desktop)` | String (windowId), Int32 (desktop) | Boolean | Send to virtual desktop |

### Monitor Management Methods

| Method | Parameters | Returns | Description |
|---------|------------|----------|-------------|
| `listMonitors()` | None | Array of QVariantMap | List all monitors |
| `getMonitorInfo(monitor)` | Int32 (monitor) | QVariantMap | Get specific monitor info |

#### Monitor Info Properties
Each monitor info contains:
- `index` - Monitor index (Integer)
- `name` - Monitor name (String)
- `x`, `y` - Position (Integer)
- `width`, `height` - Resolution (Integer)
- `refreshRate` - Refresh rate (Integer)
- `scale` - Scale factor (Double)
- `enabled` - Whether enabled (Boolean)

### Window Information Structure

#### Window Info Map
Each window info contains:
- `id` - Unique window ID (String)
- `title` - Window title (String)
- `resourceClass` - Window class (String)
- `resourceName` - Window resource name (String)
- `desktopFile` - Desktop file (String)
- `role` - Window role (String)
- `caption` - Window caption (String)
- `clientMachine` - Client machine (String)
- `localhost` - Is localhost (Boolean)
- `type` - Window type (Integer)
- `x`, `y` - Position (Integer)
- `width`, `height` - Size (Integer)
- `desktops` - Desktop IDs (Array)
- `minimized` - Is minimized (Boolean)
- `fullscreen` - Is fullscreen (Boolean)
- `keepAbove` - Keep above (Boolean)
- `keepBelow` - Keep below (Boolean)
- `noBorder` - No border (Boolean)
- `skipTaskbar` - Skip taskbar (Boolean)
- `skipPager` - Skip pager (Boolean)
- `skipSwitcher` - Skip switcher (Boolean)
- `maximizeHorizontal` - Maximized horizontally (Boolean)
- `maximizeVertical` - Maximized vertically (Boolean)
- `uuid` - Internal UUID (String)
- `layer` - Window layer (Integer)
- `pid` - Process ID (Integer)

#### Window State Map
Window state contains:
- `minimized` - Is minimized (Boolean)
- `maximized` - Is maximized (Boolean)
- `fullscreen` - Is fullscreen (Boolean)
- `alwaysOnTop` - Always on top (Boolean)
- `shaded` - Is shaded (Boolean)

#### Position/Size Maps
Position map: `x`, `y` (Integer)  
Size map: `width`, `height` (Integer)  
Geometry map: `x`, `y`, `width`, `height` (Integer)

### Usage Examples

```bash
# List all windows
qdbus org.kde.KWin /WindowManager listWindows

# Get active window
ACTIVE=$(qdbus org.kde.KWin /WindowManager getActiveWindow)
WINDOW_ID=$(echo "$ACTIVE" | jq -r '.id')

# Get window properties
qdbus org.kde.KWin /WindowManager getWindowTitle "$WINDOW_ID"
qdbus org.kde.KWin /WindowManager getWindowGeometry "$WINDOW_ID"

# Window actions
qdbus org.kde.KWin /WindowManager maximizeWindow "$WINDOW_ID"
qdbus org.kde.KWin /WindowManager setWindowOpacity "$WINDOW_ID" 0.7
qdbus org.kde.KWin /WindowManager setAlwaysOnTop "$WINDOW_ID" true

# Window geometry
qdbus org.kde.KWin /WindowManager moveWindow "$WINDOW_ID" 100 100
qdbus org.kde.KWin /WindowManager resizeWindow "$WINDOW_ID" 800 600
qdbus org.kde.KWin /WindowManager centerWindow "$WINDOW_ID"

# Monitor operations
qdbus org.kde.KWin /WindowManager listMonitors
qdbus org.kde.KWin /WindowManager sendWindowToMonitor "$WINDOW_ID" 1

# Find windows
qdbus org.kde.KWin /WindowManager findWindowsByClass "firefox"
qdbus org.kde.KWin /WindowManager findWindowsByTitle "Terminal"
```

---

## Effects DBus Interface

**Service:** `org.kde.KWin`  
**Object Path:** `/Effects`  
**Interface:** `org.kde.kwin.Effects`

### Methods

| Method | Parameters | Description |
|---------|------------|-------------|
| `loadEffect(effectName)` | String (effectName) | Load specific effect |
| `unloadEffect(effectName)` | String (effectName) | Unload specific effect |
| `isEffectLoaded(effectName)` | String (effectName) | Check if effect is loaded |
| `listEffects()` | None | List all available effects |
| `listLoadedEffects()` | None | List currently loaded effects |

### Usage Examples

```bash
# List all effects
qdbus org.kde.KWin /Effects listEffects

# Load blur effect
qdbus org.kde.KWin /Effects loadEffect "blur"

# Check if effect is loaded
qdbus org.kde.KWin /Effects isEffectLoaded "blur"

# Unload effect
qdbus org.kde.KWin /Effects unloadEffect "blur"
```

---

## Usage Examples

### Python Examples

```python
import dbus
import json

# Connect to session bus
bus = dbus.SessionBus()

# Window Manager interface
wm = dbus.Interface(bus.get_object('org.kde.KWin', '/WindowManager'), 
                   'org.kde.KWin.WindowManager')

# Night Light interface
nightlight = dbus.Interface(bus.get_object('org.kde.KWin', '/NightLight'), 
                         'org.kde.KWin.NightLight')

# Effects interface
effects = dbus.Interface(bus.get_object('org.kde.KWin', '/Effects'), 
                        'org.kde.kwin.Effects')

# List windows
windows = wm.listWindows()
for window in windows:
    print(f"Window: {window['title']} ({window['resourceClass']})")

# Get active window and maximize it
active = wm.getActiveWindow()
wm.maximizeWindow(active['id'])

# Set Night Light temperature
nightlight.setTemperature(4000)

# Load effect
effects.loadEffect("blur")
```

### Shell Script Examples

```bash
#!/bin/bash

# Function to get window ID
get_window_id() {
    qdbus org.kde.KWin /WindowManager getActiveWindow | jq -r '.id'
}

# Function to maximize active window
maximize_active() {
    local window_id=$(get_window_id)
    qdbus org.kde.KWin /WindowManager maximizeWindow "$window_id"
}

# Function to set window opacity
set_opacity() {
    local opacity=$1
    local window_id=$(get_window_id)
    qdbus org.kde.KWin /WindowManager setWindowOpacity "$window_id" "$opacity"
}

# Function to toggle Night Light
toggle_nightlight() {
    qdbus org.kde.KWin /NightLight toggle
}

# Usage examples
maximize_active
set_opacity 0.7
toggle_nightlight
```

### Advanced Usage

#### Window Management Workflow
```bash
# Get all Firefox windows
firefox_windows=$(qdbus org.kde.KWin /WindowManager findWindowsByClass "firefox")

# Extract first window ID
first_ff=$(echo "$firefox_windows" | jq -r '.[0].id')

# Move Firefox to second monitor and maximize
qdbus org.kde.KWin /WindowManager sendWindowToMonitor "$first_ff" 1
qdbus org.kde.KWin /WindowManager maximizeWindow "$first_ff"

# Set opacity to 80%
qdbus org.kde.KWin /WindowManager setWindowOpacity "$first_ff" 0.8
```

#### Night Light Automation
```bash
# Set warm temperature for evening
set_evening_mode() {
    qdbus org.kde.KWin /NightLight setTemperature 3000
    qdbus org.kde.KWin /NightLight setBrightness 0.8
}

# Set cool temperature for daytime
set_day_mode() {
    qdbus org.kde.KWin /NightLight setTemperature 6500
    qdbus org.kde.KWin /NightLight setBrightness 1.0
}

# Toggle based on time
hour=$(date +%H)
if [ "$hour" -ge 18 ] || [ "$hour" -lt 6 ]; then
    set_evening_mode
else
    set_day_mode
fi
```

---

## Testing Scripts

### Comprehensive Test Scripts

1. **Night Light Test Script:** `/tmp/test_nightlight_extended.py`
   - Tests all Night Light functionality
   - Demonstrates brightness, temperature, and gamma control
   - Includes error handling

2. **Window Manager Test Script:** `/tmp/test_windowmanager_dbus.py`
   - Tests all window management operations
   - Interactive and automated modes
   - Comprehensive coverage of all methods

3. **Shell Examples:** `/tmp/nightlight_dbus_examples.sh` and `/tmp/windowmanager_dbus_examples.sh`
   - Quick reference for common operations
   - Copy-paste ready commands
   - Includes error checking

### Running Tests

```bash
# Make scripts executable
chmod +x /tmp/test_*.py /tmp/*_dbus_examples.sh

# Run Night Light tests
python3 /tmp/test_nightlight_extended.py

# Run Window Manager tests (automated)
python3 /tmp/test_windowmanager_dbus.py

# Run Window Manager tests (interactive)
python3 /tmp/test_windowmanager_dbus.py --interactive

# Run shell examples
/tmp/nightlight_dbus_examples.sh
/tmp/windowmanager_dbus_examples.sh
```

---

## Error Handling

### Common Error Scenarios

1. **Invalid Window ID:** Methods return `false` or empty QVariantMap
2. **Window Not Movable/Resizable:** Geometry operations return `false`
3. **Invalid Monitor Index:** Monitor operations return `false` or empty QVariantMap
4. **Invalid Parameters:** Methods return `false` or appropriate error values
5. **Interface Not Available:** DBus call fails with service error

### Best Practices

1. Always check return values for operations
2. Use window IDs from `listWindows()` or `getActiveWindow()`
3. Validate monitor indices before using them
4. Handle cases where windows might not support certain operations
5. Use try-catch blocks in scripts for robustness

---

## Integration Examples

### Desktop Environment Integration

```bash
# Add to desktop shortcuts
# Window management shortcuts
khotkeys "Ctrl+Alt+Up" "qdbus org.kde.KWin /WindowManager maximizeWindow \$(get_window_id)"
khotkeys "Ctrl+Alt+Down" "qdbus org.kde.KWin /WindowManager minimizeWindow \$(get_window_id)"

# Night Light shortcuts
khotkeys "Ctrl+Alt+N" "qdbus org.kde.KWin /NightLight toggle"
```

### Application Integration

```python
# Application that manages windows
class WindowManager:
    def __init__(self):
        self.bus = dbus.SessionBus()
        self.wm = dbus.Interface(self.bus.get_object('org.kde.KWin', '/WindowManager'), 
                               'org.kde.KWin.WindowManager')
    
    def arrange_windows(self):
        """Arrange windows in grid layout"""
        windows = self.wm.listWindows()
        for i, window in enumerate(windows):
            x = (i % 4) * 400
            y = (i // 4) * 300
            self.wm.moveWindow(window['id'], x, y)
            self.wm.resizeWindow(window['id'], 380, 280)
```

---

## Conclusion

KWin provides comprehensive DBus interfaces for:

1. **Night Light Control:** Complete control over color temperature, brightness, and gamma
2. **Window Management:** Full window manipulation including finding, moving, resizing, and state control
3. **Effects Management:** Load and unload KWin effects dynamically

These interfaces enable powerful automation and integration capabilities for desktop environments, applications, and scripts. The new Window Manager interface provides complete programmatic control over all window management operations previously only available through the GUI.

For more information, see the KWin source code and KDE documentation.

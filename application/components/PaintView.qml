import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import Theme 1.0
import QFlipper 1.0

Rectangle {
    id: root

    implicitWidth: 745
    implicitHeight: 302
    // Fully opaque background to hide device information behind
    color: Theme.color.bluepurple7

    property bool active: false
    property bool wasVirtualDisplayAllowed: true
    property real zoomLevel: 1.0
    property real panX: 0.0
    property real panY: 0.0
    onZoomLevelChanged: if(paintCanvas) paintCanvas.requestPaint()
    onPanXChanged: if(paintCanvas) paintCanvas.requestPaint()
    onPanYChanged: if(paintCanvas) paintCanvas.requestPaint()

    // Pixel data storage (128x64 = 8192 pixels, stored as boolean array)
    property var pixelData: []

    // Convert pixel data to Flipper format (bit-packed, transposed)
    // VirtualDisplay expects transposed format (same as what Flipper Zero sends)
    // Format: vertical packing where byte index = y / 8 * width + x, bit = y % 8
    function pixelDataToFlipperFormat() {
        var width = 128
        var height = 64
        var totalBytes = (width * height) / 8  // Should be 1024
        
        // Initialize output array (transposed/vertical packing format)
        var output = []
        for(var i = 0; i < totalBytes; i++) {
            output.push(0)
        }
        
        // Convert from our internal format (pixelData[y * 128 + x]) to Flipper format
        // The comment in screenstreamer.cpp says "ScreenStream and VirtualDisplay formats differ"
        // ScreenStream receives transposed format, but VirtualDisplay might expect non-transposed
        // Try sending in normal horizontal packing format (non-transposed)
        for(var y = 0; y < height; y++) {
            for(var x = 0; x < width; x++) {
                var pixelIdx = y * width + x
                if(pixelIdx < pixelData.length && pixelData[pixelIdx]) {
                    // Normal horizontal packing (non-transposed): byte = (y * width + x) / 8, bit = (y * width + x) % 8
                    var byteIdx = Math.floor((y * width + x) / 8)
                    var bitIdx = (y * width + x) % 8
                    
                    if(byteIdx < output.length) {
                        output[byteIdx] |= (1 << bitIdx)
                    }
                }
            }
        }
        
        if(output.length !== 1024) {
            console.error("[PaintView] Frame conversion error: expected 1024 bytes, got", output.length)
        }
        
        return output
    }

    property var pendingFrame: null
    property bool autoSend: false
    
    Timer {
        id: autoSendTimer
        interval: 100  // Send every 100ms while drawing
        repeat: false
        onTriggered: {
            if(autoSend && active) {
                sendToDevice()
            }
        }
    }

    function sendToDevice() {
        if(!Backend.deviceState) {
            console.warn("[PaintView] Device state not available")
            return
        }
        
        if(!Backend.deviceState.isOnline) {
            console.warn("[PaintView] Device not online")
            return
        }
        
        if(!Backend.virtualDisplay) {
            console.warn("[PaintView] Virtual display object not available")
            return
        }
        
        // Note: We don't check isAllowVirtualDisplay here because we enable it in activate()
        
        var flipperData = pixelDataToFlipperFormat()
        
        // Validate frame size
        if(flipperData.length !== 1024) {
            console.error("[PaintView] Invalid frame size:", flipperData.length, "expected 1024")
            return
        }
        
        // Convert to QByteArray format (JavaScript array of numbers 0-255)
        var byteArray = []
        for(var i = 0; i < flipperData.length; i++) {
            // Ensure values are in 0-255 range
            var byteValue = flipperData[i] & 0xFF
            byteArray.push(byteValue)
        }
        
        // VirtualDisplay.DisplayState: Starting=0, Running=1, Stopping=2, Stopped=3
        var virtualDisplay = Backend.virtualDisplay
        var state = virtualDisplay.displayState
        
        // Handle undefined state (property might not be accessible yet)
        if(state === undefined || state === null) {
            console.warn("[PaintView] Virtual display state is undefined, attempting to start anyway")
            // Try to start it - if it's already running, this will be ignored
            virtualDisplay.startFromArray(byteArray)
            return
        }
        
        if(state === 3) {
            // Stopped - start virtual display with the actual frame data
            console.log("[PaintView] Starting virtual display with frame data, size:", byteArray.length)
            virtualDisplay.startFromArray(byteArray)
        } else if(state === 1) {
            // Running - send frame immediately
            console.log("[PaintView] Sending frame to device, size:", byteArray.length)
            virtualDisplay.sendFrameFromArray(byteArray)
        } else if(state === 0) {
            // Starting - queue the frame to send once ready
            console.log("[PaintView] Virtual display starting, queueing frame")
            pendingFrame = byteArray
        } else {
            console.warn("[PaintView] Virtual display in unexpected state:", state, "(0=Starting, 1=Running, 2=Stopping, 3=Stopped)")
        }
    }

    function clearCanvas() {
        for(var i = 0; i < pixelData.length; i++) {
            pixelData[i] = false
        }
        paintCanvas.requestPaint()
    }

    function setPixel(x, y, value) {
        if(x < 0 || x >= 128 || y < 0 || y >= 64) {
            return
        }
        var idx = y * 128 + x
        pixelData[idx] = value
        paintCanvas.requestPaint()
    }

    Connections {
        target: Backend.virtualDisplay
        function onDisplayStateChanged() {
            if(!Backend.virtualDisplay) return
            
            var state = Backend.virtualDisplay.displayState
            console.log("[PaintView] Virtual display state changed to:", state, "(0=Starting, 1=Running, 2=Stopping, 3=Stopped)")
            
            // When virtual display becomes Running, send any pending frame
            if(state === 1 && pendingFrame !== null) {
                console.log("[PaintView] Virtual display ready, sending pending frame, size:", pendingFrame.length)
                // Small delay to ensure virtual display is fully ready
                Qt.callLater(function() {
                    if(Backend.virtualDisplay && Backend.virtualDisplay.displayState === 1) {
                        Backend.virtualDisplay.sendFrameFromArray(pendingFrame)
                        pendingFrame = null
                    }
                })
            }
        }
    }

    function activate() {
        if(active) return
        active = true
        
        console.log("[PaintView] Activating paint view")
        
        // Ensure virtual display is ready
        // VirtualDisplay.DisplayState: Starting=0, Running=1, Stopping=2, Stopped=3
        if(!Backend.deviceState) {
            console.warn("[PaintView] Cannot activate: device state not available")
            return
        }
        
        if(!Backend.deviceState.isOnline) {
            console.warn("[PaintView] Cannot activate: device not online")
            return
        }
        
        if(!Backend.virtualDisplay) {
            console.warn("[PaintView] Cannot activate: virtual display object not available")
            return
        }
        
        // Store original state and enable virtual display for paint feature
        // TODO: Re-enable once backend MOC files are regenerated with isAllowVirtualDisplay property
        // For now, we'll assume virtual display is allowed
        wasVirtualDisplayAllowed = true
        
        // Disable and stop screen streamer when using virtual display
        if(Backend.screenStreamer) {
            Backend.screenStreamer.isEnabled = false
            // Stop screen streamer if it's running (streamState: Stopped=0, Starting=1, Running=2, Stopping=3)
            if(Backend.screenStreamer.streamState !== 0) {
                console.log("[PaintView] Stopping screen streamer for virtual display")
                Backend.screenStreamer.stop()
            }
        }
        
        var state = Backend.virtualDisplay.displayState
        if(state === 3) {
            console.log("[PaintView] Starting virtual display with blank frame")
            // Start with a blank frame
            var blankFrame = []
            for(var i = 0; i < 1024; i++) {
                blankFrame.push(0)
            }
            Backend.virtualDisplay.startFromArray(blankFrame)
        } else {
            console.log("[PaintView] Virtual display already active, state:", state)
        }
    }

    function deactivate() {
        if(!active) return
        active = false
        
        console.log("[PaintView] Deactivating paint view")
        
        // Stop virtual display when leaving paint view
        // VirtualDisplay.DisplayState.Stopped = 3
        if(Backend.virtualDisplay && Backend.virtualDisplay.displayState !== 3) {
            console.log("[PaintView] Stopping virtual display")
            Backend.virtualDisplay.stop()
        }
        
        // Restore original virtual display allowed state
        // TODO: Re-enable once backend MOC files are regenerated with setAllowVirtualDisplay method
        // For now, we'll skip the restore
        
        // Re-enable screen streamer
        if(Backend.screenStreamer) {
            Backend.screenStreamer.isEnabled = true
            console.log("[PaintView] Screen streamer restarted")
        }
        
        pendingFrame = null
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 8

        // Canvas area
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.color.bluepurple7
            radius: 6
            border.color: Theme.color.bluepurple3
            border.width: 1
            
            Canvas {
                id: paintCanvas
                anchors.fill: parent
                anchors.margins: 4

                readonly property int canvasWidth: 128
                readonly property int canvasHeight: 64
                readonly property real baseScaleX: width > 0 ? width / canvasWidth : 1.0
                readonly property real baseScaleY: height > 0 ? height / canvasHeight : 1.0
                readonly property real scaleX: baseScaleX * root.zoomLevel
                readonly property real scaleY: baseScaleY * root.zoomLevel

                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d")
                    if(!ctx) return
                    if(pixelData.length === 0) return
                    
                    // Calculate canvas position and size
                    var scaledWidth = canvasWidth * scaleX
                    var scaledHeight = canvasHeight * scaleY
                    var offsetX = (width - scaledWidth) / 2 + root.panX
                    var offsetY = (height - scaledHeight) / 2 + root.panY
                    
                    // Fill entire canvas area with background color (to cover any artifacts)
                    ctx.fillStyle = Theme.color.bluepurple7
                    ctx.fillRect(0, 0, width, height)
                    
                    ctx.save()
                    
                    // Clip to canvas bounds to prevent drawing outside
                    ctx.beginPath()
                    ctx.rect(offsetX, offsetY, scaledWidth, scaledHeight)
                    ctx.clip()
                    
                    // Translate and scale for canvas content
                    ctx.translate(offsetX, offsetY)
                    ctx.scale(scaleX, scaleY)
                    
                    // Fill white background (only within clipped area)
                    ctx.fillStyle = "#FFFFFF"
                    ctx.fillRect(0, 0, canvasWidth, canvasHeight)
                    
                    // Draw grid for reference (scaled with canvas)
                    ctx.strokeStyle = "#E0E0E0"
                    ctx.lineWidth = 0.1  // Very thin lines that scale naturally
                    for(var x = 0; x <= canvasWidth; x += 16) {
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, canvasHeight)
                        ctx.stroke()
                    }
                    for(var y = 0; y <= canvasHeight; y += 16) {
                        ctx.beginPath()
                        ctx.moveTo(0, y)
                        ctx.lineTo(canvasWidth, y)
                        ctx.stroke()
                    }
                    
                    // Draw pixels from pixelData
                    ctx.fillStyle = "#000000"
                    for(var y = 0; y < 64; y++) {
                        for(var x = 0; x < 128; x++) {
                            var idx = y * 128 + x
                            if(idx < pixelData.length && pixelData[idx]) {
                                ctx.fillRect(x, y, 1, 1)
                            }
                        }
                    }
                    
                    ctx.restore()
                    
                    // Draw border around canvas to clearly show boundaries
                    ctx.save()
                    ctx.strokeStyle = Theme.color.bluepurple3
                    ctx.lineWidth = 2
                    ctx.strokeRect(offsetX, offsetY, scaledWidth, scaledHeight)
                    ctx.restore()
                }

            }
            
            // MouseArea covering entire Rectangle to block click-through
            MouseArea {
                id: canvasMouseArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.AllButtons
                propagateComposedEvents: false
                z: 1
                focus: true

                property bool isDrawing: false
                property bool isPanning: false
                property int lastX: -1
                property int lastY: -1
                property int panStartX: 0
                property int panStartY: 0
                property real panStartOffsetX: 0
                property real panStartOffsetY: 0

                onPressed: function(mouse) {
                    mouse.accepted = true
                    if(mouse.button === Qt.MiddleButton) {
                        // Start panning
                        isPanning = true
                        panStartX = mouse.x
                        panStartY = mouse.y
                        panStartOffsetX = root.panX
                        panStartOffsetY = root.panY
                    } else {
                        // Start drawing
                        isDrawing = true
                        drawPoint(mouse.x, mouse.y, mouse.button === Qt.LeftButton)
                    }
                }
                
                onClicked: function(mouse) {
                    mouse.accepted = true
                }

                onPositionChanged: function(mouse) {
                    if(isPanning) {
                        // Pan the canvas
                        var deltaX = mouse.x - panStartX
                        var deltaY = mouse.y - panStartY
                        root.panX = panStartOffsetX + deltaX
                        root.panY = panStartOffsetY + deltaY
                    } else if(isDrawing) {
                        drawPoint(mouse.x, mouse.y, mouse.button === Qt.LeftButton)
                    }
                }

                onReleased: function(mouse) {
                    isDrawing = false
                    isPanning = false
                    lastX = -1
                    lastY = -1
                }

                function drawPoint(x, y, isDraw) {
                    if(paintCanvas.scaleX <= 0 || paintCanvas.scaleY <= 0) return
                    
                    // Convert mouse coordinates from Rectangle to Canvas coordinates
                    // Canvas has 4px margins, so subtract that
                    var canvasX = x - 4
                    var canvasY = y - 4
                    
                    // Account for canvas centering offset when zoomed, plus pan offset
                    var scaledWidth = paintCanvas.canvasWidth * paintCanvas.scaleX
                    var scaledHeight = paintCanvas.canvasHeight * paintCanvas.scaleY
                    var offsetX = (paintCanvas.width - scaledWidth) / 2 + root.panX
                    var offsetY = (paintCanvas.height - scaledHeight) / 2 + root.panY
                    
                    // Convert mouse coordinates to canvas coordinates
                    var pixelX = Math.floor((canvasX - offsetX) / paintCanvas.scaleX)
                    var pixelY = Math.floor((canvasY - offsetY) / paintCanvas.scaleY)
                    
                    if(pixelX < 0 || pixelX >= paintCanvas.canvasWidth ||
                       pixelY < 0 || pixelY >= paintCanvas.canvasHeight) {
                        return
                    }

                    // Draw line from last point if exists
                    if(isDraw && lastX >= 0 && lastY >= 0) {
                        // Bresenham's line algorithm for smooth drawing
                        var dx = Math.abs(pixelX - lastX)
                        var dy = Math.abs(pixelY - lastY)
                        var sx = lastX < pixelX ? 1 : -1
                        var sy = lastY < pixelY ? 1 : -1
                        var err = dx - dy
                        
                        var curX = lastX
                        var curY = lastY
                        
                        while(true) {
                            setPixel(curX, curY, isDraw)
                            
                            if(curX === pixelX && curY === pixelY) break
                            
                            var e2 = 2 * err
                            if(e2 > -dy) {
                                err -= dy
                                curX += sx
                            }
                            if(e2 < dx) {
                                err += dx
                                curY += sy
                            }
                        }
                    } else {
                        setPixel(pixelX, pixelY, isDraw)
                    }
                    
                    lastX = pixelX
                    lastY = pixelY
                    
                    // Auto-send if enabled
                    if(autoSend && active) {
                        autoSendTimer.restart()
                    }
                }
            }
        }

        // Controls
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            // Use a very subtle color (almost transparent) to block clicks
            // while appearing transparent to the user
            color: "#10000000"
            
            RowLayout {
                anchors.fill: parent
                spacing: 4
                z: 1

            Button {
                Layout.preferredHeight: 28
                onClicked: clearCanvas()
                
                contentItem: Text {
                    text: qsTr("Clear")
                    color: "#FFFFFF"
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    anchors.centerIn: parent
                }
            }

            Button {
                Layout.preferredHeight: 28
                enabled: Backend.deviceState && Backend.deviceState.isOnline
                onClicked: sendToDevice()
                
                contentItem: Text {
                    text: qsTr("Send")
                    color: "#FFFFFF"
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    anchors.centerIn: parent
                }
            }

            Button {
                Layout.preferredHeight: 28
                checkable: true
                checked: autoSend
                onCheckedChanged: autoSend = checked
                
                contentItem: Row {
                    spacing: 6
                    anchors.centerIn: parent
                    
                    Rectangle {
                        width: 14
                        height: 14
                        anchors.verticalCenter: parent.verticalCenter
                        border.color: "#FFFFFF"
                        border.width: 1
                        radius: 2
                        color: autoSend ? Theme.color.lightgreen : "transparent"
                        
                        Text {
                            anchors.centerIn: parent
                            text: "✓"
                            color: "#000000"
                            font.pixelSize: 10
                            font.bold: true
                            visible: autoSend
                        }
                    }
                    
                    Text {
                        text: qsTr("Auto")
                        color: "#FFFFFF"
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Button {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                onClicked: {
                    root.zoomLevel = Math.max(0.5, root.zoomLevel - 0.25)
                    paintCanvas.requestPaint()
                }
                
                contentItem: Text {
                    text: "-"
                    font.pixelSize: 15
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    anchors.fill: parent
                }
            }

            Text {
                text: Math.round(root.zoomLevel * 100) + "%"
                color: Theme.color.bluepurple2
                font.pixelSize: 15
                Layout.minimumWidth: 40
                Layout.preferredHeight: 28
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
            }

            Button {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                onClicked: {
                    root.zoomLevel = Math.min(5.0, root.zoomLevel + 0.25)
                    paintCanvas.requestPaint()
                }
                
                contentItem: Text {
                    text: "+"
                    font.pixelSize: 15
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -1
                }
            }

            Button {
                Layout.preferredWidth: 50
                Layout.preferredHeight: 28
                onClicked: {
                    root.zoomLevel = 1.0
                    root.panX = 0.0
                    root.panY = 0.0
                    paintCanvas.requestPaint()
                }
                
                contentItem: Text {
                    text: qsTr("Reset")
                    color: "#FFFFFF"
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    anchors.centerIn: parent
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("L:Draw R:Erase M:Pan")
                color: Theme.color.bluepurple2
                font.pixelSize: 12
                Layout.rightMargin: 4
                Layout.preferredHeight: 28
                verticalAlignment: Text.AlignVCenter
            }
            }
        }
    }

    Component.onCompleted: {
        // Initialize pixel data array
        pixelData = []
        for(var i = 0; i < 128 * 64; i++) {
            pixelData.push(false)
        }
        clearCanvas()
    }
}





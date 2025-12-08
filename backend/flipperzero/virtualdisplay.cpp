#include "virtualdisplay.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QVariant>

#include "flipperzero.h"
#include "devicestate.h"
#include "protobufsession.h"

#include "rpc/guistartvirtualdisplayoperation.h"
#include "rpc/guistopvirtualdisplayoperation.h"
#include "rpc/guiscreenframeoperation.h"

Q_LOGGING_CATEGORY(LOG_VIRTDISPLAY, "DPY")

using namespace Flipper;
using namespace Zero;

VirtualDisplay::VirtualDisplay(QObject *parent):
    QObject(parent),
    m_displayState(DisplayState::Stopped),
    m_device(nullptr)
{}

void VirtualDisplay::setDevice(FlipperZero *device)
{
    if(device == m_device) {
        return;
    }

    m_device = device;

    if(device) {
        connect(device->rpc(), &ProtobufSession::sessionStateChanged, this, &VirtualDisplay::onProtobufSessionStateChanged);
    }
}

VirtualDisplay::DisplayState VirtualDisplay::displayState() const
{
    return m_displayState;
}

void VirtualDisplay::start(const QByteArray &firstFrame)
{
    if(m_displayState != DisplayState::Stopped) {
        qCDebug(LOG_VIRTDISPLAY) << "Cannot start virtual display: already in state" << m_displayState;
        return;
    }

    if(!m_device) {
        qCWarning(LOG_VIRTDISPLAY) << "Cannot start virtual display: no device";
        return;
    }

    if(!m_device->rpc()->isSessionUp()) {
        qCWarning(LOG_VIRTDISPLAY) << "Cannot start virtual display: RPC session not up";
        return;
    }

    qCDebug(LOG_VIRTDISPLAY) << "Starting virtual display with frame size:" << firstFrame.size();
    setDisplayState(DisplayState::Starting);

    auto *operation = m_device->rpc()->guiStartVirtualDisplay(firstFrame);
    connect(operation, &AbstractOperation::finished, this, [=]() {
        if(operation->isError()) {
            qCWarning(LOG_VIRTDISPLAY).noquote() << "Failed to start virtual display:" << operation->errorString();
            setDisplayState(DisplayState::Stopped);
        } else {
            qCDebug(LOG_VIRTDISPLAY) << "Virtual display started successfully";
            setDisplayState(DisplayState::Running);
        }
    });
}

void VirtualDisplay::startFromArray(const QVariantList &frameData)
{
    qCDebug(LOG_VIRTDISPLAY) << "startFromArray called with" << frameData.size() << "bytes";
    
    QByteArray byteArray;
    if(!frameData.isEmpty()) {
        byteArray.reserve(frameData.size());
        int invalidCount = 0;
        for(const QVariant &value : frameData) {
            bool ok;
            int byteValue = value.toInt(&ok);
            if(ok && byteValue >= 0 && byteValue <= 255) {
                char c = static_cast<char>(byteValue);
                byteArray.append(c);
            } else {
                invalidCount++;
                byteArray.append(static_cast<char>(0));
            }
        }
        if(invalidCount > 0) {
            qCWarning(LOG_VIRTDISPLAY) << "Found" << invalidCount << "invalid byte values in start frame data";
        }
    }
    
    qCDebug(LOG_VIRTDISPLAY) << "Converted to QByteArray, size:" << byteArray.size();
    start(byteArray);
}

void VirtualDisplay::sendFrame(const QByteArray &screenFrame)
{
    if(m_displayState != DisplayState::Running) {
        qCWarning(LOG_VIRTDISPLAY) << "Cannot send frame: virtual display not running (state:" << m_displayState << ")";
        return;
    }

    if(!m_device) {
        qCWarning(LOG_VIRTDISPLAY) << "Cannot send frame: no device";
        return;
    }

    if(!m_device->rpc()->isSessionUp()) {
        qCWarning(LOG_VIRTDISPLAY) << "Cannot send frame: RPC session not up";
        return;
    }

    if(screenFrame.size() != 1024) {
        qCWarning(LOG_VIRTDISPLAY) << "Invalid frame size:" << screenFrame.size() << "(expected 1024)";
        return;
    }

    qCDebug(LOG_VIRTDISPLAY) << "Sending screen frame, size:" << screenFrame.size();
    auto *operation = m_device->rpc()->guiSendScreenFrame(screenFrame);

    connect(operation, &AbstractOperation::finished, this, [=]() {
        if(operation->isError()) {
            qCWarning(LOG_VIRTDISPLAY).noquote() << "Failed to send screen frame:" << operation->errorString();
        } else {
            qCDebug(LOG_VIRTDISPLAY) << "Screen frame sent successfully";
        }
    });
}

void VirtualDisplay::sendFrameFromArray(const QVariantList &frameData)
{
    qCDebug(LOG_VIRTDISPLAY) << "sendFrameFromArray called with" << frameData.size() << "bytes";
    
    if(frameData.isEmpty()) {
        qCWarning(LOG_VIRTDISPLAY) << "Cannot send frame: empty frame data";
        return;
    }

    QByteArray byteArray;
    byteArray.reserve(frameData.size());
    
    int invalidCount = 0;
    for(const QVariant &value : frameData) {
        bool ok;
        int byteValue = value.toInt(&ok);
        if(ok && byteValue >= 0 && byteValue <= 255) {
            char c = static_cast<char>(byteValue);
            byteArray.append(c);
        } else {
            invalidCount++;
            byteArray.append(static_cast<char>(0));
        }
    }
    
    if(invalidCount > 0) {
        qCWarning(LOG_VIRTDISPLAY) << "Found" << invalidCount << "invalid byte values in frame data";
    }
    
    qCDebug(LOG_VIRTDISPLAY) << "Converted to QByteArray, size:" << byteArray.size();
    sendFrame(byteArray);
}

void VirtualDisplay::stop()
{
    if(m_displayState != DisplayState::Running) {
        return;
    }

    setDisplayState(DisplayState::Stopping);

    auto *operation = m_device->rpc()->guiStopVirtualDisplay();

    connect(operation, &AbstractOperation::finished, this, [=]() {
        if(operation->isError()) {
            qCDebug(LOG_VIRTDISPLAY).noquote() << "Failed to stop virtual display:" << operation->errorString();
        }

        setDisplayState(DisplayState::Stopped);
    });
}

void VirtualDisplay::onProtobufSessionStateChanged()
{
    if(!m_device->rpc()->isSessionUp()) {
        setDisplayState(Stopped);
    }
}

void VirtualDisplay::setDisplayState(DisplayState newState)
{
    if(newState == m_displayState) {
        return;
    }

    m_displayState = newState;
    emit displayStateChanged();
}

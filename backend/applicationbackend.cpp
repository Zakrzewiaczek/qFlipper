#include "applicationbackend.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QEventLoop>
#include <QPointer>
#include <QSerialPortInfo>
#include <QSerialPort>
#include <functional>

#include "serialfinder.h"

#include "logger.h"
#include "deviceregistry.h"
#include "firmwareupdateregistry.h"

#include "preferences.h"
#include "flipperupdates.h"

#include "flipperzero/screenstreamer.h"
#include "flipperzero/virtualdisplay.h"
#include "flipperzero/filemanager.h"
#include "flipperzero/protobufsession.h"
#include "flipperzero/rpc/systemrebootoperation.h"
#include "flipperzero/rpc/abstractprotobufoperation.h"

#include "flipperzero/flipperzero.h"
#include "flipperzero/devicestate.h"
#include "flipperzero/assetmanifest.h"
#include "flipperzero/utilityinterface.h"
#include "flipperzero/utility/abstractutilityoperation.h"
#include "flipperzero/utility/storageinforefreshoperation.h"
#include "abstractoperation.h"

#include "flipperzero/helper/toplevelhelper.h"

#include "flipperzero/pixmaps/updateok.h"
#include "flipperzero/pixmaps/updating.h"

Q_LOGGING_CATEGORY(LOG_BACKEND, "BKD")
Q_DECLARE_METATYPE(QAbstractListModel*)

// Forward declaration to avoid namespace conflicts
class AssetPacks;
extern AssetPacks *globalAssetPacks;

using namespace Flipper;
using namespace Zero;

ApplicationBackend::ApplicationBackend(QObject *parent):
    QObject(parent),
    m_deviceRegistry(new DeviceRegistry(this)),
    m_firmwareUpdateRegistry(new FirmwareUpdateRegistry("https://up.momentum-fw.dev/firmware/directory.json", this)),
    m_screenStreamer(new ScreenStreamer(this)),
    m_virtualDisplay(new VirtualDisplay(this)),
    m_fileManager(new FileManager(this)),
    m_backendState(BackendState::WaitingForDevices),
    m_errorType(BackendError::UnknownError)
{
    registerMetaTypes();
#if QT_VERSION < 0x060000
    registerComparators();
#endif

    initLibraryPaths();
    initConnections();
}
bool ApplicationBackend::cliActive() const
{
    return m_cliActive;
}

void ApplicationBackend::setCliActive(bool active)
{
    if (m_cliActive == active) return;
    m_cliActive = active;
    emit cliActiveChanged();
}

bool ApplicationBackend::isSwitchingMode() const
{
    return m_isSwitchingMode;
}

ApplicationBackend::BackendState ApplicationBackend::backendState() const
{
    return m_backendState;
}

BackendError::ErrorType ApplicationBackend::errorType() const
{
    return m_errorType;
}

ApplicationBackend::FirmwareUpdateState ApplicationBackend::firmwareUpdateState() const
{
    if(!device() || (m_firmwareUpdateRegistry->state() == UpdateRegistry::State::Unknown)) {
        return FirmwareUpdateState::Unknown;
    } else if(m_firmwareUpdateRegistry->state() == UpdateRegistry::State::Checking) {
        return FirmwareUpdateState::Checking;
    } else if(m_firmwareUpdateRegistry->state() == UpdateRegistry::State::ErrorOccured) {
        return FirmwareUpdateState::ErrorOccured;
    }

    const auto &latestVersion = m_firmwareUpdateRegistry->latestVersion();

    if (device()->canRepair(latestVersion)) {
        return FirmwareUpdateState::CanRepair;
    } else if(device()->canUpdate(latestVersion)) {
        return FirmwareUpdateState::CanUpdate;
    } else if(device()->canInstall(latestVersion)) {
        return FirmwareUpdateState::CanInstall;
    } else{
        return FirmwareUpdateState::NoUpdates;
    }
}

QAbstractListModel *ApplicationBackend::firmwareUpdateModel() const
{
    return m_firmwareUpdateRegistry;
}

FlipperZero *ApplicationBackend::device() const
{
    return m_deviceRegistry->currentDevice();
}

DeviceState *ApplicationBackend::deviceState() const
{
    if(device()) {
        return device()->deviceState();
    } else {
        return nullptr;
    }
}

DeviceRegistry *ApplicationBackend::deviceRegistry() const
{
    return m_deviceRegistry;
}

ScreenStreamer *ApplicationBackend::screenStreamer() const
{
    return m_screenStreamer;
}

VirtualDisplay *ApplicationBackend::virtualDisplay() const
{
    return m_virtualDisplay;
}

FileManager *ApplicationBackend::fileManager() const
{
    return m_fileManager;
}

const Updates::VersionInfo ApplicationBackend::latestFirmwareVersion() const
{
    return m_firmwareUpdateRegistry->latestVersion();
}

bool ApplicationBackend::isQueryInProgress() const
{
    return m_deviceRegistry->isQueryInProgress();
}

void ApplicationBackend::mainAction()
{
    AbstractOperationHelper *helper;

       if(device()->deviceState()->isRecoveryMode()) {
           setBackendState(BackendState::RepairingDevice);
           helper = new RepairTopLevelHelper(m_firmwareUpdateRegistry, device(), this);

       } else {
           setBackendState(BackendState::UpdatingDevice);
           helper = new UpdateTopLevelHelper(m_firmwareUpdateRegistry, device(), this);
       }

       connect(helper, &AbstractOperationHelper::finished, helper, &QObject::deleteLater);
}

void ApplicationBackend::createBackup(const QUrl &backupUrl)
{
    setBackendState(BackendState::CreatingBackup);
    device()->createBackup(backupUrl);
}

void ApplicationBackend::restoreBackup(const QUrl &backupUrl)
{
    setBackendState(BackendState::RestoringBackup);
    device()->restoreBackup(backupUrl);
}

void ApplicationBackend::factoryReset()
{
    setBackendState(BackendState::FactoryResetting);
    device()->factoryReset();
}

void ApplicationBackend::installFirmware(const QUrl &fileUrl)
{
    setBackendState(BackendState::InstallingFirmware);
    device()->installFirmware(fileUrl);
}

void ApplicationBackend::installWirelessStack(const QUrl &fileUrl)
{
    setBackendState(BackendState::InstallingWirelessStack);
    device()->installWirelessStack(fileUrl);
}

void ApplicationBackend::installFUS(const QUrl &fileUrl, uint32_t address)
{
    setBackendState(BackendState::InstallingFUS);
    device()->installFUS(fileUrl, address);
}

void ApplicationBackend::startFullScreenStreaming()
{
    setBackendState(BackendState::ScreenStreaming);
}

void ApplicationBackend::stopFullScreenStreaming()
{
    setBackendState(BackendState::Ready);
}

void ApplicationBackend::refreshStorageInfo()
{
    device()->refreshStorageInfo();
}

void ApplicationBackend::checkFirmwareUpdates()
{
    m_firmwareUpdateRegistry->check();
}

void ApplicationBackend::finalizeOperation()
{
    qCDebug(LOG_BACKEND) << "Finalized current operation";

    globalLogger->setErrorCount(0);

    m_deviceRegistry->removeOfflineDevices();
    m_deviceRegistry->clearError();

    if(!device()) {
        setBackendState(BackendState::WaitingForDevices);

    } else {
        device()->finalizeOperation();

        if(!deviceState()->isRecoveryMode()) {
            if(deviceState()->isAllowVirtualDisplay()) {
                m_virtualDisplay->stop();
            }

            m_screenStreamer->start();

            m_fileManager->reset();
            m_fileManager->refresh();
        }

        setBackendState(BackendState::Ready);
    }
}

void ApplicationBackend::enterCliMode()
{
    if (!device()) {
        qCDebug(LOG_BACKEND) << "Cannot enter CLI mode: no device";
        return;
    }
    
    // Prevent rapid mode switching
    if (m_isSwitchingMode) {
        qCDebug(LOG_BACKEND) << "Already switching modes, ignoring enterCliMode";
        return;
    }
    
    m_isSwitchingMode = true;
    emit isSwitchingModeChanged();
    
    setCliActive(true);
    device()->setRpcAutostartEnabled(false);
    
    // Stop screenstreamer FIRST to avoid "operations still running" error
    auto *rpc = device()->rpc();
    if (m_screenStreamer && m_screenStreamer->streamState() != ScreenStreamer::Stopped && rpc && rpc->isSessionUp()) {
        qCDebug(LOG_BACKEND) << "Stopping screenstreamer before CLI mode";
        
        // Use a QEventLoop to wait for the stop operation to complete
        QEventLoop stopLoop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        timeoutTimer.setInterval(2000); // 2 second timeout
        
        auto conn = QObject::connect(m_screenStreamer, &ScreenStreamer::streamStateChanged, &stopLoop, [&stopLoop, this]() {
            if (m_screenStreamer->streamState() == ScreenStreamer::Stopped) {
                stopLoop.quit();
            }
        });
        
        QObject::connect(&timeoutTimer, &QTimer::timeout, &stopLoop, &QEventLoop::quit);
        
        m_screenStreamer->stop();
        timeoutTimer.start();
        stopLoop.exec();
        
        QObject::disconnect(conn);
        
        if (m_screenStreamer->streamState() == ScreenStreamer::Stopped) {
            qCDebug(LOG_BACKEND) << "Screenstreamer stopped successfully";
        } else {
            qCWarning(LOG_BACKEND) << "Screenstreamer stop timed out, proceeding anyway";
        }
    }
    
    // Stop RPC session only if currently up
    if (rpc && rpc->isSessionUp()) {
        m_waitingRpcStop = true;
        qCDebug(LOG_BACKEND) << "Stopping RPC session for CLI mode";
        rpc->stopSession();
        
        // Wait for RPC to actually stop (stopSession is async)
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 1000 && rpc->isSessionUp()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        m_waitingRpcStop = false;
        
        if (rpc->isSessionUp()) {
            qCWarning(LOG_BACKEND) << "RPC failed to stop within timeout";
        } else {
            qCDebug(LOG_BACKEND) << "RPC session stopped successfully, port released";
        }
    }
    
    m_isSwitchingMode = false;
    emit isSwitchingModeChanged();
}

void ApplicationBackend::exitCliMode()
{
    if (!device()) {
        qCDebug(LOG_BACKEND) << "Cannot exit CLI mode: no device";
        return;
    }
    
    // Prevent rapid mode switching
    if (m_isSwitchingMode) {
        qCDebug(LOG_BACKEND) << "Already switching modes, queueing exitCliMode";
        QTimer::singleShot(350, this, &ApplicationBackend::exitCliMode);
        return;
    }
    
    m_isSwitchingMode = true;
    emit isSwitchingModeChanged();
    
    setCliActive(false);
    device()->setRpcAutostartEnabled(true);
    
    auto *rpc = device()->rpc();
    if (!rpc) {
        m_isSwitchingMode = false;
        emit isSwitchingModeChanged();
        return;
    }
    
    qCInfo(LOG_BACKEND) << "Exiting CLI mode, immediately switching to RPC";
    
    // Ensure device state is reset - mark offline temporarily so RPC can properly reconnect
    if (deviceState() && deviceState()->isOnline()) {
        deviceState()->setOnline(false);
    }
    
    // Start RPC IMMEDIATELY - no delays, mimic rapid switching behavior
    // Stop any stale session and start fresh right away
    if (rpc->isSessionUp()) {
        rpc->stopSession();
    }
    
    // Use Qt.callLater equivalent to start RPC in the next event loop iteration
    // This ensures CLI disconnect completes first, but starts RPC immediately after
    QTimer::singleShot(0, this, [this, rpc]() {
        if (!device() || m_cliActive || !rpc) {
            m_isSwitchingMode = false;
            emit isSwitchingModeChanged();
            return;
        }

        qCInfo(LOG_BACKEND) << "Starting RPC session immediately (no delays)";
        rpc->startSession();
        
        // Check if it started - but don't wait long
        QTimer::singleShot(100, this, [this, rpc]() {
            if (!device() || m_cliActive || !rpc) {
                m_isSwitchingMode = false;
                emit isSwitchingModeChanged();
                return;
            }
            
            if (rpc->isSessionUp()) {
                qCInfo(LOG_BACKEND) << "RPC session started successfully";
            } else {
                qCWarning(LOG_BACKEND) << "RPC session did not start, retrying once";
                // Single retry with minimal delay
                QTimer::singleShot(100, this, [this, rpc]() {
                    if (device() && !m_cliActive && rpc && !rpc->isSessionUp()) {
                        rpc->startSession();
                    }
                    m_isSwitchingMode = false;
                    emit isSwitchingModeChanged();
                });
                return;
            }
            
            m_isSwitchingMode = false;
            emit isSwitchingModeChanged();
        });
    });
}

void ApplicationBackend::startRpcImmediately(Flipper::Zero::ProtobufSession *rpc)
{
    if (!device() || m_cliActive || !rpc) {
        m_isSwitchingMode = false;
        emit isSwitchingModeChanged();
        return;
    }
    
    qCInfo(LOG_BACKEND) << "Starting RPC session immediately";
    
    // Try to start RPC right away - don't wait for port release
    // If it fails, retry a few times with short delays
    if (!rpc->isSessionUp()) {
        rpc->startSession();
        
        // Check if it started successfully after a brief wait
        QTimer::singleShot(300, this, [this, rpc]() {
            if (!device() || m_cliActive || !rpc) {
                m_isSwitchingMode = false;
                emit isSwitchingModeChanged();
                return;
            }
            
            if (rpc->isSessionUp()) {
                qCInfo(LOG_BACKEND) << "RPC session started successfully";
                m_isSwitchingMode = false;
                emit isSwitchingModeChanged();
            } else {
                qCWarning(LOG_BACKEND) << "RPC session did not start, retrying";
                // Retry once more
                QTimer::singleShot(200, this, [this, rpc]() {
                    if (!device() || m_cliActive || !rpc) {
                        m_isSwitchingMode = false;
                        emit isSwitchingModeChanged();
                        return;
                    }
                    
                    if (!rpc->isSessionUp()) {
                        rpc->startSession();
                    }
                    
                    // Final check
                    QTimer::singleShot(500, this, [this, rpc]() {
                        m_isSwitchingMode = false;
                        emit isSwitchingModeChanged();
                    });
                });
            }
        });
    } else {
        qCInfo(LOG_BACKEND) << "RPC session already running";
        m_isSwitchingMode = false;
        emit isSwitchingModeChanged();
    }
}

void ApplicationBackend::proceedWithRpcRestart(Flipper::Zero::ProtobufSession *rpc)
{
    if (!device() || m_cliActive || !rpc) {
        m_isSwitchingMode = false;
        emit isSwitchingModeChanged();
        return;
    }
    
    // Rescan port to get fresh port info
    rescanDevicePort();
    
    // Force device info update to trigger RPC restart
    QTimer::singleShot(300, this, [this, rpc]() {
        if (!device() || m_cliActive || !rpc) {
            m_isSwitchingMode = false;
            emit isSwitchingModeChanged();
            return;
        }
        
        if (deviceState()) {
            deviceState()->setDeviceInfo(deviceState()->deviceInfo());
        }
        
        // If RPC still hasn't started, start it manually
        QTimer::singleShot(500, this, [this, rpc]() {
            if (!m_cliActive && rpc) {
                if (rpc->isSessionUp()) {
                    qCInfo(LOG_BACKEND) << "RPC session already running";
                } else {
                    qCInfo(LOG_BACKEND) << "RPC session not started automatically, starting manually";
                    rpc->startSession();
                }
            }
            
            m_isSwitchingMode = false;
            emit isSwitchingModeChanged();
        });
    });
}

void ApplicationBackend::rescanDevicePort()
{
    if (!device() || !deviceState()) {
        return;
    }

    // Re-run serial finder based on current USB serial number to refresh portInfo
    const auto serial = deviceState()->deviceInfo().usbInfo.serialNumber();
    if (serial.isEmpty()) {
        return;
    }

    auto *finder = new SerialFinder(serial, this);
    finder->setNumberOfTries(10);
    finder->setTryPeriod(100);
    connect(finder, &SerialFinder::finished, this, [this, finder](const QSerialPortInfo &portInfo) {
        finder->deleteLater();
        if (portInfo.isNull()) {
            return;
        }
        // Update device info with new port and re-kick RPC
        auto info = deviceState()->deviceInfo();
        info.portInfo = portInfo;
        info.systemLocation = portInfo.systemLocation();
        deviceState()->setDeviceInfo(info);
    });

    // Direct call; SerialFinder manages its own timer
    QMetaObject::invokeMethod(finder, "findMatchingPort", Qt::QueuedConnection);
}

void ApplicationBackend::restartDeviceAfterCli()
{
    if (!device()) {
        return;
    }

    if (deviceState()) {
        deviceState()->setOnline(false);
    }

    // Force RPC stop (no-op if already stopped)
    if (device()->rpc()) {
        device()->rpc()->stopSession();
    }

    // Re-emit deviceInfoChanged on current device to trigger FlipperZero::onDeviceInfoChanged logic
    if (deviceState()) {
        deviceState()->setDeviceInfo(deviceState()->deviceInfo());
    }
}

void ApplicationBackend::onCurrentDeviceChanged()
{
    // Should not happen during an ongoing operation
    if(m_backendState > BackendState::ScreenStreaming && m_backendState < BackendState::Finished) {
        setBackendState(BackendState::ErrorOccured);

        qCDebug(LOG_BACKEND) << "Current operation was interrupted";

    } else if(device()) {
        qCDebug(LOG_BACKEND) << "Current device changed to" << device()->deviceState()->deviceInfo().name;
        // No need to disconnect the old device, as it has been destroyed at this point
        connect(device(), &FlipperZero::operationFinished, this, &ApplicationBackend::onDeviceOperationFinished);
        connect(device(), &FlipperZero::deviceStateChanged, this, &ApplicationBackend::firmwareUpdateStateChanged);

        connect(deviceState(), &DeviceState::deviceInfoChanged, this, &ApplicationBackend::onDeviceInfoChanged);
        connect(deviceState(), &DeviceState::isPersistentChanged, this, &ApplicationBackend::onDeviceInfoChanged);

        onDeviceInfoChanged();

        if(!deviceState()->isRecoveryMode()) {
            connect(m_screenStreamer, &ScreenStreamer::streamStateChanged, this, &ApplicationBackend::onScreenStreamerStateChanged);
            if (!m_cliActive) {
                m_screenStreamer->start();
            }

        } else {
            setBackendState(BackendState::Ready);
        }

    } else {
        qCDebug(LOG_BACKEND) << "Last device was disconnected";
        setBackendState(BackendState::WaitingForDevices);
    }
}

void ApplicationBackend::onDeviceInfoChanged()
{
    // Do not (re)initialize RPC while CLI holds the serial port
    if (m_cliActive) {
        qCDebug(LOG_BACKEND) << "CLI active; skipping RPC init on device info change";
        return;
    }
    if(deviceState()->isRecoveryMode()) {
        return;
    }

    m_fileManager->setDevice(device());
    m_screenStreamer->setDevice(device());
    m_virtualDisplay->setDevice(device());

    if(deviceState()->isPersistent() && deviceState()->isAllowVirtualDisplay()) {
        m_virtualDisplay->start(QByteArray((char*)updating_bits, sizeof(updating_bits)));
    }
}

void ApplicationBackend::onDeviceOperationFinished()
{
    if(!device()) {
        qCDebug(LOG_BACKEND) << "Lost all connected devices";
        setErrorType(BackendError::UnknownError);
        setBackendState(BackendState::ErrorOccured);

    } else if(device()->deviceState()->isError()) {
        qCDebug(LOG_BACKEND) << "Current operation finished with error:" << device()->deviceState()->errorString();
        setErrorType(device()->deviceState()->error());
        setBackendState(BackendState::ErrorOccured);

    } else {
        // TODO: Replace with state check
        if(deviceState()->isAllowVirtualDisplay()) {
            m_virtualDisplay->sendFrame(QByteArray((char*)update_ok_bits, sizeof(update_ok_bits)));
        }

        setBackendState(BackendState::Finished);
    }
}

void ApplicationBackend::onDeviceRegistryErrorOccured()
{
    if(m_backendState != BackendState::WaitingForDevices) {
        return;
    }

    const auto err = m_deviceRegistry->error();

    if(err != BackendError::NoError) {
        setErrorType(err);
        setBackendState(BackendState::ErrorOccured);
    }
}

void ApplicationBackend::onFileManagerErrorOccured()
{
    const auto err = m_fileManager->error();

    if(err != BackendError::NoError) {
        setErrorType(m_fileManager->error());
        setBackendState(BackendState::ErrorOccured);
    }
}

void ApplicationBackend::onScreenStreamerStateChanged()
{
    if(m_screenStreamer->isEnabled()) {
        disconnect(m_screenStreamer, &ScreenStreamer::streamStateChanged, this, &ApplicationBackend::onScreenStreamerStateChanged);
        setBackendState(BackendState::Ready);
    }
    // TODO: check for ScreenStreamer errors
}

void ApplicationBackend::onRpcSessionStateChanged()
{
    // Ensure screen streaming only starts when CLI is inactive
    if (!device() || !device()->rpc()) {
        return;
    }
    if (device()->rpc()->isSessionUp() && !m_cliActive) {
        // Ensure device state is marked online when RPC reconnects
        if (deviceState() && !deviceState()->isOnline()) {
            deviceState()->setOnline(true);
        }
        
        // Refresh storage info to update device information after RPC reconnects
        QTimer::singleShot(200, this, [this]() {
            if (device() && device()->rpc() && device()->rpc()->isSessionUp() && !m_cliActive) {
                if (deviceState() && !deviceState()->isRecoveryMode()) {
                    // Refresh storage info
                    refreshStorageInfo();
                    
                    // Force device reset for screenstreamer to re-establish broadcast connections
                    // setDevice returns early if device is the same, so we need to reset it
                    if (m_screenStreamer) {
                        m_screenStreamer->setDevice(nullptr);
                        m_screenStreamer->setDevice(device());
                        
                        // Start screenstreamer after device reset
                        m_screenStreamer->start();
                    }
                    
                    // Ensure backend state is Ready
                    setBackendState(BackendState::Ready);
                }
            }
        });
    }
}

void ApplicationBackend::initLibraryPaths()
{
    const auto appPath = qApp->applicationDirPath();
    qApp->addLibraryPath(QStringLiteral("%1/plugins").arg(appPath));

#if defined Q_OS_LINUX
    qApp->addLibraryPath(QStringLiteral("%1/../lib/%2/plugins").arg(appPath, APP_NAME));
#elif defined Q_OS_MAC
#endif
}

void ApplicationBackend::initConnections()
{
    connect(m_deviceRegistry, &DeviceRegistry::currentDeviceChanged, this, &ApplicationBackend::onCurrentDeviceChanged);
    connect(m_deviceRegistry, &DeviceRegistry::currentDeviceChanged, this, &ApplicationBackend::currentDeviceChanged);

    connect(m_deviceRegistry, &DeviceRegistry::currentDeviceChanged, this, &ApplicationBackend::firmwareUpdateStateChanged);
    connect(m_deviceRegistry, &DeviceRegistry::isQueryInProgressChanged, this, &ApplicationBackend::isQueryInProgressChanged);
    connect(m_firmwareUpdateRegistry, &UpdateRegistry::latestVersionChanged, this, &ApplicationBackend::firmwareUpdateStateChanged);

    connect(m_deviceRegistry, &DeviceRegistry::errorOccured, this, &ApplicationBackend::onDeviceRegistryErrorOccured);
    connect(m_fileManager, &FileManager::errorOccured, this, &ApplicationBackend::onFileManagerErrorOccured);

    // Track RPC session changes to coordinate with CLI
    if (device() && device()->rpc()) {
        connect(device()->rpc(), &ProtobufSession::sessionStateChanged, this, &ApplicationBackend::onRpcSessionStateChanged);
    }
}

void ApplicationBackend::beginUpdate()
{
    setBackendState(BackendState::UpdatingDevice);
    auto *helper = new UpdateTopLevelHelper(m_firmwareUpdateRegistry, device(), this);
    connect(helper, &AbstractOperationHelper::finished, helper, &QObject::deleteLater);
}

void ApplicationBackend::beginRepair()
{
    setBackendState(BackendState::RepairingDevice);
    auto *helper = new RepairTopLevelHelper(m_firmwareUpdateRegistry, device(), this);
    connect(helper, &AbstractOperationHelper::finished, helper, &QObject::deleteLater);
}

void ApplicationBackend::setBackendState(BackendState newState)
{
    if(m_backendState == newState) {
        return;
    }

    m_backendState = newState;
    emit backendStateChanged();
}

void ApplicationBackend::setErrorType(BackendError::ErrorType newErrorType)
{
    if(m_errorType == newErrorType) {
        return;
    }

    m_errorType = newErrorType;
    emit errorTypeChanged();
}

void ApplicationBackend::registerMetaTypes()
{
    qRegisterMetaType<Preferences*>("Preferences*");
    qRegisterMetaType<Flipper::Updates::FileInfo>("Flipper::Updates::FileInfo");
    qRegisterMetaType<Flipper::Updates::VersionInfo>("Flipper::Updates::VersionInfo");
    qRegisterMetaType<Flipper::Updates::ChannelInfo>("Flipper::Updates::ChannelInfo");

    qRegisterMetaType<Flipper::Zero::DeviceInfo>("Flipper::Zero::DeviceInfo");
    qRegisterMetaType<Flipper::Zero::HardwareInfo>("Flipper::Zero::HardwareInfo");
    qRegisterMetaType<Flipper::Zero::SoftwareInfo>("Flipper::Zero::SoftwareInfo");
    qRegisterMetaType<Flipper::Zero::StorageInfo>("Flipper::Zero::StorageInfo");

    qRegisterMetaType<Flipper::FlipperZero*>("Flipper::FlipperZero*");
    qRegisterMetaType<Flipper::Zero::DeviceState*>("Flipper::Zero::DeviceState*");
    qRegisterMetaType<Flipper::Zero::ScreenStreamer*>("Flipper::Zero::ScreenStreamer*");
    qRegisterMetaType<Flipper::Zero::VirtualDisplay*>("Flipper::Zero::VirtualDisplay*");
    qRegisterMetaType<Flipper::Zero::FileManager*>("Flipper::Zero::FileManager*");
    qRegisterMetaType<Flipper::Zero::ScreenStreamer*>("Flipper::Zero::ScreenStreamer*");

    qRegisterMetaType<Flipper::Zero::AssetManifest::FileInfo>();

    qRegisterMetaType<QAbstractListModel*>();

    qRegisterMetaType<InputEvent::Key>();
    qRegisterMetaType<InputEvent::Type>();
    qRegisterMetaType<AsciiEvent::Value>();
}

#if QT_VERSION < 0x060000
void ApplicationBackend::registerComparators()
{
    QMetaType::registerComparators<Flipper::Zero::AssetManifest::FileInfo>();
}
#endif

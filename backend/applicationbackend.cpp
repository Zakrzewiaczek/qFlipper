#include "applicationbackend.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QSerialPortInfo>

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

#include "flipperzero/flipperzero.h"
#include "flipperzero/devicestate.h"
#include "flipperzero/assetmanifest.h"
#include "flipperzero/screenstreamer.h"

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
        return;
    }
    setCliActive(true);
    // Stop RPC session only if currently up
    if (device()->rpc() && device()->rpc()->isSessionUp()) {
        m_waitingRpcStop = true;
        device()->rpc()->stopSession();
        // Wait briefly for port closure
        QElapsedTimer t; t.start();
        while (t.elapsed() < 200) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        m_waitingRpcStop = false;
    }
}

void ApplicationBackend::exitCliMode()
{
    if (!device()) {
        return;
    }
    setCliActive(false);
    // Resume RPC only after ensuring CLI released the port; then start screen streamer
    auto *rpc = device()->rpc();
    if (!rpc) {
        return;
    }

    // Refresh serial port info (in case COM id changed)
    if (deviceState()) {
        const auto info = deviceState()->deviceInfo();
        rpc->setMajorVersion(info.protobuf.versionMajor);
        rpc->setMinorVersion(info.protobuf.versionMinor);
        rpc->setSerialPort(info.portInfo);
    }

    // One-shot connection to start streamer only after RPC is up
    QMetaObject::Connection conn;
    conn = QObject::connect(rpc, &ProtobufSession::sessionStateChanged, this, [this, rpc, conn]() mutable {
        if (rpc->isSessionUp()) {
            if (m_screenStreamer && deviceState() && !deviceState()->isRecoveryMode()) {
                m_screenStreamer->start();
            }
            QObject::disconnect(conn);
        }
    });

    // Start RPC later to avoid handle race and only if CLI is not active
    QTimer::singleShot(600, this, [this, rpc]() {
        if (!m_cliActive) {
            rpc->startSession();
        }
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
        
        // Check for installed asset packs when device connects
        if (globalAssetPacks) {
            // Use QMetaObject to call the method to avoid undefined type issues
            QMetaObject::invokeMethod(reinterpret_cast<QObject*>(globalAssetPacks), "checkInstalledPacks", Qt::QueuedConnection);
        }

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
        if (m_screenStreamer && deviceState() && !deviceState()->isRecoveryMode()) {
            m_screenStreamer->start();
        }
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

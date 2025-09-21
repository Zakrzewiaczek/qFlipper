#include "climanager.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(LOG_CLI, "CLI")

CliManager::CliManager(QObject *parent)
    : QObject(parent)
    , m_serialPort(nullptr)
    , m_connectTimer(new QTimer(this))
    , m_isConnected(false)
    , m_isReady(false)
{
    m_connectTimer->setSingleShot(true);
    m_connectTimer->setInterval(CONNECT_TIMEOUT);
    connect(m_connectTimer, &QTimer::timeout, this, &CliManager::onConnectTimeout);
}

CliManager::~CliManager()
{
    if (m_serialPort) {
        disconnectFromDevice();
    }
}

bool CliManager::isConnected() const
{
    return m_isConnected;
}

QString CliManager::terminalOutput() const
{
    return m_terminalOutput;
}

bool CliManager::isReady() const
{
    return m_isReady;
}

static QString normalizePortName(const QString &input)
{
    QString name = input.trimmed();
    if (name.startsWith("\\\\.\\")) {
        // Windows full device path -> COMx
        name = name.mid(4);
    }
    int slash = name.lastIndexOf('/');
    int backslash = name.lastIndexOf('\\');
    int idx = qMax(slash, backslash);
    if (idx >= 0 && idx + 1 < name.size()) {
        name = name.mid(idx + 1);
    }
    return name;
}

void CliManager::connectToDevice(const QString &portName)
{
    if (m_isConnected) {
        qCWarning(LOG_CLI) << "Already connected to device";
        return;
    }

    const QString normalized = normalizePortName(portName);
    qCInfo(LOG_CLI) << "Connecting to device on port:" << normalized;

    if (m_serialPort) {
        m_serialPort->deleteLater();
    }

    m_serialPort = new QSerialPort(normalized, this);
    m_serialPort->setBaudRate(QSerialPort::Baud115200);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    connect(m_serialPort, &QSerialPort::readyRead, this, &CliManager::onSerialPortReadyRead);
    connect(m_serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
            this, &CliManager::onSerialPortErrorOccurred);

    if (m_serialPort->open(QIODevice::ReadWrite)) {
        setConnected(true);
        initializeConnection();
        m_connectTimer->start();
    } else {
        qCCritical(LOG_CLI) << "Failed to open serial port:" << m_serialPort->errorString();
        emit errorOccurred(m_serialPort->errorString());
        m_serialPort->deleteLater();
        m_serialPort = nullptr;
    }
}

void CliManager::disconnectFromDevice()
{
    if (!m_isConnected) {
        return;
    }

    qCInfo(LOG_CLI) << "Disconnecting from device";
    
    m_connectTimer->stop();
    setReady(false);
    setConnected(false);

    if (m_serialPort) {
        m_serialPort->close();
        m_serialPort->deleteLater();
        m_serialPort = nullptr;
    }

    m_receivedData.clear();
}

void CliManager::sendCommand(const QString &command)
{
    if (!m_isConnected || !m_serialPort) {
        qCWarning(LOG_CLI) << "Cannot send command: not connected";
        return;
    }

    qCDebug(LOG_CLI) << "Sending command:" << command;
    
    // Echo the command to the terminal output
    appendOutput(QString("> %1\n").arg(command));
    
    // Send command to device
    QByteArray data = command.toUtf8() + "\r";
    m_serialPort->write(data);
    m_serialPort->flush();
}

void CliManager::clearTerminal()
{
    m_terminalOutput.clear();
    emit terminalOutputChanged();
}

void CliManager::onSerialPortReadyRead()
{
    if (!m_serialPort) {
        return;
    }

    const QByteArray data = m_serialPort->readAll();
    m_receivedData.append(data);

    // Check if we're ready (look for CLI prompt)
    if (!m_isReady && (m_receivedData.contains(">: ") || m_receivedData.contains("flipper>"))) {
        setReady(true);
        m_connectTimer->stop();
        qCInfo(LOG_CLI) << "CLI ready";
    }

    // Sanitize: remove ANSI color/format codes and collapse duplicate prompts
    auto sanitizeChunk = [](const QByteArray &chunk) -> QString {
        QString text = QString::fromUtf8(chunk);

        // Normalize newlines
        text.replace("\r\n", "\n");

        // Strip SGR ANSI sequences like ESC[97m or ESC[38;2;255;130;0m
        static const QRegularExpression sgr(QStringLiteral("\x1B\\[[0-9;]*m"));
        text.remove(sgr);

        // Collapse repeated prompts ">: " on consecutive lines
        static const QRegularExpression dblPrompt(QStringLiteral("\n>:\\s*\n>:\\s*"));
        while (text.contains(dblPrompt)) {
            text.replace(dblPrompt, QStringLiteral("\n>: \n"));
        }

        return text;
    };

    appendOutput(sanitizeChunk(data));
}

void CliManager::onSerialPortErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }

    QString errorString = m_serialPort ? m_serialPort->errorString() : "Unknown error";
    qCCritical(LOG_CLI) << "Serial port error:" << errorString;
    
    emit errorOccurred(errorString);
    disconnectFromDevice();
}

void CliManager::onConnectTimeout()
{
    if (!m_isReady) {
        qCWarning(LOG_CLI) << "Connection timeout - CLI not ready";
        emit errorOccurred("Connection timeout - device CLI not responding");
        disconnectFromDevice();
    }
}

void CliManager::setConnected(bool connected)
{
    if (m_isConnected != connected) {
        m_isConnected = connected;
        emit isConnectedChanged();
    }
}

void CliManager::setReady(bool ready)
{
    if (m_isReady != ready) {
        m_isReady = ready;
        emit isReadyChanged();
    }
}

void CliManager::appendOutput(const QString &text)
{
    m_terminalOutput.append(text);
    
    // Keep terminal output from getting too large (keep last 10000 characters)
    const int maxLength = 10000;
    if (m_terminalOutput.length() > maxLength) {
        m_terminalOutput = m_terminalOutput.right(maxLength);
    }
    
    emit terminalOutputChanged();
}

void CliManager::initializeConnection()
{
    // Reset DTR to ensure clean connection
    if (m_serialPort) {
        m_serialPort->setDataTerminalReady(false);
        QTimer::singleShot(50, this, [this]() {
            if (m_serialPort) {
                m_serialPort->setDataTerminalReady(true);
                // Send a carriage return to get to CLI prompt
                m_serialPort->write("\r");
                m_serialPort->flush();
            }
        });
    }
}

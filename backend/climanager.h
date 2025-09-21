#pragma once

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QStringList>

class CliManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)
    Q_PROPERTY(QString terminalOutput READ terminalOutput NOTIFY terminalOutputChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY isReadyChanged)

public:
    explicit CliManager(QObject *parent = nullptr);
    ~CliManager();

    bool isConnected() const;
    QString terminalOutput() const;
    bool isReady() const;

public slots:
    void connectToDevice(const QString &portName);
    void disconnectFromDevice();
    void sendCommand(const QString &command);
    void clearTerminal();

signals:
    void isConnectedChanged();
    void terminalOutputChanged();
    void isReadyChanged();
    void errorOccurred(const QString &error);

private slots:
    void onSerialPortReadyRead();
    void onSerialPortErrorOccurred(QSerialPort::SerialPortError error);
    void onConnectTimeout();

private:
    void setConnected(bool connected);
    void setReady(bool ready);
    void appendOutput(const QString &text);
    void initializeConnection();

    QSerialPort *m_serialPort;
    QTimer *m_connectTimer;
    QString m_terminalOutput;
    bool m_isConnected;
    bool m_isReady;
    QByteArray m_receivedData;
    
    static const int CONNECT_TIMEOUT = 5000; // 5 seconds
};




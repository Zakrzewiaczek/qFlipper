#pragma once

#include <QObject>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>

#include "failable.h"

class ZipUncompressor : public QObject, public Failable
{
    Q_OBJECT
public:
    ZipUncompressor(QFile *zipFile, const QDir &targetDir, QObject *parent = nullptr);

signals:
    void finished();

private:
    void start();
    void extract();
    bool extractAll(QIODevice *in, const QDir &outDir, QString &errorString);

    QFile *m_zipFile;
    QDir m_targetDir;
    QFutureWatcher<void> *m_watcher;
};



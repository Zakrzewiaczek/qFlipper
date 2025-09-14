#include "filesuploadoperation.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <memory>

#include "flipperzero/devicestate.h"
#include "flipperzero/protobufsession.h"
#include "flipperzero/rpc/storagemkdiroperation.h"
#include "flipperzero/rpc/storagewriteoperation.h"
#include "backenderror.h"

using namespace Flipper;
using namespace Zero;

FilesUploadOperation::FilesUploadOperation(ProtobufSession *rpc, DeviceState *deviceState, const QList<QUrl> &fileUrls, const QByteArray &remotePath, QObject *parent) : AbstractUtilityOperation(rpc, deviceState, parent),
                                                                                                                                                                         m_remotePath(remotePath),
                                                                                                                                                                         m_urlList(fileUrls),
                                                                                                                                                                         m_totalSize(0),
                                                                                                                                                                         m_progressBase(0.0)
{
}

const QString FilesUploadOperation::description() const
{
    const auto numFiles = m_urlList.size();
    return QStringLiteral("Upload %1 %2 @%3").arg(QString::number(numFiles), (numFiles == 1) ? "entry" : "entries", deviceState()->deviceInfo().name);
}

void FilesUploadOperation::nextStateLogic()
{
    if (operationState() == Ready)
    {
        setOperationState(ReadingFileList);
        readFileList();
    }
    else if (operationState() == ReadingFileList)
    {
        setOperationState(WritingFiles);
        writeFiles();
    }
    else if (operationState() == WritingFiles)
    {
        // Do nothing here. Finishing is handled in processNextFile() once all files are processed.
        // This avoids prematurely finishing after the first mkdir/file.
    }
}

void FilesUploadOperation::readFileList()
{
    qDebug().noquote() << "[UPLOAD] FilesUploadOperation::readFileList() - Processing" << m_urlList.size() << "URLs";

    for (const auto &url : qAsConst(m_urlList))
    {
        const QFileInfo fileInfo(url.adjusted(QUrl::StripTrailingSlash).toLocalFile());
        const QDir topmostDir = fileInfo.dir();

        qDebug().noquote() << "[UPLOAD] Processing URL:" << url.toString();
        qDebug().noquote() << "[UPLOAD] File path:" << fileInfo.absoluteFilePath();
        qDebug().noquote() << "[UPLOAD] Is file:" << fileInfo.isFile() << "Is dir:" << fileInfo.isDir();
        qDebug().noquote() << "[UPLOAD] Topmost dir:" << topmostDir.absolutePath();

        m_fileList.append({fileInfo, topmostDir});

        if (fileInfo.isFile())
        {
            m_totalSize += fileInfo.size();
            qDebug().noquote() << "[UPLOAD] Added file:" << fileInfo.fileName() << "Size:" << fileInfo.size();
        }
        else if (fileInfo.isDir())
        {
            QDir dir(fileInfo.absoluteFilePath());
            dir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
            dir.setSorting(QDir::Name | QDir::DirsFirst);

            qDebug().noquote() << "[UPLOAD] Scanning directory:" << dir.absolutePath();
            qDebug().noquote() << "[UPLOAD] Directory exists:" << dir.exists();
            qDebug().noquote() << "[UPLOAD] Directory is readable:" << dir.isReadable();

            QDirIterator it(dir, QDirIterator::Subdirectories);
            int dirFileCount = 0;
            while (it.hasNext())
            {
                const QFileInfo fileInfo(it.next());
                dirFileCount++;

                m_fileList.append({fileInfo, topmostDir});

                if (fileInfo.isFile())
                {
                    m_totalSize += fileInfo.size();
                    qDebug().noquote() << "[UPLOAD] Found file:" << fileInfo.fileName() << "Size:" << fileInfo.size();
                }
                else
                {
                    qDebug().noquote() << "[UPLOAD] Found directory:" << fileInfo.fileName();
                }
            }
            qDebug().noquote() << "[UPLOAD] Total entries found in directory:" << dirFileCount;
        }
    }

    qDebug().noquote() << "[UPLOAD] FilesUploadOperation::readFileList() - Total files to upload:" << m_fileList.size();
    qDebug().noquote() << "[UPLOAD] FilesUploadOperation::readFileList() - Total size:" << m_totalSize;

    advanceOperationState();
}

void FilesUploadOperation::writeFiles()
{
    // Use sequential approach to avoid overwhelming the Flipper Zero
    auto fileCountLeft = m_fileList.size();

    if (fileCountLeft == 0)
    {
        advanceOperationState();
        return;
    }

    qDebug().noquote() << "[UPLOAD START] Starting sequential upload of" << fileCountLeft << "entries";

    // Process files one by one to avoid overwhelming the Flipper Zero
    m_currentFileIndex = 0;
    m_totalFiles = m_fileList.size();
    m_progressBase = 0.0;
    setProgress(0.0);
    processNextFile();
}

void FilesUploadOperation::processNextFile()
{
    if (m_currentFileIndex >= m_totalFiles)
    {
        qDebug().noquote() << "[UPLOAD COMPLETE] All files processed successfully";
        // Ensure progress shows 100% before finishing
        setProgress(100.0);
        // All files done; finish the operation right here to avoid re-entering state machine
        finish();
        return;
    }

    const auto &entry = m_fileList[m_currentFileIndex];
    const auto &fileInfo = entry.fileInfo;
    const auto &topmostDir = entry.topmostDir;

    const auto absoluteLocalPath = fileInfo.absoluteFilePath();
    const auto relativeLocalPath = topmostDir.relativeFilePath(absoluteLocalPath);

    QString normalizedRelativePath = relativeLocalPath;
    normalizedRelativePath.replace("\\", "/");
    const auto absoluteRemotePath = m_remotePath + QByteArrayLiteral("/") + normalizedRelativePath.toLocal8Bit();
    const auto sizeRatio = (m_totalSize > 0) ? (double)fileInfo.size() / m_totalSize : 1.0 / m_totalFiles;

    // Keep progress driven by accumulated size only to avoid overshooting 100%
    double totalProgress = m_progressBase * 100.0;
    if (totalProgress < 0.0)
        totalProgress = 0.0;
    else if (totalProgress > 100.0)
        totalProgress = 100.0;
    setProgress(totalProgress);

    // Set a timeout for this individual file operation
    QTimer *fileTimeout = new QTimer(this);
    fileTimeout->setSingleShot(true);
    fileTimeout->setInterval(60000); // 60 second timeout per file

    if (fileInfo.isFile())
    {
        auto *file = new QFile(absoluteLocalPath, this);
        auto *operation = rpc()->storageWrite(absoluteRemotePath, file);

        connect(operation, &AbstractOperation::progressChanged, this, [=]()
                {
            // Progress is based on total bytes ratio, not file count
            double totalProgress = (m_progressBase + (operation->progress() / 100.0) * sizeRatio) * 100.0;
            if (totalProgress < 0.0) totalProgress = 0.0; else if (totalProgress > 100.0) totalProgress = 100.0;
            
            // Ensure the final file shows 100% when complete
            if (m_currentFileIndex == m_totalFiles - 1 && operation->progress() >= 100.0) {
                totalProgress = 100.0;
            }
            
            setProgress(totalProgress); });

        connect(operation, &AbstractOperation::finished, this, [=]()
                {
            fileTimeout->stop();
            fileTimeout->deleteLater();
            if(operation->isError()) {
                finishWithError(operation->error(), operation->errorString());
            } else {
                // Accumulate completed file share of total progress
                m_progressBase += sizeRatio;
                double totalProgress = m_progressBase * 100.0;
                if (totalProgress > 100.0) totalProgress = 100.0; else if (totalProgress < 0.0) totalProgress = 0.0;
                setProgress(totalProgress);
                m_currentFileIndex++;
                // Small delay to respect SD card SPI speeds and avoid overwhelming the device
                QTimer::singleShot(300, this, &FilesUploadOperation::processNextFile);
            }
            operation->deleteLater(); });

        connect(fileTimeout, &QTimer::timeout, this, [=]()
                {
            operation->deleteLater();
            fileTimeout->deleteLater();
            finishWithError(BackendError::TimeoutError, "File upload timed out"); });

        fileTimeout->start();
    }
    else if (fileInfo.isDir())
    {
        auto *operation = rpc()->storageMkdir(absoluteRemotePath);
        connect(operation, &AbstractOperation::finished, this, [=]()
                {
            fileTimeout->stop();
            fileTimeout->deleteLater();
            if(operation->isError()) {
                finishWithError(operation->error(), operation->errorString());
            } else {
                m_currentFileIndex++;
                // Small delay to respect SD card SPI speeds and avoid overwhelming the device
                QTimer::singleShot(300, this, &FilesUploadOperation::processNextFile);
            }
            operation->deleteLater(); });

        connect(fileTimeout, &QTimer::timeout, this, [=]()
                {
            operation->deleteLater();
            fileTimeout->deleteLater();
            finishWithError(BackendError::TimeoutError, "Directory creation timed out"); });

        fileTimeout->start();
    }
}

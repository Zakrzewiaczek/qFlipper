#include "assetpacks.h"
#include "applicationbackend.h"

AssetPacks *globalAssetPacks = nullptr;

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QFileInfo>
#include <QFile>
#include <QIODevice>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QProcess>
#include <QDirIterator>
#include <QBuffer>
#include <QDateTime>
#include "zipuncompressor.h"

#include "flipperzero/flipperzero.h"
#include "flipperzero/protobufsession.h"
#include "flipperzero/rpc/storagemkdiroperation.h"
#include "flipperzero/rpc/storageremoveoperation.h"
#include "flipperzero/rpc/storagelistoperation.h"
#include "flipperzero/utility/filesuploadoperation.h"
#include "flipperzero/utilityinterface.h"
#include "flipperzero/devicestate.h"
#include "flipperzero/assetmanifest.h"
#include "flipperzero/rpc/storagereadoperation.h"
#include "flipperzero/rpc/storagewriteoperation.h"
#include "tarzipuncompressor.h"
#include "abstractoperation.h"

Q_DECLARE_LOGGING_CATEGORY(CATEGORY_UPDATES)

AssetPacks::AssetPacks(ApplicationBackend *backend, QObject *parent)
    : QObject(parent), m_backend(backend)
{
}

void AssetPacks::fetchJson(const QUrl &url)
{
    if (m_currentReply)
    {
        m_currentReply->disconnect(this);
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    qCDebug(CATEGORY_UPDATES).noquote() << "Fetching asset packs information from " << url.toString();
    QNetworkRequest request(url);
    m_currentReply = m_networkManager.get(request);
    connect(m_currentReply, &QNetworkReply::finished, this, &AssetPacks::onReplyFinished);
}

void AssetPacks::downloadAndSaveFile(const QString &fileUrl)
{
    if (fileUrl.isEmpty())
    {
        qCCritical(CATEGORY_UPDATES).noquote() << "Download URL is empty";
        emit downloadFinished(false, "Download URL is empty", fileUrl);
        return;
    }

    // Extract filename from URL
    QUrl url(fileUrl);
    QString fileName = QFileInfo(url.path()).fileName();
    if (fileName.isEmpty())
    {
        fileName = "asset_pack.zip";
    }

    // Emit signal to request save dialog from QML/frontend
    emit requestSaveFile(fileUrl, fileName);
}

void AssetPacks::performDownload(const QString &fileUrl, const QString &savePath)
{
    if (fileUrl.isEmpty() || savePath.isEmpty())
    {
        qCCritical(CATEGORY_UPDATES).noquote() << "Download URL or save path is empty";
        emit downloadFinished(false, "Invalid parameters", fileUrl);
        return;
    }

    // Clean up any existing download reply
    if (m_downloadReply)
    {
        m_downloadReply->disconnect(this);
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }

    emit downloadStarted();

    qCDebug(CATEGORY_UPDATES).noquote() << "Starting download from" << fileUrl;
    QUrl url(fileUrl);
    QNetworkRequest request(url);
    m_downloadReply = m_networkManager.get(request);
    m_downloadReply->setProperty("savePath", savePath);
    m_downloadReply->setProperty("fileUrl", fileUrl);

    connect(m_downloadReply, &QNetworkReply::finished, this, &AssetPacks::onDownloadReplyFinished);
}

void AssetPacks::onDownloadReplyFinished()
{
    if (!m_downloadReply)
    {
        return;
    }

    QString savePath = m_downloadReply->property("savePath").toString();
    QString fileUrl = m_downloadReply->property("fileUrl").toString();

    if (m_downloadReply->error() == QNetworkReply::NoError)
    {
        QByteArray data = m_downloadReply->readAll();

        QFile file(savePath);
        if (file.open(QIODevice::WriteOnly))
        {
            qint64 written = file.write(data);
            file.close();

            if (written == data.size())
            {
                qCDebug(CATEGORY_UPDATES).noquote() << "Download completed successfully, saved to" << savePath;
                emit downloadFinished(true, tr("Asset pack downloaded successfully"), fileUrl);
            }
            else
            {
                emit downloadFinished(false, tr("Failed to write file"), fileUrl);
            }
        }
        else
        {
            emit downloadFinished(false, tr("Failed to open file for writing"), fileUrl);
        }
    }
    else
    {
        qCDebug(CATEGORY_UPDATES).noquote() << "Download failed:" << m_downloadReply->errorString();
        emit downloadFinished(false, tr("Download failed: %1").arg(m_downloadReply->errorString()), fileUrl);
    }

    m_downloadReply->deleteLater();
    m_downloadReply = nullptr;
}

void AssetPacks::onReplyFinished()
{
    if (!m_currentReply)
    {
        return;
    }
    if (m_currentReply->error() == QNetworkReply::NoError)
    {
        m_errorOccured = false;
        emit errorOccuredChanged();
        parseJson(m_currentReply->readAll());
    }
    else
    {
        qCCritical(CATEGORY_UPDATES).noquote() << "Failed to fetch asset packs:" << m_currentReply->errorString();
        m_errorOccured = true;
        emit errorOccuredChanged();
    }

    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

void AssetPacks::parseJson(const QByteArray &data)
{
    clearData();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        qCCritical(CATEGORY_UPDATES).noquote() << "Failed to parse JSON:" << parseError.errorString();
        m_errorOccured = true;
        emit errorOccuredChanged();
        return;
    }

    QJsonArray packsArray;
    if (doc.isArray())
    {
        // JSON is directly an array
        packsArray = doc.array();
    }
    else if (doc.isObject())
    {
        // JSON is an object containing "packs" array
        QJsonObject rootObj = doc.object();
        packsArray = rootObj.value("packs").toArray();
    }
    else
    {
        qCCritical(CATEGORY_UPDATES).noquote() << "JSON is neither an array nor an object";
        m_errorOccured = true;
        emit errorOccuredChanged();
        return;
    }

    for (const QJsonValue &packValue : packsArray)
    {
        if (!packValue.isObject())
            continue;

        QJsonObject packObj = packValue.toObject();

        // Extract basic information
        m_idsList.append(packObj.value("id").toString());
        m_titlesList.append(packObj.value("name").toString()); // Changed from "title" to "name"
        m_authorsList.append(packObj.value("author").toString());
        m_descriptionsList.append(packObj.value("description").toString());
        m_sourceUrlsList.append(packObj.value("source_url").toString());

        // Extract files information
        QJsonArray filesArray = packObj.value("files").toArray();
        QString zipUrl, targzUrl, targzSha256;
        for (const QJsonValue &fileValue : filesArray)
        {
            QJsonObject fileObj = fileValue.toObject();
            QString type = fileObj.value("type").toString();
            if (type == "pack_zip")
            {
                zipUrl = fileObj.value("url").toString();
            }
            else if (type == "pack_targz")
            {
                targzUrl = fileObj.value("url").toString();
                targzSha256 = fileObj.value("sha256").toString();
            }
        }
        m_zipUrlsList.append(zipUrl);
        m_targzUrlsList.append(targzUrl);
        m_targzSha256List.append(targzSha256);

        // Extract stats information
        QJsonObject statsObj = packObj.value("stats").toObject();
        m_lastUpdatedList.append(QString::number(statsObj.value("updated").toInt()));
        m_addedList.append(QString::number(statsObj.value("added").toInt()));

        // Extract preview URLs
        QStringList previewUrls;
        QJsonArray previewArray = packObj.value("preview_urls").toArray();
        for (const QJsonValue &urlValue : previewArray)
        {
            QString url = urlValue.toString();
            previewUrls.append(url);
            m_previewUrlsFlat.append(url);
        }
        m_previewUrlsList.append(previewUrls);

        // Extract counts
        m_packsList.append(statsObj.value("packs").toInt());
        m_animsList.append(statsObj.value("anims").toInt());
        m_iconsList.append(statsObj.value("icons").toInt());

        // Extract string arrays from stats
        QStringList fonts;
        QJsonArray fontsArray = statsObj.value("fonts").toArray();
        for (const QJsonValue &fontValue : fontsArray)
        {
            fonts.append(fontValue.toString());
        }
        m_fontsList.append(fonts);

        QStringList passport;
        QJsonArray passportArray = statsObj.value("passport").toArray();
        for (const QJsonValue &passportValue : passportArray)
        {
            passport.append(passportValue.toString());
        }
        m_passportList.append(passport);

        QStringList folders;
        QJsonArray foldersArray = statsObj.value("folders").toArray();
        for (const QJsonValue &folderValue : foldersArray)
        {
            folders.append(folderValue.toString());
        }
        m_foldersList.append(folders);

        // Initialize installation status as unknown/false
        m_isInstalledList.append(false);
        m_needsUpdateList.append(false);
    }

    emit jsonFetched(data);
    emit dataChanged();
}

void AssetPacks::clearData()
{
    m_idsList.clear();
    m_titlesList.clear();
    m_authorsList.clear();
    m_descriptionsList.clear();
    m_previewUrlsList.clear();
    m_previewUrlsFlat.clear();
    m_sourceUrlsList.clear();
    m_zipUrlsList.clear();
    m_targzUrlsList.clear();
    m_targzSha256List.clear();
    m_isInstalledList.clear();
    m_needsUpdateList.clear();
    m_packsList.clear();
    m_animsList.clear();
    m_iconsList.clear();
    m_fontsList.clear();
    m_passportList.clear();
    m_foldersList.clear();
    m_lastUpdatedList.clear();
    m_addedList.clear();
}

void AssetPacks::installAssetPack(const QString &packId, const QString &packUrl)
{
    if (!m_backend || !m_backend->device()) {
        emit installFinished(packId, false, "No device connected");
        return;
    }

    if (packUrl.isEmpty()) {
        emit installFinished(packId, false, "No download URL available");
        return;
    }

    emit installStarted(packId);

    // Create temporary directory for extraction
    QTemporaryDir *tempDir = new QTemporaryDir();
    if (!tempDir->isValid()) {
        emit installFinished(packId, false, "Failed to create temporary directory");
        delete tempDir;
        return;
    }

    // Download the asset pack
        qDebug() << "Starting download for pack:" << packId << "URL:" << packUrl;
        emit installProgress(packId, 5); // Show 5% progress
    QUrl url(packUrl);
    QNetworkRequest request(url);
    // Ensure redirects are followed and set a UA so some CDNs don't serve HTML
    // Qt 6.4: use RedirectPolicyAttribute only
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#else
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
    request.setRawHeader("User-Agent", QByteArray("qFlipper/1.3.4 (Qt)"));
    request.setRawHeader("Accept", QByteArray("application/zip,application/octet-stream,*/*"));
    request.setRawHeader("Cache-Control", QByteArray("no-cache"));
    request.setRawHeader("Pragma", QByteArray("no-cache"));
    QNetworkReply *reply = m_networkManager.get(request);
    
        connect(reply, &QNetworkReply::finished, this, [=]() {
        qDebug() << "Download finished for pack:" << packId;
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Download error:" << reply->errorString();
            emit installFinished(packId, false, QString("Download failed: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }

        // Save downloaded file (use .zip extension)
        QString tempFilePath = tempDir->filePath("asset_pack.zip");
        qDebug() << "Saving downloaded file to:" << tempFilePath;
        QFile file(tempFilePath);
        if (!file.open(QIODevice::WriteOnly)) {
            qDebug() << "Failed to open file for writing:" << tempFilePath;
            emit installFinished(packId, false, "Failed to save downloaded file");
            reply->deleteLater();
            delete tempDir;
            return;
        }

        QByteArray data = reply->readAll();
        qDebug() << "Downloaded" << data.size() << "bytes";
        emit installProgress(packId, 25); // Show 25% progress
        file.write(data);
        file.close();
        reply->deleteLater();

        // Validate ZIP magic to avoid extracting HTML/redirects
        if (data.size() < 4 || !(data.startsWith("PK\x03\x04") || data.startsWith("PK\x05\x06") || data.startsWith("PK\x07\x08"))) {
            qDebug() << "Downloaded file does not look like a ZIP (bad magic)";
            emit installFinished(packId, false, "Downloaded file is not a valid ZIP");
            delete tempDir;
            return;
        }

        // Extract the zip file
        QString extractPath = tempDir->filePath("extracted");
        qDebug() << "Creating extraction directory:" << extractPath;
        QDir().mkpath(extractPath);

        // Prepare for extraction (avoid holding the file open to prevent locking)
        QFileInfo zipInfo(tempFilePath);
        qDebug() << "Starting zip extraction";
        qDebug() << "Zip file size:" << zipInfo.size() << "bytes";
        qDebug() << "Zip file path:" << zipInfo.absoluteFilePath();
        emit installProgress(packId, 30); // Show 30% progress
        
        // Built-in ZipUncompressor only
        auto *zipFileHandle = new QFile(tempFilePath, this);
        auto *unzipper = new ZipUncompressor(zipFileHandle, QDir(extractPath), this);
        emit installProgress(packId, 40);
        connect(unzipper, &ZipUncompressor::finished, this, [=]() {
            if (unzipper->isError()) {
                qDebug() << "Zip extraction error:" << unzipper->errorString();
                emit installFinished(packId, false, QString("Failed to extract ZIP: %1").arg(unzipper->errorString()));
                delete tempDir;
                unzipper->deleteLater();
                return;
            }
            // Ensure at least one file exists
            int extractedFiles = 0;
            {
                QDirIterator it(extractPath, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) { it.next(); ++extractedFiles; }
            }
            if (extractedFiles == 0) {
                emit installFinished(packId, false, "ZIP extracted but no files found");
                delete tempDir;
                unzipper->deleteLater();
                return;
            }
            emit installProgress(packId, 50);
            this->processExtractedFiles(packId, extractPath, tempDir);
            unzipper->deleteLater();
        });
    });
}

void AssetPacks::processExtractedFiles(const QString &packId, const QString &extractPath, QTemporaryDir *tempDir) {
    qDebug() << "Processing extracted files for pack:" << packId;
    qDebug() << "Extraction successful, checking extracted content at:" << extractPath;
    
    // Find the root folder in the extracted content
    QDir extractDir(extractPath);
    QStringList entries = extractDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    qDebug() << "Found entries:" << entries;
    if (entries.isEmpty()) {
        qDebug() << "No root folder found in asset pack";
        emit installFinished(packId, false, "No root folder found in asset pack");
        delete tempDir;
        return;
    }

    QString rootFolderName = entries.first();
    // Sanity-check: ensure there are files inside the root folder
    int fileCount = 0;
    {
        const QString rootPath = QDir(extractPath).filePath(rootFolderName);
        QDirIterator it(rootPath, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) { it.next(); ++fileCount; }
    }
    if (fileCount == 0) {
        qDebug() << "No files found in extracted asset pack root; aborting";
        emit installFinished(packId, false, "Extracted pack contains no files");
        delete tempDir;
        return;
    }
    QString flipperParentPath = QString("/ext/asset_packs");
    QString flipperPath = QString("%1/%2").arg(flipperParentPath, rootFolderName);
    qDebug() << "Using root folder:" << rootFolderName << "Target path (parent):" << flipperParentPath;
    // Ensure screen streaming is stopped before heavy RPC traffic
    if (m_backend) {
        m_backend->stopFullScreenStreaming();
    }
    
    // Create the directory on Flipper
    qDebug() << "Creating directory on Flipper:" << flipperPath;
    auto *mkdirOp = m_backend->device()->rpc()->storageMkdir(flipperPath.toUtf8());
    connect(mkdirOp, &AbstractOperation::finished, this, [=]() {
        qDebug() << "Mkdir operation finished for:" << flipperPath;
        if (mkdirOp->isError()) {
            qDebug() << "Mkdir error:" << mkdirOp->errorString();
            emit installFinished(packId, false, QString("Failed to create directory: %1").arg(mkdirOp->errorString()));
            mkdirOp->deleteLater();
            QTimer::singleShot(100, [tempDir]() {
                if (tempDir) {
                    delete tempDir;
                }
            });
            return;
        }

        qDebug() << "Directory created successfully, preparing file upload";
        // Upload the entire root folder; utility will recurse and preserve structure
        const QString localRoot = QDir(extractPath).filePath(rootFolderName);
        
        // Debug: Check what's actually in the extracted directory
        QDir rootDir(localRoot);
        qDebug() << "[ASSET DEBUG] Checking contents of:" << localRoot;
        qDebug() << "[ASSET DEBUG] Directory exists:" << rootDir.exists();
        qDebug() << "[ASSET DEBUG] Directory is readable:" << rootDir.isReadable();
        
        QStringList entries = rootDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        qDebug() << "[ASSET DEBUG] Found entries in root directory:" << entries;
        
        // Count files recursively
        int fileCount = 0;
        QDirIterator it(localRoot, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            fileCount++;
        }
        qDebug() << "[ASSET DEBUG] Total files found recursively:" << fileCount;
        
        // List all files with their paths
        QDirIterator fileIt(localRoot, QDir::Files, QDirIterator::Subdirectories);
        while (fileIt.hasNext()) {
            QString filePath = fileIt.next();
            QFileInfo fileInfo(filePath);
            qDebug() << "[ASSET DEBUG] Found file:" << filePath << "Size:" << fileInfo.size();
        }
        
        // Pass the root directory URL - FilesUploadOperation will handle directory traversal
        QList<QUrl> fileUrls;
        fileUrls.append(QUrl::fromLocalFile(localRoot));
        qDebug() << "[ASSET DEBUG] Queueing root directory for upload:" << localRoot;
        
        // Disable virtual display/screen stream during upload to avoid RPC/UI races
        if (m_backend && m_backend->device() && m_backend->device()->deviceState()) {
            m_backend->device()->deviceState()->setAllowVirtualDisplay(false);
        }
        if (m_backend) {
            m_backend->stopFullScreenStreaming();
        }
        qDebug() << "[UPLOAD START] Starting file upload operation";
        Flipper::Zero::FilesUploadOperation *uploadOp = m_backend->device()->utility()->uploadFiles(fileUrls, flipperParentPath.toUtf8());
        
        // Add a timer to track upload progress
        QTimer *progressTimer = new QTimer(this);
        QPointer<QTimer> progressTimerGuard(progressTimer);
        QPointer<AbstractOperation> uploadOpGuard(uploadOp);
        progressTimer->setInterval(1000); // Check every second
        connect(progressTimer, &QTimer::timeout, this, [=]() {
            if (!uploadOpGuard || !progressTimerGuard) return;
            qDebug() << "[TIMER] Upload still running, progress:" << uploadOpGuard->progress();
        });
        progressTimer->start();
        
        QMetaObject::Connection progressConn = connect(uploadOp, &AbstractOperation::progressChanged, this, [=]() {
            try {
                int progress = qBound(50, 50 + static_cast<int>(uploadOp->progress() / 2), 100);
                qDebug() << "[PROGRESS] Upload progress:" << uploadOp->progress() << "-> UI progress:" << progress;
                emit installProgress(packId, progress);
            } catch (...) {
                qDebug() << "[PROGRESS] Exception in progress handler";
            }
        });
        
        // Error handling is done in the finished signal handler
        
        // Force initial progress update
        emit installProgress(packId, 50);
        
        connect(uploadOp, &AbstractOperation::finished, this, [=]() {
            try {
                qDebug() << "[FINISH] Upload operation finished for pack:" << packId;
                if (uploadOp->isError()) {
                    qDebug() << "[FINISH] Upload error:" << uploadOp->errorString();
                    emit installFinished(packId, false, QString("Upload failed: %1").arg(uploadOp->errorString()));
                } else {
                    qDebug() << "[FINISH] Upload successful for pack:" << packId;
                    emit installProgress(packId, 100);
                    emit installFinished(packId, true, "Asset pack installed successfully");
                    
                     // Create asset pack manifest file for detection
                     createAssetPackManifest(packId);
                     
                     // Update asset pack status to show as installed
                     updateAssetPackStatus(packId, true);
                     
                     // Also refresh the manifest to ensure accurate detection
                     QTimer::singleShot(1000, this, [this]() {
                         checkInstalledPacks();
                     });
                }
                
                 // Clean up progress timer safely
                 if (progressTimerGuard) {
                     progressTimerGuard->stop();
                     progressTimerGuard->deleteLater();
                 }
                
                 // Re-enable virtual display after upload completes
                 // Use a longer delay to avoid race conditions that cause crashes
                 QTimer::singleShot(2000, this, [this]() {
                     try {
                         if (m_backend && m_backend->device() && m_backend->device()->deviceState()) {
                             m_backend->device()->deviceState()->setAllowVirtualDisplay(true);
                         }
                         // Don't restart screen streaming immediately to avoid crashes
                         // Let the user manually restart it if needed
                         qDebug() << "[FINISH] Virtual display re-enabled, screen streaming can be manually restarted";
                     } catch (...) {
                         qDebug() << "[FINISH] Exception while re-enabling virtual display";
                     }
                 });
                 
                
                // Safely delete temp directory using deferred deletion to avoid race conditions
                QTimer::singleShot(3000, [tempDir]() {
                    if (tempDir) {
                        delete tempDir;
                    }
                });
            } catch (...) {
                qDebug() << "[FINISH] Exception in upload finished handler - skipping cleanup";
            }
        });
        
        uploadOp->start();
    });
}

void AssetPacks::uninstallAssetPack(const QString &packId)
{
    if (!m_backend || !m_backend->device()) {
        emit uninstallFinished(packId, false, "No device connected");
        return;
    }

    QString flipperPath = QString("/ext/asset_packs/%1").arg(packId);
    
    // Remove the directory recursively
    auto *removeOp = m_backend->device()->rpc()->storageRemove(flipperPath.toUtf8(), true);
    connect(removeOp, &AbstractOperation::finished, this, [=]() {
        if (removeOp->isError()) {
            emit uninstallFinished(packId, false, QString("Uninstall failed: %1").arg(removeOp->errorString()));
        } else {
        // Also remove the manifest file
        QString manifestPath = QString("/ext/asset_packs/.manifests/%1.pack").arg(packId);
            auto *manifestRemoveOp = m_backend->device()->rpc()->storageRemove(manifestPath.toUtf8());
            connect(manifestRemoveOp, &AbstractOperation::finished, this, [=]() {
                manifestRemoveOp->deleteLater();
                emit uninstallFinished(packId, true, "Asset pack uninstalled successfully");
                updateAssetPackStatus(packId, false);
            });
        }
        removeOp->deleteLater();
    });
}

void AssetPacks::createAssetPackManifest(const QString &packId)
{
    if (!m_backend || !m_backend->device()) {
        return;
    }

    qDebug() << "[MANIFEST] Creating asset pack manifest for:" << packId;
    
        // Create .manifests directory inside asset_packs if it doesn't exist
        auto *mkdirOp = m_backend->device()->rpc()->storageMkdir("/ext/asset_packs/.manifests");
    connect(mkdirOp, &AbstractOperation::finished, this, [=]() {
        mkdirOp->deleteLater();
        
        // Create the pack manifest file with proper JSON structure
        QString manifestFileName = QString("/ext/asset_packs/.manifests/%1.pack").arg(packId);
        
        // Create JSON structure similar to the example you provided
        QJsonObject manifestJson;
        manifestJson["sha256"] = QString("placeholder_sha256_for_%1").arg(packId);
        manifestJson["folders"] = QJsonArray::fromStringList(QStringList() << packId);
        
        QJsonDocument doc(manifestJson);
        QString manifestContent = doc.toJson(QJsonDocument::Compact);
        
        QBuffer *buffer = new QBuffer(this);
        buffer->setData(manifestContent.toUtf8());
        
        auto *writeOp = m_backend->device()->rpc()->storageWrite(manifestFileName.toUtf8(), buffer);
        connect(writeOp, &AbstractOperation::finished, this, [=]() {
            if (writeOp->isError()) {
                qDebug() << "[MANIFEST] Failed to create manifest file:" << writeOp->errorString();
            } else {
                qDebug() << "[MANIFEST] Successfully created manifest file:" << manifestFileName;
            }
            writeOp->deleteLater();
            buffer->deleteLater();
        });
    });
}

void AssetPacks::checkInstalledPacks()
{
    if (!m_backend || !m_backend->device()) {
        return;
    }

    qDebug() << "[MANIFEST] Checking installed asset packs via manifest files";
    
    // List the .manifests directory inside asset_packs to find .pack files
    auto *listOp = m_backend->device()->rpc()->storageList("/ext/asset_packs/.manifests");
    connect(listOp, &AbstractOperation::finished, this, [=]() {
        if (listOp->isError()) {
            qDebug() << "[MANIFEST] Failed to list manifest directory:" << listOp->errorString();
            // If manifest directory doesn't exist, assume no packs are installed
            updateAllPackStatuses(false);
            return;
        }

        QStringList installedPacks;
        const auto &files = listOp->files();
        
        for (const auto &fileInfo : files) {
            if (fileInfo.type == FileType::RegularFile && fileInfo.name.endsWith(".pack")) {
                // Extract pack ID from filename like "akira.pack"
                QString packId = fileInfo.name;
                packId.chop(5); // Remove ".pack"
                installedPacks.append(packId);
                qDebug() << "[MANIFEST] Found installed pack:" << packId;
            }
        }

        // Update status for all known packs
        for (int i = 0; i < m_idsList.size(); ++i) {
            QString packId = m_idsList[i];
            bool isInstalled = installedPacks.contains(packId);
            m_isInstalledList[i] = isInstalled;
            qDebug() << "[MANIFEST] Pack" << packId << "installed:" << isInstalled;
        }

        // Emit dataChanged to refresh the UI
        emit dataChanged();
        
        listOp->deleteLater();
    });
}

void AssetPacks::updateAllPackStatuses(bool isInstalled)
{
    qDebug() << "[MANIFEST] Updating all pack statuses to:" << isInstalled;
    for (int i = 0; i < m_isInstalledList.size(); ++i) {
        m_isInstalledList[i] = isInstalled;
    }
    emit dataChanged();
}

void AssetPacks::updateAssetPackStatus(const QString &packId, bool isInstalled)
{
    int index = m_idsList.indexOf(packId);
    if (index >= 0 && index < m_isInstalledList.size() && 
        index < m_titlesList.size() && index < m_authorsList.size() && 
        index < m_descriptionsList.size() && index < m_previewUrlsList.size() &&
        index < m_sourceUrlsList.size() && index < m_zipUrlsList.size()) {
        
        qDebug() << "[ASSET UPDATE] Updating status for pack:" << packId << "at index:" << index << "to installed:" << isInstalled;
        m_isInstalledList[index] = isInstalled;
        
        // Use QTimer::singleShot to defer the dataChanged signal to avoid race conditions
        QTimer::singleShot(100, this, [this]() {
            qDebug() << "[ASSET UPDATE] Emitting dataChanged signal";
            emit dataChanged();
        });
    } else {
        qDebug() << "[ASSET UPDATE] Invalid index or mismatched list sizes for pack:" << packId << "index:" << index;
    }
}

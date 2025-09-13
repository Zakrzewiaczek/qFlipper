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
#include "flipperzero/rpc/storagestatoperation.h"
#include "tarzipuncompressor.h"
#include "abstractoperation.h"

Q_DECLARE_LOGGING_CATEGORY(CATEGORY_UPDATES)

AssetPacks::AssetPacks(ApplicationBackend *backend, QObject *parent)
    : QObject(parent), m_backend(backend)
{
    // Connect the manifestCreated signal to trigger detection refresh
    connect(this, &AssetPacks::manifestCreated, this, &AssetPacks::onManifestCreated);
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
        QString zipUrl, targzUrl, targzSha256, zipSha256;
        for (const QJsonValue &fileValue : filesArray)
        {
            QJsonObject fileObj = fileValue.toObject();
            QString type = fileObj.value("type").toString();
            if (type == "pack_zip")
            {
                zipUrl = fileObj.value("url").toString();
                // Some feeds may provide sha256 for zip instead of targz
                zipSha256 = fileObj.value("sha256").toString();
            }
            else if (type == "pack_targz")
            {
                targzUrl = fileObj.value("url").toString();
                targzSha256 = fileObj.value("sha256").toString();
            }
        }
        m_zipUrlsList.append(zipUrl);
        m_targzUrlsList.append(targzUrl);
        // Prefer targz sha if present, otherwise fall back to zip sha
        m_targzSha256List.append(!targzSha256.isEmpty() ? targzSha256 : zipSha256);

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
    
    // Check for installed packs after loading the pack list
    // This ensures we have the pack IDs available for matching
    if (m_backend && m_backend->device()) {
        QTimer::singleShot(500, this, [this]() {
            qDebug() << "[MANIFEST] Checking installed packs after JSON load";
            checkInstalledPacks();
        });
    }
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

    // Support multiple top-level folders inside the archive
    QStringList rootFolderNames = entries;
    
    // Store the first actual folder name for backwards compatibility/manifest
    if (!rootFolderNames.isEmpty()) {
        m_extractedFolderNames[packId] = rootFolderNames.first();
    }
    // Sanity-check: ensure there are files inside the root folder
    int fileCount = 0;
    for (const QString &rootFolderName : rootFolderNames) {
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
    qDebug() << "Using root folders:" << rootFolderNames << "Target path (parent):" << flipperParentPath;
    // Ensure screen streaming is stopped before heavy RPC traffic
    if (m_backend) {
        m_backend->stopFullScreenStreaming();
    }
    
    // Ensure the parent directory exists on Flipper (top-level asset_packs)
    qDebug() << "Ensuring parent directory exists on Flipper:" << flipperParentPath;
    auto *mkdirOp = m_backend->device()->rpc()->storageMkdir(flipperParentPath.toUtf8());
    connect(mkdirOp, &AbstractOperation::finished, this, [=]() {
        qDebug() << "Mkdir operation finished for parent:" << flipperParentPath;
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

        qDebug() << "Directory created successfully, queueing upload";
        
        // Create upload queue entry
        QueuedUpload upload;
        upload.packId = packId;
        upload.extractPath = extractPath;
        upload.rootFolderNames = rootFolderNames;
        upload.flipperParentPath = flipperParentPath;
        upload.tempDir = tempDir;
        
        // Add to queue
        m_uploadQueue.enqueue(upload);
        qDebug() << "[QUEUE] Added pack to upload queue:" << packId << "Queue size:" << m_uploadQueue.size();
        
        // Process queue if not already uploading
        if (!m_isUploading) {
            processUploadQueue();
        }
    });
}

void AssetPacks::uninstallAssetPack(const QString &packId)
{
    if (!m_backend || !m_backend->device()) {
        emit uninstallFinished(packId, false, "No device connected");
        return;
    }

    // First read the manifest to get all actual folder names
    QString manifestPath = QString("/ext/asset_packs/.manifests/%1.pack").arg(packId);
    QBuffer *buffer = new QBuffer(this);
    auto *readOp = m_backend->device()->rpc()->storageRead(manifestPath.toUtf8(), buffer);
    connect(readOp, &AbstractOperation::finished, this, [=]() {
        QStringList folderNames; // Default filled below if manifest missing
        
        if (!readOp->isError()) {
            // Parse the manifest to get the folder names
            QByteArray manifestData = buffer->data();
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(manifestData, &parseError);
            
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject manifestObj = doc.object();
                QJsonArray foldersArray = manifestObj.value("folders").toArray();
                for (const QJsonValue &v : foldersArray) {
                    const QString name = v.toString();
                    if (!name.isEmpty()) folderNames.append(name);
                }
                qDebug() << "[UNINSTALL] Folders from manifest:" << folderNames;
            } else {
                qDebug() << "[UNINSTALL] Failed to parse manifest, using packId as fallback";
            }
        } else {
            qDebug() << "[UNINSTALL] Failed to read manifest, using packId as fallback:" << readOp->errorString();
        }

        if (folderNames.isEmpty()) {
            folderNames.append(packId);
        }

        // Remove all listed folders recursively, then remove manifest
        struct RemoveContext { qsizetype remaining; bool hadError; QString firstError; };
        RemoveContext *ctx = new RemoveContext{ folderNames.size(), false, QString() };

        auto onAllRemoved = [=]() {
            // Remove the manifest file last
            auto *manifestRemoveOp = m_backend->device()->rpc()->storageRemove(manifestPath.toUtf8());
            connect(manifestRemoveOp, &AbstractOperation::finished, this, [=]() {
                manifestRemoveOp->deleteLater();
                emit uninstallFinished(packId, !ctx->hadError, ctx->hadError ? ctx->firstError : QStringLiteral("Asset pack uninstalled successfully"));
                updateAssetPackStatus(packId, false);
                QTimer::singleShot(500, this, [this]() { checkInstalledPacks(); });
                delete ctx;
            });
        };

        for (const QString &folder : folderNames) {
            const QString flipperPath = QString("/ext/asset_packs/%1").arg(folder);
            qDebug() << "[UNINSTALL] Removing folder:" << flipperPath;
            auto *removeOp = m_backend->device()->rpc()->storageRemove(flipperPath.toUtf8(), true);
            connect(removeOp, &AbstractOperation::finished, this, [=]() {
                if (removeOp->isError()) {
                    ctx->hadError = true;
                    if (ctx->firstError.isEmpty()) ctx->firstError = QString("Uninstall failed: %1").arg(removeOp->errorString());
                }
                removeOp->deleteLater();
                ctx->remaining -= 1;
                if (ctx->remaining == 0) onAllRemoved();
            });
        }

        readOp->deleteLater();
        buffer->deleteLater();
    });
}

void AssetPacks::createAssetPackManifest(const QString &packId, const QString &actualFolderName)
{
    if (!m_backend || !m_backend->device()) {
        return;
    }

    // Resolve folders list from JSON if available; fallback to provided folder name
    QStringList foldersForManifest;
    int packIndex = m_idsList.indexOf(packId);
    if (packIndex >= 0 && packIndex < m_foldersList.size() && !m_foldersList[packIndex].isEmpty()) {
        foldersForManifest = m_foldersList[packIndex];
    } else if (!actualFolderName.isEmpty()) {
        foldersForManifest = QStringList() << actualFolderName;
    }

    // Resolve sha256 from JSON if available; fallback to placeholder
    QString sha256Value = QString("placeholder_sha256_for_%1").arg(packId);
    if (packIndex >= 0 && packIndex < m_targzSha256List.size() && !m_targzSha256List[packIndex].isEmpty()) {
        sha256Value = m_targzSha256List[packIndex];
    }

    qDebug() << "[MANIFEST] Creating asset pack manifest for:" << packId
             << "folders:" << foldersForManifest << "sha256:" << sha256Value;

    // Create .manifests directory inside asset_packs if it doesn't exist
    auto *mkdirOp = m_backend->device()->rpc()->storageMkdir("/ext/asset_packs/.manifests");
    connect(mkdirOp, &AbstractOperation::finished, this, [=]() {
        mkdirOp->deleteLater();

        // Create the pack manifest file with proper JSON structure
        QString manifestFileName = QString("/ext/asset_packs/.manifests/%1.pack").arg(packId);

        QJsonObject manifestJson;
        manifestJson["sha256"] = sha256Value;
        manifestJson["folders"] = QJsonArray::fromStringList(foldersForManifest);

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

                // Immediately update the status to show as installed
                updateAssetPackStatus(packId, true);

                // Emit signal that manifest was created successfully
                emit manifestCreated(packId);
            }
            writeOp->deleteLater();
            buffer->deleteLater();
        });
    });
}

void AssetPacks::checkInstalledPacks()
{
    if (!m_backend || !m_backend->device()) {
        qDebug() << "[MANIFEST] Cannot check installed packs - no device connected";
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
            listOp->deleteLater();
            return;
        }

        const auto &files = listOp->files();
        qDebug() << "[MANIFEST] Found" << files.size() << "files in manifest directory";
        
        // Collect all manifest files
        QStringList manifestFiles;
        for (const auto &fileInfo : files) {
            qDebug() << "[MANIFEST] Checking file:" << fileInfo.name << "type:" << static_cast<int>(fileInfo.type);
            if (fileInfo.type == FileType::RegularFile && fileInfo.name.endsWith(".pack")) {
                manifestFiles.append(fileInfo.name);
                qDebug() << "[MANIFEST] Added manifest file:" << fileInfo.name;
            }
        }
        
        qDebug() << "[MANIFEST] Found" << manifestFiles.size() << "manifest files:" << manifestFiles;
        
        if (manifestFiles.isEmpty()) {
            qDebug() << "[MANIFEST] No manifest files found - marking all packs as not installed";
            updateAllPackStatuses(false);
            listOp->deleteLater();
            return;
        }
        
        // Extract pack IDs from manifest filenames
        QStringList installedPacks;
        for (const QString &manifestFile : manifestFiles) {
            QString packId = manifestFile;
            packId.chop(5); // Remove ".pack"
            installedPacks.append(packId);
            qDebug() << "[MANIFEST] Found installed pack:" << packId << "from file:" << manifestFile;
        }
        
        qDebug() << "[MANIFEST] Extracted pack IDs:" << installedPacks;
        qDebug() << "[MANIFEST] Known pack IDs from JSON:" << m_idsList;
        
        // Update UI with found packs
        updatePackStatusesFromInstalledList(installedPacks);
        
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

void AssetPacks::updatePackStatusesFromInstalledList(const QStringList &installedPacks)
{
    qDebug() << "[MANIFEST] Updating pack statuses from installed list:" << installedPacks;
    qDebug() << "[MANIFEST] Total known packs:" << m_idsList.size();
    qDebug() << "[MANIFEST] Known pack IDs:" << m_idsList;
    
    // Ensure the installed list is properly sized
    if (m_isInstalledList.size() != m_idsList.size()) {
        qDebug() << "[MANIFEST] Resizing installed list from" << m_isInstalledList.size() << "to" << m_idsList.size();
        m_isInstalledList.resize(m_idsList.size());
        m_isInstalledList.fill(false); // Initialize all as not installed
    }
    
    // Update status for all known packs
    for (int i = 0; i < m_idsList.size(); ++i) {
        QString packId = m_idsList[i];
        bool isInstalled = installedPacks.contains(packId);
        m_isInstalledList[i] = isInstalled;
        qDebug() << "[MANIFEST] Pack" << packId << "at index" << i << "installed:" << isInstalled;
    }
    
    qDebug() << "[MANIFEST] Final installed status list:" << m_isInstalledList;

    // Emit dataChanged to refresh the UI
    emit dataChanged();
}

void AssetPacks::updateAssetPackStatus(const QString &packId, bool isInstalled)
{
    int index = m_idsList.indexOf(packId);
    if (index < 0) {
        qDebug() << "[ASSET UPDATE] Pack ID not found in ids list:" << packId;
        return;
    }

    // Ensure installed list matches ids list size
    if (m_isInstalledList.size() != m_idsList.size()) {
        qDebug() << "[ASSET UPDATE] Resizing m_isInstalledList from" << m_isInstalledList.size() << "to" << m_idsList.size();
        m_isInstalledList.resize(m_idsList.size());
        // Fill any new entries with false
        for (int i = 0; i < m_isInstalledList.size(); ++i) {
            if (i >= m_isInstalledList.size()) break; // safety
        }
    }

    qDebug() << "[ASSET UPDATE] Updating status for pack:" << packId << "at index:" << index << "to installed:" << isInstalled;
    m_isInstalledList[index] = isInstalled;

    // Defer dataChanged slightly to avoid UI races
    QTimer::singleShot(50, this, [this]() {
        emit dataChanged();
    });
}

void AssetPacks::processUploadQueue()
{
    if (m_uploadQueue.isEmpty() || m_isUploading) {
        qDebug() << "[QUEUE] Queue processing skipped - empty:" << m_uploadQueue.isEmpty() << "uploading:" << m_isUploading;
        return;
    }
    
    QueuedUpload upload = m_uploadQueue.dequeue();
    qDebug() << "[QUEUE] Processing upload for pack:" << upload.packId << "Remaining in queue:" << m_uploadQueue.size();
    
    m_isUploading = true;
    startUpload(upload);
}

void AssetPacks::startUpload(const QueuedUpload &upload)
{
    qDebug() << "[UPLOAD START] Starting upload for pack:" << upload.packId;
    
    // Upload all top-level folders from the archive; utility will recurse and preserve structure
    
    // Debug: Check what's actually in the extracted directory
    qDebug() << "[ASSET DEBUG] Uploading root folders:" << upload.rootFolderNames;
    
    // Count files recursively
    int fileCount = 0;
    for (const QString &rootFolderName : upload.rootFolderNames) {
        const QString localRoot = QDir(upload.extractPath).filePath(rootFolderName);
        QDirIterator it(localRoot, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            fileCount++;
        }
    }
    qDebug() << "[ASSET DEBUG] Total files found recursively:" << fileCount;
    
    // Pass the root directory URL - FilesUploadOperation will handle directory traversal
    QList<QUrl> fileUrls;
    for (const QString &rootFolderName : upload.rootFolderNames) {
        const QString localRoot = QDir(upload.extractPath).filePath(rootFolderName);
        fileUrls.append(QUrl::fromLocalFile(localRoot));
        qDebug() << "[ASSET DEBUG] Queueing root directory for upload:" << localRoot;
    }
    
    // Disable virtual display/screen stream during upload to avoid RPC/UI races
    if (m_backend && m_backend->device() && m_backend->device()->deviceState()) {
        m_backend->device()->deviceState()->setAllowVirtualDisplay(false);
    }
    if (m_backend) {
        m_backend->stopFullScreenStreaming();
    }
    qDebug() << "[UPLOAD START] Starting file upload operation";
    Flipper::Zero::FilesUploadOperation *uploadOp = m_backend->device()->utility()->uploadFiles(fileUrls, upload.flipperParentPath.toUtf8());
    
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
            emit installProgress(upload.packId, progress);
        } catch (...) {
            qDebug() << "[PROGRESS] Exception in progress handler";
        }
    });
    
    // Force initial progress update
    emit installProgress(upload.packId, 50);
    
    connect(uploadOp, &AbstractOperation::finished, this, [=]() {
        try {
            qDebug() << "[FINISH] Upload operation finished for pack:" << upload.packId;
            if (uploadOp->isError()) {
                qDebug() << "[FINISH] Upload error:" << uploadOp->errorString();
                emit installFinished(upload.packId, false, QString("Upload failed: %1").arg(uploadOp->errorString()));
            } else {
                qDebug() << "[FINISH] Upload successful for pack:" << upload.packId;
                emit installProgress(upload.packId, 100);
                emit installFinished(upload.packId, true, "Asset pack installed successfully");
                
                // Update asset pack status to show as installed
                updateAssetPackStatus(upload.packId, true);
                
                // Create asset pack manifest file immediately for detection
                qDebug() << "[INSTALL] Creating manifest immediately after upload completion";
                // Pass first folder name for backward-compat; manifest will include all folders from JSON
                const QString firstFolder = upload.rootFolderNames.isEmpty() ? QString() : upload.rootFolderNames.first();
                createAssetPackManifest(upload.packId, firstFolder);
            }
            
            // Clean up progress timer safely
            if (progressTimerGuard) {
                progressTimerGuard->stop();
                progressTimerGuard->deleteLater();
            }
            
            // Clean up temp directory
            if (upload.tempDir) {
                QTimer::singleShot(1000, [upload]() {
                    if (upload.tempDir) {
                        delete upload.tempDir;
                    }
                });
            }
            
            // Mark upload as finished and process next in queue
            m_isUploading = false;
            qDebug() << "[QUEUE] Upload finished for pack:" << upload.packId << "Processing next in queue";
            
            // Process next item in queue
            QTimer::singleShot(500, this, [this]() {
                processUploadQueue();
            });
            
            // Re-enable virtual display after upload completes
            QTimer::singleShot(2000, this, [this]() {
                try {
                    if (m_backend && m_backend->device() && m_backend->device()->deviceState()) {
                        m_backend->device()->deviceState()->setAllowVirtualDisplay(true);
                    }
                    qDebug() << "[FINISH] Virtual display re-enabled";
                } catch (...) {
                    qDebug() << "[FINISH] Exception while re-enabling virtual display";
                }
            });
            
        } catch (...) {
            qDebug() << "[FINISH] Exception in upload finished handler";
            m_isUploading = false;
            // Still try to process next in queue
            QTimer::singleShot(500, this, [this]() {
                processUploadQueue();
            });
        }
    });
    
    uploadOp->start();
}

void AssetPacks::refreshInstalledPacks()
{
    qDebug() << "[MANIFEST] Manual refresh of installed packs requested";
    if (m_backend && m_backend->device()) {
        checkInstalledPacks();
    } else {
        qDebug() << "[MANIFEST] Cannot refresh - no device connected";
    }
}

void AssetPacks::forceRefreshDetection()
{
    qDebug() << "[MANIFEST] Force refresh detection requested";
    if (m_backend && m_backend->device()) {
        // Clear current status first
        updateAllPackStatuses(false);
        
        // Wait a moment then do a fresh detection
        QTimer::singleShot(500, this, [this]() {
            qDebug() << "[MANIFEST] Performing forced detection refresh";
            checkInstalledPacks();
        });
    } else {
        qDebug() << "[MANIFEST] Cannot force refresh - no device connected";
    }
}

void AssetPacks::onManifestCreated(const QString &packId)
{
    qDebug() << "[MANIFEST] Manifest created for pack:" << packId << "- refreshing detection immediately";
    
    // First, ensure the pack is marked as installed locally
    updateAssetPackStatus(packId, true);
    
    // Then refresh detection to ensure consistency
    checkInstalledPacks();
    
    // Also do a second check after a short delay to ensure detection is working
    QTimer::singleShot(2000, this, [this, packId]() {
        qDebug() << "[MANIFEST] Second detection check for pack:" << packId;
        checkInstalledPacks();
    });
}

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

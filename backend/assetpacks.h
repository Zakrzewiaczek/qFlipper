#pragma once
#include <memory>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMap>
#include <QStringList>
#include <QQueue>
#include <QTemporaryDir>

namespace Flipper
{
    namespace Zero
    {
        class FlipperZero;
    }
}

class ApplicationBackend;

class AssetPacks : public QObject
{
    Q_OBJECT

public:
    // Upload queue system
    struct QueuedUpload
    {
        QString packId;
        QString extractPath;
        QStringList rootFolderNames;
        QString flipperParentPath;
        QTemporaryDir *tempDir;
    };
    Q_PROPERTY(bool errorOccured READ errorOccured NOTIFY errorOccuredChanged)
    Q_PROPERTY(int count READ count NOTIFY dataChanged)
    Q_PROPERTY(QStringList idsList READ idsList NOTIFY dataChanged)
    Q_PROPERTY(QStringList titlesList READ titlesList NOTIFY dataChanged)
    Q_PROPERTY(QStringList authorsList READ authorsList NOTIFY dataChanged)
    Q_PROPERTY(QStringList descriptionsList READ descriptionsList NOTIFY dataChanged)
    Q_PROPERTY(QList<QStringList> previewUrlsList READ previewUrlsList NOTIFY dataChanged)
    Q_PROPERTY(QStringList sourceUrlsList READ sourceUrlsList NOTIFY dataChanged)
    Q_PROPERTY(QStringList zipUrlsList READ zipUrlsList NOTIFY dataChanged)
    Q_PROPERTY(QStringList targzUrlsList READ targzUrlsList NOTIFY dataChanged)
    Q_PROPERTY(QStringList targzSha256List READ targzSha256List NOTIFY dataChanged)
    Q_PROPERTY(QList<bool> isInstalledList READ isInstalledList NOTIFY dataChanged)
    Q_PROPERTY(QList<bool> isInQueueList READ isInQueueList NOTIFY dataChanged)
    Q_PROPERTY(QList<bool> needsUpdateList READ needsUpdateList NOTIFY dataChanged)
    Q_PROPERTY(QList<int> packsList READ packsList NOTIFY dataChanged)
    Q_PROPERTY(QList<int> animsList READ animsList NOTIFY dataChanged)
    Q_PROPERTY(QList<int> iconsList READ iconsList NOTIFY dataChanged)
    Q_PROPERTY(QList<QStringList> fontsList READ fontsList NOTIFY dataChanged)
    Q_PROPERTY(QList<QStringList> passportList READ passportList NOTIFY dataChanged)
    Q_PROPERTY(QList<QStringList> foldersList READ foldersList NOTIFY dataChanged)
    Q_PROPERTY(QStringList lastUpdatedList READ lastUpdatedList NOTIFY dataChanged)
    Q_PROPERTY(QStringList addedList READ addedList NOTIFY dataChanged)

public:
    explicit AssetPacks(ApplicationBackend *backend, QObject *parent = nullptr);

    Q_INVOKABLE void fetchJson(const QUrl &url);
    Q_INVOKABLE void downloadAndSaveFile(const QString &fileUrl);
    Q_INVOKABLE void performDownload(const QString &fileUrl, const QString &savePath);

    // Asset pack installation and management
    Q_INVOKABLE void installAssetPack(const QString &packId, const QString &packUrl);
    Q_INVOKABLE void uninstallAssetPack(const QString &packId);
    Q_INVOKABLE void checkInstalledPacks();
    Q_INVOKABLE void updateAssetPackStatus(const QString &packId, bool isInstalled);
    Q_INVOKABLE void refreshInstalledPacks();
    Q_INVOKABLE void forceRefreshDetection();

private:
    void updateAllPackStatuses(bool isInstalled);
    void updatePackStatusesFromInstalledList(const QStringList &installedPacks);
    void checkSha256ForInstalledPacks(const QStringList &installedPacks);
    void updateAssetPackQueueStatus(const QString &packId, bool isInQueue);
    void updateAllQueueStatuses();
    void createAssetPackManifest(const QString &packId, const QString &actualFolderName);
    void processUploadQueue();
    void startUpload(const QueuedUpload &upload);

private:
    void processExtractedFiles(const QString &packId, const QString &extractPath, QTemporaryDir *tempDir);

    // QML property accessors
    bool errorOccured() const { return m_errorOccured; }
    int count() const { return m_titlesList.size(); }
    QStringList idsList() const { return m_idsList; }
    QStringList titlesList() const { return m_titlesList; }
    QStringList authorsList() const { return m_authorsList; }
    QStringList descriptionsList() const { return m_descriptionsList; }
    QList<QStringList> previewUrlsList() const { return m_previewUrlsList; }
    QStringList sourceUrlsList() const { return m_sourceUrlsList; }
    QStringList zipUrlsList() const { return m_zipUrlsList; }
    QStringList targzUrlsList() const { return m_targzUrlsList; }
    QStringList targzSha256List() const { return m_targzSha256List; }
    QList<bool> isInstalledList() const { return m_isInstalledList; }
    QList<bool> isInQueueList() const { return m_isInQueueList; }
    QList<bool> needsUpdateList() const { return m_needsUpdateList; }
    QList<int> packsList() const { return m_packsList; }
    QList<int> animsList() const { return m_animsList; }
    QList<int> iconsList() const { return m_iconsList; }
    QList<QStringList> fontsList() const { return m_fontsList; }
    QList<QStringList> passportList() const { return m_passportList; }
    QList<QStringList> foldersList() const { return m_foldersList; }
    QStringList lastUpdatedList() const { return m_lastUpdatedList; }
    QStringList addedList() const { return m_addedList; }

signals:
    void jsonFetched(const QByteArray &data);
    void fetchFailed(const QString &errorString);
    void errorOccuredChanged();
    void dataChanged();
    void downloadStarted();
    void downloadFinished(bool success, const QString &message, const QString &fileUrl);
    void requestSaveFile(const QString &fileUrl, const QString &suggestedFileName);

    // Asset pack installation signals
    void installStarted(const QString &packId);
    void installProgress(const QString &packId, int progress);
    void installFinished(const QString &packId, bool success, const QString &message);
    void uninstallFinished(const QString &packId, bool success, const QString &message);
    void manifestCreated(const QString &packId);

private slots:
    void onReplyFinished();
    void onDownloadReplyFinished();
    void onManifestCreated(const QString &packId);

private:
    void parseJson(const QByteArray &data);
    void clearData();

    ApplicationBackend *m_backend;
    QNetworkAccessManager m_networkManager;
    QNetworkReply *m_currentReply = nullptr;
    QNetworkReply *m_downloadReply = nullptr;

    bool m_errorOccured = false;
    QStringList m_idsList;
    QStringList m_titlesList;
    QStringList m_authorsList;
    QStringList m_descriptionsList;
    QList<QStringList> m_previewUrlsList;
    QStringList m_sourceUrlsList;
    QStringList m_zipUrlsList;
    QStringList m_targzUrlsList;
    QStringList m_targzSha256List;
    QList<bool> m_isInstalledList;
    QList<bool> m_isInQueueList;
    QList<bool> m_needsUpdateList;
    QList<int> m_packsList;
    QList<int> m_animsList;
    QList<int> m_iconsList;
    QList<QStringList> m_fontsList;
    QList<QStringList> m_passportList;
    QList<QStringList> m_foldersList;
    QStringList m_lastUpdatedList;
    QStringList m_addedList;
    // Helper for previewUrls (flattened)
    QStringList m_previewUrlsFlat;
    // Map to store actual folder names for each pack ID
    QMap<QString, QString> m_extractedFolderNames;

    // Upload queue system
    QQueue<QueuedUpload> m_uploadQueue;
    bool m_isUploading = false;
};

extern AssetPacks *globalAssetPacks;

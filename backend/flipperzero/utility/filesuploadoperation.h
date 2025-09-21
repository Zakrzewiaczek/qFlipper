#pragma once

#include "abstractutilityoperation.h"

#include <QUrl>
#include <QDir>
#include <QFileInfo>

namespace Flipper {
namespace Zero {

class FilesUploadOperation : public AbstractUtilityOperation
{
    Q_OBJECT

    enum State {
        ReadingFileList = AbstractOperation::User,
        WritingFiles
    };

    struct FileListElement {
        QFileInfo fileInfo;
        QDir topmostDir;
    };

public:
    FilesUploadOperation(ProtobufSession *rpc, DeviceState *deviceState, const QList<QUrl> &fileUrls,
                         const QByteArray &remotePath, QObject *parent = nullptr);
    const QString description() const override;
    
    // Method to disable file timeout for firmware updates
    void setFileTimeoutEnabled(bool enabled) { m_enableFileTimeout = enabled; }

private slots:
    void nextStateLogic() override;

private:
    void readFileList();
    void writeFiles();
    void processNextFile();

    QByteArray m_remotePath;
    QList<QUrl> m_urlList;
    QList<FileListElement> m_fileList;
    qint64 m_totalSize;
    double m_progressBase;
    int m_currentFileIndex;
    int m_totalFiles;
    bool m_enableFileTimeout;
};

}
}


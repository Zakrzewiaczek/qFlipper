#include "zipuncompressor.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QDir>
#include <QFileInfo>
#include <QDataStream>
#include <QtEndian>
#include <zlib.h>

ZipUncompressor::ZipUncompressor(QFile *zipFile, const QDir &targetDir, QObject *parent)
    : QObject(parent), m_zipFile(zipFile), m_targetDir(targetDir), m_watcher(new QFutureWatcher<void>(this))
{
    connect(m_watcher, &QFutureWatcherBase::finished, this, [=]() {
        emit finished();
        m_watcher->deleteLater();
    });
#if QT_VERSION < 0x060000
    m_watcher->setFuture(QtConcurrent::run(this, &ZipUncompressor::extract));
#else
    m_watcher->setFuture(QtConcurrent::run(&ZipUncompressor::extract, this));
#endif
}

void ZipUncompressor::extract()
{
    qDebug() << "[ZIP DEBUG] Starting ZIP extraction";
    qDebug() << "[ZIP DEBUG] ZIP file:" << m_zipFile->fileName();
    qDebug() << "[ZIP DEBUG] Target directory:" << m_targetDir.absolutePath();
    
    if(!m_zipFile->open(QIODevice::ReadOnly)) {
        qDebug() << "[ZIP DEBUG] Failed to open ZIP file:" << m_zipFile->errorString();
        setError(BackendError::DiskError, m_zipFile->errorString());
        return;
    }
    
    qDebug() << "[ZIP DEBUG] ZIP file opened successfully, size:" << m_zipFile->size();
    
    QString err;
    if(!extractAll(m_zipFile, m_targetDir, err)) {
        qDebug() << "[ZIP DEBUG] Extraction failed:" << err;
        setError(BackendError::DataError, err);
    } else {
        qDebug() << "[ZIP DEBUG] Extraction completed successfully";
    }
    m_zipFile->close();
}

// Minimal ZIP reader: supports local file headers with store/deflate and no encryption
// This is a simplified extractor for our asset packs; robust error checks omitted for brevity
static quint16 rd16(QIODevice *in) { quint16 v; in->read(reinterpret_cast<char*>(&v), 2); return qFromLittleEndian(v); }
static quint32 rd32(QIODevice *in) { quint32 v; in->read(reinterpret_cast<char*>(&v), 4); return qFromLittleEndian(v); }

static inline quint16 rd16buf(const char *p) { quint16 v; memcpy(&v, p, 2); return qFromLittleEndian(v); }
static inline quint32 rd32buf(const char *p) { quint32 v; memcpy(&v, p, 4); return qFromLittleEndian(v); }

bool ZipUncompressor::extractAll(QIODevice *in, const QDir &outDir, QString &errorString)
{
    qDebug() << "[ZIP DEBUG] Starting extractAll";
    
    if (in->isSequential()) { errorString = QStringLiteral("ZIP device must be seekable"); return false; }

    const quint32 SIG_EOCD = 0x06054b50;
    const quint32 SIG_CDH  = 0x02014b50;
    const quint32 SIG_LFH  = 0x04034b50;

    // Find End Of Central Directory (EOCD)
    const qint64 fileSize = in->size();
    qDebug() << "[ZIP DEBUG] File size:" << fileSize;
    
    if (fileSize < 22) { errorString = QStringLiteral("ZIP too small"); return false; }
    const qint64 searchLen = qMin<qint64>(fileSize, 65557); // 64K comment + EOCD
    qDebug() << "[ZIP DEBUG] Search length:" << searchLen;
    
    if (!in->seek(fileSize - searchLen)) { errorString = QStringLiteral("seek failed"); return false; }
    const QByteArray tail = in->read(searchLen);
    if (tail.size() != searchLen) { errorString = QStringLiteral("read tail failed"); return false; }

    qDebug() << "[ZIP DEBUG] Read tail bytes:" << tail.size();
    
    int eocdIndex = -1;
    for (int i = tail.size() - 22; i >= 0; --i) {
        quint32 sig = rd32buf(tail.constData() + i);
        qDebug() << "[ZIP DEBUG] Checking signature at offset" << i << ":" << QString::number(sig, 16);
        if (sig == SIG_EOCD) { 
            eocdIndex = i; 
            qDebug() << "[ZIP DEBUG] Found EOCD at index:" << eocdIndex;
            break; 
        }
    }
    if (eocdIndex < 0) { 
        qDebug() << "[ZIP DEBUG] EOCD not found, checking first few bytes";
        // Debug: show first few bytes
        for (int i = 0; i < qMin(20, tail.size()); i++) {
            qDebug() << "[ZIP DEBUG] Byte" << i << ":" << QString::number((unsigned char)tail[i], 16);
        }
        errorString = QStringLiteral("EOCD not found"); 
        return false; 
    }

    const char *e = tail.constData() + eocdIndex;
    // Skip signature (4), disk nums (2+2), disk entries (2)
    const quint16 totalEntries = rd16buf(e + 10);
    const quint32 cdSize       = rd32buf(e + 12);
    const quint32 cdOffset     = rd32buf(e + 16);
    Q_UNUSED(cdSize);

    qDebug() << "[ZIP DEBUG] Total entries:" << totalEntries;
    qDebug() << "[ZIP DEBUG] CD size:" << cdSize;
    qDebug() << "[ZIP DEBUG] CD offset:" << cdOffset;

    if (!in->seek(cdOffset)) { errorString = QStringLiteral("seek CD failed"); return false; }

    // Try to parse central directory entries
    qint64 currentPos = cdOffset;
    int validEntries = 0;
    
    for (quint16 i = 0; i < totalEntries; ++i) {
        if (!in->seek(currentPos)) { 
            qDebug() << "[ZIP DEBUG] Failed to seek to position" << currentPos;
            break; 
        }
        
        // Read central directory header
        char header[46];
        if (in->read(header, 46) != 46) { 
            qDebug() << "[ZIP DEBUG] Failed to read header at entry" << i;
            break; 
        }
        
        quint32 sig = rd32buf(header);
        qDebug() << "[ZIP DEBUG] Entry" << i << "signature:" << QString::number(sig, 16) << "expected:" << QString::number(SIG_CDH, 16);
        
        if (sig != SIG_CDH) { 
            qDebug() << "[ZIP DEBUG] Invalid signature at entry" << i << "- stopping parsing";
            break; 
        }
        
        validEntries++;

        const quint16 gpFlags     = rd16buf(header + 8);
        const quint16 method      = rd16buf(header + 10);
        const quint32 compSize    = rd32buf(header + 20);
        const quint32 uncompSize  = rd32buf(header + 24);
        const quint16 nameLen     = rd16buf(header + 28);
        const quint16 extraLen    = rd16buf(header + 30);
        const quint16 commentLen  = rd16buf(header + 32);
        const quint32 relOffLocal = rd32buf(header + 42);

        qDebug() << "[ZIP DEBUG] Processing entry" << i << "nameLen:" << nameLen << "extraLen:" << extraLen << "commentLen:" << commentLen;

        // Read name/extra/comment
        QByteArray name = in->read(nameLen);
        if (name.size() != nameLen) { 
            qDebug() << "[ZIP DEBUG] Failed to read name for entry" << i;
            break; 
        }
        if (extraLen) in->read(extraLen);
        if (commentLen) in->read(commentLen);
        
        // Update position for next entry
        currentPos = in->pos();

        QString relPath = QString::fromUtf8(name);
        relPath.replace("\\", "/");
        if (relPath.startsWith('/') || relPath.contains("..")) { continue; }

        // Seek to local header to find data start
        if (!in->seek(relOffLocal)) { errorString = QStringLiteral("seek LFH failed"); return false; }
        if (rd32(in) != SIG_LFH) { errorString = QStringLiteral("bad LFH signature"); return false; }
        rd16(in); // version
        rd16(in); // flags (redundant)
        const quint16 lfhMethod = rd16(in); Q_UNUSED(lfhMethod);
        rd16(in); rd16(in); // time/date
        rd32(in); // crc
        rd32(in); // comp size (may be 0 if descriptor)
        rd32(in); // uncomp size
        const quint16 lfhNameLen  = rd16(in);
        const quint16 lfhExtraLen = rd16(in);
        if (lfhNameLen) in->read(lfhNameLen);
        if (lfhExtraLen) in->read(lfhExtraLen);
        const qint64 dataPos = in->pos();

        QFileInfo fi(outDir.filePath(relPath));
        if (relPath.endsWith('/')) {
            QDir().mkpath(fi.absoluteFilePath());
            continue;
        }
        QDir().mkpath(fi.dir().absolutePath());

        QFile outFile(fi.absoluteFilePath());
        if (!outFile.open(QIODevice::WriteOnly)) { errorString = outFile.errorString(); return false; }

        if (gpFlags & 0x0001) { // encrypted
            errorString = QStringLiteral("Encrypted ZIP entry not supported");
            return false;
        }

        if (method == 0) {
            // Stored
            if (!in->seek(dataPos)) { errorString = QStringLiteral("seek data failed"); return false; }
            qint64 left = compSize;
            QByteArray buf(64 * 1024, Qt::Uninitialized);
            while (left > 0) {
                const qint64 chunk = qMin<qint64>(left, buf.size());
                const qint64 n = in->read(buf.data(), chunk);
                if (n <= 0) { errorString = QStringLiteral("unexpected EOF in stored entry"); return false; }
                if (outFile.write(buf.constData(), n) != n) { errorString = QStringLiteral("short write"); return false; }
                left -= n;
            }
        } else if (method == 8) {
            // Deflate
            if (!in->seek(dataPos)) { errorString = QStringLiteral("seek data failed"); return false; }
            z_stream strm{};
            if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) { errorString = QStringLiteral("inflate init failed"); return false; }
            QByteArray inChunk(64 * 1024, Qt::Uninitialized);
            QByteArray outChunk(64 * 1024, Qt::Uninitialized);
            qint64 left = compSize;
            int ret = Z_OK;
            while (left > 0 && ret != Z_STREAM_END) {
                const qint64 toRead = qMin<qint64>(left, inChunk.size());
                const qint64 n = in->read(inChunk.data(), toRead);
                if (n <= 0) { errorString = QStringLiteral("unexpected EOF in deflate entry"); inflateEnd(&strm); return false; }
                left -= n;
                strm.next_in = reinterpret_cast<Bytef*>(inChunk.data());
                strm.avail_in = static_cast<uInt>(n);
                do {
                    strm.next_out = reinterpret_cast<Bytef*>(outChunk.data());
                    strm.avail_out = static_cast<uInt>(outChunk.size());
                    ret = inflate(&strm, Z_NO_FLUSH);
                    if (ret != Z_OK && ret != Z_STREAM_END) { errorString = QStringLiteral("inflate failed"); inflateEnd(&strm); return false; }
                    const int have = static_cast<int>(outChunk.size() - strm.avail_out);
                    if (have > 0) {
                        if (outFile.write(outChunk.constData(), have) != have) { errorString = QStringLiteral("short write"); inflateEnd(&strm); return false; }
                    }
                } while (strm.avail_in > 0 && ret != Z_STREAM_END);
            }
            inflateEnd(&strm);
        } else {
            errorString = QStringLiteral("Unsupported ZIP method");
            return false;
        }

        outFile.close();
    }

    qDebug() << "[ZIP DEBUG] Successfully processed" << validEntries << "out of" << totalEntries << "entries";
    
    if (validEntries == 0) {
        errorString = QStringLiteral("No valid entries found in ZIP");
        return false;
    }

    return true;
}



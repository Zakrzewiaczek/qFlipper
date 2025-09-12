#pragma once

#include "abstractprotobufoperation.h"
#include "protobufplugininterface.h"

#include <QByteArray>

namespace Flipper {
namespace Zero {

class AbstractStorageOperation : public AbstractProtobufOperation
{
    Q_OBJECT

public:
    AbstractStorageOperation(uint32_t id, const QByteArray &path, QObject *parent = nullptr);
    virtual ~AbstractStorageOperation();

protected:
    const QByteArray &path() const;

private:
    QByteArray m_path;
};

class StorageTarExtractOperation : public AbstractProtobufOperation
{
    Q_OBJECT
public:
    StorageTarExtractOperation(uint32_t id, const QByteArray &tarPath, const QByteArray &outPath, QObject *parent = nullptr)
        : AbstractProtobufOperation(id, parent), m_tarPath(tarPath), m_outPath(outPath) {}
    const QString description() const override { return QStringLiteral("Storage TarExtract @%1 -> %2").arg(QString(m_tarPath), QString(m_outPath)); }
    const QByteArray encodeRequest(ProtobufPluginInterface *encoder) override { return encoder->storageTarExtract(id(), m_tarPath, m_outPath); }
private:
    QByteArray m_tarPath;
    QByteArray m_outPath;
};

}
}


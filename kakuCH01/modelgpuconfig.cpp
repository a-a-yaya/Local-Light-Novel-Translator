#include "modelgpuconfig.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QVector>
#include <algorithm>

namespace {
constexpr quint64 MiB = 1024 * 1024;
QString settingKey(const QString &engine, const QString &path)
{
    if (!ModelGpuConfig::contextLength(engine) || path.trimmed().isEmpty()) return {};
    QFileInfo file(path);
    QString identity = file.canonicalFilePath();
    if (identity.isEmpty()) identity = file.absoluteFilePath();
    identity = QDir::cleanPath(identity);
#ifdef Q_OS_WIN
    identity = identity.toCaseFolded();
#endif
    const auto hash = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("modelGpu/%1/%2/layers").arg(engine, QString::fromLatin1(hash));
}
QVariantMap error(const QString &message)
{
    return {{"success", false}, {"message", message}};
}

// Only metadata and tensor offsets are read. Weight data is never loaded.
class GgufReader {
public:
    explicit GgufReader(const QString &path) : file(path), stream(&file) {
        stream.setByteOrder(QDataStream::LittleEndian);
    }
    bool skip(quint64 size) {
        if (size > quint64(file.size() - file.pos())) return false;
        return file.seek(file.pos() + qint64(size));
    }
    bool string(QString *text = nullptr) {
        quint64 length = 0; stream >> length;
        if (stream.status() != QDataStream::Ok || length > 64 * MiB) return false;
        if (!text) return skip(length);
        if (length > 65536 || length > quint64(file.size() - file.pos())) return false;
        *text = QString::fromUtf8(file.read(qint64(length)));
        return true;
    }
    bool value(quint32 type, quint64 *number, QString *text) {
        static const int sizes[] = {1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8};
        if (type > 12) return false;
        if (type == 8) return string(text);
        if (type == 9) {
            quint32 itemType = 0; quint64 count = 0; stream >> itemType >> count;
            if (itemType > 12 || itemType == 9 || count > 4000000) return false;
            if (itemType == 8) {
                for (quint64 i = 0; i < count; ++i) if (!string()) return false;
                return true;
            }
            return skip(count * sizes[itemType]);
        }
        // Integer metadata is enough for architecture, attention dimensions and alignment.
        if (number && type != 6 && type != 12) {
            if (sizes[type] == 1) { quint8 n; stream >> n; *number = n; }
            if (sizes[type] == 2) { quint16 n; stream >> n; *number = n; }
            if (sizes[type] == 4) { quint32 n; stream >> n; *number = n; }
            if (sizes[type] == 8) { quint64 n; stream >> n; *number = n; }
            return stream.status() == QDataStream::Ok;
        }
        return skip(sizes[type]);
    }
    QFile file;
    QDataStream stream;
};

struct ModelMemory {
    QVector<quint64> weights;
    QVector<quint64> cache;
    quint64 outputWeights = 0;
};
bool readMemory(const QString &path, int context, ModelMemory *result)
{
    GgufReader reader(path);
    if (!reader.file.open(QIODevice::ReadOnly)) return false;
    quint32 magic = 0, version = 0;
    quint64 tensors = 0, entries = 0;
    reader.stream >> magic >> version >> tensors >> entries;
    if (magic != 0x46554747 || version < 2 || version > 3
        || tensors == 0 || tensors > 200000 || entries > 100000) return false;
    QHash<QString, quint64> numbers;
    QString architecture;
    for (quint64 i = 0; i < entries; ++i) {
        if (reader.file.pos() > qint64(64 * MiB)) return false;
        QString key; quint32 type = 0; quint64 n = 0;
        if (!reader.string(&key)) return false;
        reader.stream >> type;
        if (!reader.value(type, &n, key == QStringLiteral("general.architecture") ? &architecture : nullptr)) return false;
        numbers.insert(key, n);
    }
    const QString prefix = architecture + QLatin1Char('.');
    const quint64 blocks = numbers.value(prefix + "block_count");
    const quint64 embedding = numbers.value(prefix + "embedding_length");
    const quint64 heads = numbers.value(prefix + "attention.head_count");
    const quint64 kvHeads = numbers.value(prefix + "attention.head_count_kv", heads);
    const quint64 keyLength = numbers.value(prefix + "attention.key_length", heads ? embedding / heads : 0);
    const quint64 valueLength = numbers.value(prefix + "attention.value_length", keyLength);
    if (architecture.isEmpty() || blocks == 0 || blocks > 512 || heads == 0 || kvHeads == 0
        || kvHeads > 512 || keyLength == 0 || keyLength > 8192 || valueLength == 0 || valueLength > 8192) return false;
    result->weights.fill(0, int(blocks));
    result->cache.fill(quint64(context) * kvHeads * (keyLength + valueLength) * 2, int(blocks));
    const quint64 interval = numbers.value(prefix + "full_attention_interval");
    if (interval) {
        const quint64 state = numbers.value(prefix + "ssm.state_size");
        const quint64 inner = numbers.value(prefix + "ssm.inner_size");
        const quint64 groups = numbers.value(prefix + "ssm.group_count");
        const quint64 convolution = numbers.value(prefix + "ssm.conv_kernel");
        if (!state || state > 8192 || !inner || inner > 131072 || groups > 512 || !convolution || convolution > 64) return false;
        // Hybrid Qwen attention keeps KV only on attention blocks, with f32 recurrent states elsewhere.
        const quint64 recurrent = 4 * (state * inner + convolution * (inner + 2 * groups * state));
        for (int i = 0; i < int(blocks); ++i)
            if ((i + 1) % interval != 0) result->cache[i] = recurrent;
    }
    struct Tensor { QString name; quint64 offset; };
    QVector<Tensor> offsets;
    for (quint64 i = 0; i < tensors; ++i) {
        if (reader.file.pos() > qint64(64 * MiB)) return false;
        Tensor tensor; quint32 dimensions = 0, type = 0;
        if (!reader.string(&tensor.name)) return false;
        reader.stream >> dimensions;
        if (dimensions == 0 || dimensions > 4 || !reader.skip(8 * dimensions)) return false;
        reader.stream >> type >> tensor.offset;
        if (reader.stream.status() != QDataStream::Ok) return false;
        offsets.append(tensor);
    }
    const quint64 alignment = numbers.value("general.alignment", 32);
    if (alignment == 0 || alignment > 4096 || (alignment & (alignment - 1))) return false;
    const quint64 dataStart = (quint64(reader.file.pos()) + alignment - 1) / alignment * alignment;
    if (dataStart >= quint64(reader.file.size())) return false;
    const quint64 dataSize = quint64(reader.file.size()) - dataStart;
    std::sort(offsets.begin(), offsets.end(), [](const Tensor &a, const Tensor &b) { return a.offset < b.offset; });
    const QRegularExpression blockPattern(QStringLiteral("^blk\\.(\\d+)\\."));
    for (int i = 0; i < offsets.size(); ++i) {
        const quint64 next = i + 1 < offsets.size() ? offsets[i + 1].offset : dataSize;
        if (next > dataSize || offsets[i].offset >= next) return false;
        const quint64 bytes = next - offsets[i].offset;
        const auto match = blockPattern.match(offsets[i].name);
        if (match.hasMatch()) {
            const int block = match.captured(1).toInt();
            if (block >= int(blocks)) return false;
            result->weights[block] += bytes;
        } else if (!offsets[i].name.startsWith(QStringLiteral("token_embd."))) {
            result->outputWeights += bytes;
        }
    }
    return std::all_of(result->weights.cbegin(), result->weights.cend(), [](quint64 bytes) { return bytes > 0; });
}
}

int ModelGpuConfig::contextLength(const QString &engine)
{
    if (engine == "sakura") return 6144;
    if (engine == "murasaki") return 8192;
    if (engine == "qwen") return 16384;
    return 0;
}

int ModelGpuConfig::layersFor(const QString &engine, const QString &modelPath)
{
    const QString key = settingKey(engine, modelPath);
    if (key.isEmpty()) return DefaultLayers;
    bool valid = false;
    const int layers = QSettings().value(key, DefaultLayers).toInt(&valid);
    return valid && layers >= 0 && layers <= DefaultLayers ? layers : DefaultLayers;
}

bool ModelGpuConfig::save(const QString &engine, const QString &modelPath, int layers)
{
    const QString key = settingKey(engine, modelPath);
    const QFileInfo file(modelPath);
    if (key.isEmpty() || layers < 0 || layers > DefaultLayers || !file.isFile()
        || file.suffix().compare("gguf", Qt::CaseInsensitive) != 0) return false;
    QSettings settings;
    settings.setValue(key, layers);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QVariantMap ModelGpuConfig::configuration(const QString &engine, const QString &modelPath)
{
    return {{"gpuLayers", layersFor(engine, modelPath)}, {"defaultLayers", DefaultLayers},
            {"contextLength", contextLength(engine)}};
}

QVariantMap ModelGpuConfig::recommendForMemory(const QString &engine, const QString &path,
                                              quint64 freeBytes, quint64 totalBytes)
{
    const int context = contextLength(engine);
    if (!context || !totalBytes || freeBytes > totalBytes) return error(QStringLiteral("显存数据无效，请手动配置"));
    ModelMemory model;
    if (!readMemory(path, context, &model))
        return error(QStringLiteral("无法估算此 GGUF 的模型和缓存需求，请手动配置；原设置未更改"));
    // Leave room for compute buffers, CUDA runtime and normal desktop fluctuations.
    const quint64 reserve = std::max<quint64>(512 * MiB, totalBytes / 16);
    const quint64 budget = freeBytes > reserve ? freeBytes - reserve : 0;
    quint64 usage = model.outputWeights;
    int selected = 0;
    quint64 selectedUsage = 0;
    for (int layers = 1; layers <= std::min<int>(model.weights.size(), DefaultLayers); ++layers) {
        const int block = model.weights.size() - layers;
        usage += model.weights[block] + model.cache[block];
        if (usage <= budget) { selected = layers; selectedUsage = usage; }
    }
    const bool full = selected == model.weights.size();
    const int layers = full ? DefaultLayers : selected;
    return {{"success", true}, {"gpuLayers", layers}, {"totalLayers", model.weights.size()},
            {"freeMiB", double(freeBytes) / MiB}, {"reservedMiB", double(reserve) / MiB},
            {"estimatedMiB", double(selectedUsage) / MiB},
            {"message", QStringLiteral("建议 %1；可用显存 %2 GiB，预留 %3 MiB。仅为估算，保存后生效。")
                 .arg(full ? QStringLiteral("保持默认 200 层") : QStringLiteral("%1 个 GPU 层").arg(layers))
                 .arg(double(freeBytes) / (1024 * MiB), 0, 'f', 2).arg(reserve / MiB)}};
}

QVariantMap ModelGpuConfig::recommend(const QString &engine, const QString &modelPath)
{
    QProcess query;
    query.start(QStringLiteral("nvidia-smi"), {QStringLiteral("--query-gpu=memory.free,memory.total"),
                                              QStringLiteral("--format=csv,noheader,nounits")});
    if (!query.waitForStarted(2000) || !query.waitForFinished(4000)) {
        query.kill(); query.waitForFinished(1000);
        return error(QStringLiteral("无法查询 NVIDIA 显存，请手动配置；原设置未更改"));
    }
    const QString row = QString::fromLocal8Bit(query.readAllStandardOutput()).section('\n', 0, 0);
    const auto fields = row.split(',');
    bool freeOk = false, totalOk = false;
    const quint64 available = fields.value(0).trimmed().toULongLong(&freeOk);
    const quint64 total = fields.value(1).trimmed().toULongLong(&totalOk);
    if (query.exitCode() != 0 || !freeOk || !totalOk || total > 1024 * 1024)
        return error(QStringLiteral("无法读取有效显存数据，请手动配置；原设置未更改"));
    return recommendForMemory(engine, modelPath, available * MiB, total * MiB);
}

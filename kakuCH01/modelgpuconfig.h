#ifndef MODELGPUCONFIG_H
#define MODELGPUCONFIG_H

#include <QString>
#include <QVariantMap>

// Startup-only GPU placement. Translation prompts and budgets stay with each engine.
namespace ModelGpuConfig {
constexpr int DefaultLayers = 200;
int contextLength(const QString &engine);
int layersFor(const QString &engine, const QString &modelPath);
bool save(const QString &engine, const QString &modelPath, int layers);
QVariantMap configuration(const QString &engine, const QString &modelPath);
// Explicit user action only: neither function saves settings or starts a model.
QVariantMap recommend(const QString &engine, const QString &modelPath);
QVariantMap recommendForMemory(const QString &engine, const QString &modelPath,
                              quint64 freeBytes, quint64 totalBytes);
}

#endif

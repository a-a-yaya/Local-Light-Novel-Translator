#ifndef GLOSSARY_H
#define GLOSSARY_H

#include <QString>

// 手动维护的小说级术语。source 为用户在日文原文中选中的文本，
// target 为用户确认后的中文译名。origin 固定为 "manual"。
struct GlossaryTerm
{
    QString source;
    QString target;
    QString type;
    QString note;
    bool enabled = true;
    QString origin = QStringLiteral("manual");
};

#endif // GLOSSARY_H

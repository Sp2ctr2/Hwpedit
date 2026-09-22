#pragma once
#include <QString>
#include <QVector>
namespace he {
bool validUnicode(const QString& text);
QVector<int> graphemeBoundaries(const QString& text);
int previousGrapheme(const QString& text, int offset);
int nextGrapheme(const QString& text, int offset);
int previousWord(const QString& text, int offset);
int nextWord(const QString& text, int offset);
bool isGraphemeBoundary(const QString& text, int offset);
QString normalizeNewlines(QString text);
}

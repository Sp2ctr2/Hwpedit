#pragma once
#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <expected>
#include <functional>
#include <memory>
#include <variant>

namespace he {
using Id = quint64;
using Unit = qint64; // HWPUNIT: 1/7200 inch, exactly 100 units per point.
constexpr double points(Unit v) { return double(v) / 100.0; }
Unit units(double pt);
Unit mm(double value);
Id newId();
enum class ErrorCode { Io, InvalidContainer, InvalidHeader, InvalidXml, CorruptRecord, ResourceLimit, UnsupportedEncryption, UnsupportedFeature, UnsafeSave, InvalidEdit };
struct Error { ErrorCode code; QString part; QString message; };
template<class T> using Result = std::expected<T, Error>;
struct Limits {
    quint64 fileBytes = 128ULL << 20;
    quint64 partBytes = 32ULL << 20;
    quint64 expandedBytes = 256ULL << 20;
    int zipEntries = 4096;
    int xmlDepth = 96;
    int xmlNodes = 1000000;
    int paragraphs = 200000;
    int tableCells = 100000;
    int objectDepth = 24;
    int pages = 10000;
};
struct CharStyle {
    QString family = QStringLiteral("Noto Serif CJK KR");
    QStringList languageFonts;
    double size = 10;
    double width = 100;
    double spacing = 0;
    double relativeSize = 100;
    double baseline = 0;
    bool bold = false, italic = false, underline = false, strike = false;
    bool outline = false, shadow = false;
    int script = 0; // -1 subscript, +1 superscript
    QColor color = Qt::black, background = Qt::transparent;
    QColor underlineColor = Qt::black, shadowColor = Qt::gray;
    QString sourceId;
    bool operator==(const CharStyle&) const = default;
};
enum class Alignment { Left, Center, Right, Justify, Distributed };
enum class SpacingKind { Percent, Fixed, Minimum };
struct ParaStyle {
    Alignment alignment = Alignment::Left;
    Unit left = 0, right = 0, indent = 0, before = 0, after = 0;
    SpacingKind spacingKind = SpacingKind::Percent;
    double lineSpacing = 160;
    bool keepNext = false, keepTogether = false, widowOrphan = true, breakBefore = false;
    QVector<Unit> tabs;
    QString listFormat;
    int listLevel = 0, listStart = 1;
    QString sourceId;
    bool operator==(const ParaStyle&) const = default;
};
struct PageFormat {
    Unit width = 59528, height = 84189;
    Unit left = 5669, right = 5669, top = 5669, bottom = 5669;
    Unit binding = 0, headerDistance = 2835, footerDistance = 2835;
    int columns = 1;
    Unit columnGap = 1417;
    int numberStart = 1;
    int numberPosition = 0; // 0 off, 1..3 top, 4..6 bottom
    bool operator==(const PageFormat&) const = default;
};
struct Source {
    QByteArray xml;
    QByteArray raw;
    QString part;
    QString key;
    bool opaque = false;
    bool dirty = true;
};
struct Block;
using BlockPtr = std::shared_ptr<const Block>;
struct Flow { Id id = newId(); QVector<BlockPtr> blocks; };
struct Run {
    QString text;
    CharStyle style;
    BlockPtr object;
    QString field;
    QString hyperlink;
    QByteArray sourceXml;
    int length() const { return object || !field.isEmpty() ? 1 : int(text.size()); }
};
struct Paragraph {
    QVector<Run> runs;
    ParaStyle style;
    QString namedStyle;
    QString text() const;
    int length() const;
    CharStyle styleAt(int offset) const;
};
struct Border {
    QColor color = Qt::black;
    double width = 0.5;
    int style = 1; // 0 none, 1 solid, 2 dashed, 3 dotted, 4 double
    bool operator==(const Border&) const = default;
};
struct Cell {
    Id id = newId();
    int row = 0, col = 0, rowSpan = 1, colSpan = 1;
    Flow content;
    Unit left = 283, right = 283, top = 141, bottom = 141;
    Unit minimumHeight = 1800;
    int verticalAlign = 0;
    QColor background = Qt::transparent;
    Border borders[4]; // left, top, right, bottom
    Source source;
};
struct Table {
    int rows = 1, columns = 1;
    QVector<Unit> widths;
    QVector<Cell> cells;
    bool repeatHeading = false;
    bool allowRowSplit = false;
    Alignment alignment = Alignment::Left;
    QString caption;
};
enum class GraphicKind { Image, Line, Arrow, Rectangle, RoundedRectangle, Ellipse, Polygon, TextBox };
enum class Anchor { Inline, Paragraph, Page };
enum class Wrap { Inline, Square, Tight, TopBottom, Behind, Front };
struct Graphic {
    GraphicKind kind = GraphicKind::Image;
    QString resource;
    QString alt;
    Unit x = 0, y = 0, width = 14400, height = 7200;
    Anchor anchor = Anchor::Inline;
    Wrap wrap = Wrap::Inline;
    double rotation = 0, opacity = 1;
    double cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0;
    bool aspectLock = true;
    int z = 0;
    QColor fill = Qt::transparent;
    Border stroke;
    QVector<QPair<Unit,Unit>> polygon;
    Flow content;
};
struct Equation { QString script; double size = 12; };
struct Note { bool endnote = false; Flow content; };
struct Unknown { QString type; QString description; };
struct Block {
    Id id = newId();
    std::variant<Paragraph, Table, Graphic, Equation, Note, Unknown> value;
    Source source;
    const Paragraph* paragraph() const { return std::get_if<Paragraph>(&value); }
    const Table* table() const { return std::get_if<Table>(&value); }
};
struct Section { Id id = newId(); PageFormat page; Flow body, header, footer; Source source; };
struct Style { QString name; QString basedOn; CharStyle character; ParaStyle paragraph; };
struct Resource { QByteArray bytes; QString mime; QString originalPath; };
struct Origin {
    enum class Format { None, Hwpx, Hwp5 } format = Format::None;
    QByteArray original;
    QMap<QString, QByteArray> parts;
    QStringList sectionParts;
    QHash<Id,QByteArray> fingerprints;
    QHash<Id,QString> hwpStreams;
    QHash<Id,int> hwpRecordIndices;
};
struct ParagraphRef { BlockPtr block; Id flow = 0; };
class Document {
public:
    QVector<Section> sections;
    QMap<QString, Style> styles;
    QMap<QString, Resource> resources;
    QMap<QString, QString> metadata;
    QStringList warnings;
    std::shared_ptr<const Origin> origin;
    quint64 revision = 0;
    static Document blank();
    BlockPtr block(Id id) const;
    std::optional<Flow> flow(Id id) const;
    QVector<ParagraphRef> paragraphs(bool includeAuxiliary = true) const;
    Id paragraphFlow(Id id) const;
    bool replaceBlock(Id id, BlockPtr replacement);
    bool replaceFlow(Id id, const Flow& replacement);
    QString plainText() const;
    Result<void> validate(const Limits& limits = {}) const;
};
BlockPtr paragraph(QString text = {}, const CharStyle& style = {});
BlockPtr table(int rows, int columns, Unit width);
QVector<Run> sliceRuns(const Paragraph& p, int start, int end);
QVector<Run> normalizeRuns(QVector<Run> runs);
Result<Paragraph> replaceText(const Paragraph& p, int start, int end, QVector<Run> replacement);
QByteArray semanticDigest(const Document& doc);
QByteArray checkpoint(const Document& doc);
Result<Document> restoreCheckpoint(const QByteArray& bytes, const Limits& limits = {});
} // namespace he

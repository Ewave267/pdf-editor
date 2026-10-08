// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QRectF>
#include <QSet>
#include <QUrl>
#include <QVariantMap>
#include <optional>
class QPainter;
class PdfDocument;
class AddedContent : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int selected READ selected WRITE select NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY changed)
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
  public:
    struct Object
    {
        int id, page;
        QString type, text;
        QRectF rect;
        QImage image;
        QString fontFamily = "Sans Serif";
        int fontSize = 18;
        bool bold = false, italic = false, underline = false;
        QColor color = Qt::black;
        double lineWidth = 2;
        QList<QPointF> points{};
    };
    explicit AddedContent(PdfDocument* document);
    quint64 revision() const { return revision_; }
    bool canUndo() const { return !editing_ && !undo_.isEmpty(); }
    bool canRedo() const { return !editing_ && !redo_.isEmpty(); }
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE void endEdit();
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    int count() const { return objects_.size(); }
    int selected() const { return selected_; }
    int selectionCount() const { return selection_.size(); }
    bool editing() const { return editing_; }
    bool isSelected(int id) const { return selection_.contains(id); }
    QString error() const { return error_; }
    const QList<Object>& objects() const { return objects_; }
    QStringList fontFamilies() const;
    static QFont textFont(const Object& object);
    static void paintObject(QPainter* painter, const Object& object);
    PdfDocument* document() const { return document_; }
    Q_INVOKABLE int addText(int page, double x, double y, const QString& text);
    Q_INVOKABLE int addImage(int page, double x, double y, const QUrl& file,
                             bool signature = false);
    Q_INVOKABLE QVariantMap object(int id) const;
    Q_INVOKABLE int hit(int page, double x, double y) const;
    Q_INVOKABLE bool geometry(int id, double x, double y, double width, double height);
    Q_INVOKABLE bool setText(int id, const QString& text);
    Q_INVOKABLE bool setTextStyle(int id, const QString& family, int size, bool bold, bool italic,
                                  bool underline);
    Q_INVOKABLE void select(int id);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void toggleSelection(int id);
    Q_INVOKABLE void focusObject(int id);
    Q_INVOKABLE void selectAll(int page);
    Q_INVOKABLE void selectRect(int page, double x, double y, double width, double height,
                                bool additive = false);
    Q_INVOKABLE bool moveSelection(double dx, double dy);
    Q_INVOKABLE bool nudgeSelection(double dx, double dy);
    Q_INVOKABLE bool alignSelection(const QString& alignment);
    Q_INVOKABLE bool copySelection();
    Q_INVOKABLE bool cutSelection();
    Q_INVOKABLE bool paste(int page);
    Q_INVOKABLE int addGraphic(int page, double x, double y, const QString& type,
                               const QString& color, double lineWidth, const QString& text = {});
    Q_INVOKABLE bool setAppearance(int id, const QString& color, double lineWidth);
    Q_INVOKABLE bool appendStroke(int id, double x, double y);
    Q_INVOKABLE bool setLine(int id, double x0, double y0, double x1, double y1);
    void clear();
  signals:
    void changed();

  private:
    int insert(int page, double x, double y, const QString& type, const QString& text,
               const QImage& image);
    QRectF bounded(int page, const QRectF& rect) const;
    struct State
    {
        QList<Object> objects;
        int selected;
        quint64 revision;
        QSet<int> selection;
    };
    State state() const;
    void restore(const State& state);
    void recordEdit();
    void trimHistory();
    QList<State> undo_, redo_;
    QList<State> editUndo_, editRedo_;
    std::optional<State> editState_;
    QSet<int> selection_;
    QHash<int, QRectF> editRects_;
    bool replacePath(int id, const QList<QPointF>& points);
    bool editing_ = false, editRecorded_ = false;
    PdfDocument* document_;
    QList<Object> objects_;
    int selected_ = -1, nextId_ = 0;
    QString error_;
    quint64 revision_ = 0;
    quint64 nextRevision_ = 0;
};

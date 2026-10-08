// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QFont>
#include <QImage>
#include <QObject>
#include <QRectF>
#include <QUrl>
#include <QVariantMap>
class PdfDocument;
class AddedContent : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int selected READ selected WRITE select NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
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
    };
    explicit AddedContent(PdfDocument* document);
    quint64 revision() const { return revision_; }
    int count() const { return objects_.size(); }
    int selected() const { return selected_; }
    QString error() const { return error_; }
    const QList<Object>& objects() const { return objects_; }
    QStringList fontFamilies() const;
    static QFont textFont(const Object& object);
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
    void clear();
  signals:
    void changed();

  private:
    int insert(int page, double x, double y, const QString& type, const QString& text,
               const QImage& image);
    QRectF bounded(int page, const QRectF& rect) const;
    PdfDocument* document_;
    QList<Object> objects_;
    int selected_ = -1, nextId_ = 0;
    QString error_;
    quint64 revision_ = 0;
};

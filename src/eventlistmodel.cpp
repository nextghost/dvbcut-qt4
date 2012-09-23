/*  dvbcut
    Copyright (c) 2012 Martin Doucha <next_ghost@quick.cz>
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* $Id$ */

#include "eventlistmodel.h"
#include "settings.h"

#include <QLabel>
#include <QPainter>
#include <QApplication>
#include <cstdio>

EventListModel::EventListModel(QObject *parent) : QAbstractListModel(parent) {

}

EventListModel::~EventListModel(void) {

}

QVariant EventListModel::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() < 0 || index.row() >= _data.size() || role != Qt::DisplayRole) {
		return QVariant();
	}

	return _data[index.row()].pic;
}

Qt::ItemFlags EventListModel::flags(const QModelIndex &index) const {
	if (!index.isValid() || index.row() < 0 || index.row() >= _data.size()) {
		return Qt::NoItemFlags;
	}

	return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

int EventListModel::rowCount(const QModelIndex &parent) const {
	return parent.isValid() ? 0 : _data.size();
}

QModelIndex EventListModel::index(int row, int col, const QModelIndex &parent) const {
	if (parent.isValid() || row < 0 || row >= _data.size()) {
		return QModelIndex();
	}

	return QAbstractItemModel::createIndex(row, col, (void*)&_data[row]);
}

QAbstractItemDelegate *EventListModel::delegate(void) {
	return &_delegate;
}

void EventListModel::addItem(imageprovider *imgp, EventType evtype, int pic, int pictype, pts_t pts) {
	int i;
	EventListItem tmp;
	QPixmap preview;

	// Check for duplicate events and find the right place for insertion
	for (i = 0; i < _data.size() && _data[i].pic <= pic; i++) {
		if (_data[i].pic == pic && _data[i].evtype == evtype) {
			return;
		}
	}

	preview = QPixmap::fromImage(imgp->getimage(pic));

	if (preview.width() > 130 || preview.height() > 90) {
		preview = preview.scaled(130, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}

	tmp.preview = preview;
	tmp.evtype = evtype;
	tmp.pic = pic;
	tmp.pictype = pictype;
	tmp.pts = pts;

	beginInsertRows(QModelIndex(), i, i);
	_data.insert(i, tmp);
	endInsertRows();
}

void EventListModel::convert(QModelIndex &index, EventListModel::EventType evtype) {
	if (!index.isValid() || index.row() < 0 || index.row() >= _data.size()) {
		return;
	}

	_data[index.row()].evtype = evtype;
	emit dataChanged(index, index);
}

void EventListModel::remove(QModelIndex &index) {
	if (!index.isValid() || index.row() < 0 || index.row() >= _data.size()) {
		return;
	}

	beginRemoveRows(QModelIndex(), index.row(), index.row());
	_data.removeAt(index.row());
	endRemoveRows();
}

void EventListModel::remove(EventType evtype) {
	for (int i = 0; i < _data.size(); i++) {
		while (i < _data.size() && _data[i].evtype == evtype) {
			beginRemoveRows(QModelIndex(), i, i);
			_data.removeAt(i);
			endRemoveRows();
		}
	}
}

void EventListModel::removeOthers(QModelIndex &index) {
	if (!index.isValid() || index.row() < 0 || index.row() >= _data.size()) {
		clear();
		return;
	}

	if (index.row() > 0) {
		beginRemoveRows(QModelIndex(), 0, index.row() - 1);
		_data.erase(_data.begin(), _data.begin() + index.row());
		endRemoveRows();
	}

	if (_data.size() > 1) {
		beginRemoveRows(QModelIndex(), 1, _data.size() - 1);
		_data.erase(_data.begin() + 1, _data.end());
		endRemoveRows();
	}
}

const EventListModel::EventListItem *EventListModel::operator[](int idx) const {
	return idx >= 0 && idx < _data.size() ? &_data[idx] : NULL;
}

const EventListModel::EventListItem *EventListModel::operator[](const QModelIndex &index) const {
	return index.isValid() && index.row() >= 0 && index.row() < _data.size() ? &_data[index.row()] : NULL;
}

static QString itemstr(const EventListModel::EventListItem &item) {
	QString tmp, ret = "%3<br>%4<br>%1 (%2)";
	int idx;

	static const char *markerStyles[] = {
		"<font size=\"+1\" color=\"darkgreen\"><b>%1</b></font>",
		"<font size=\"+1\" color=\"darkred\"><b>%1</b></font>",
		"<font color=\"darkgoldenrod\">%1</font>",
		"<font color=\"darkblue\">%1</font>",
		"<b>%1</b>"
	};

	static const char *markerNames[] = {
		//: Text shown on start markers in the main window marker list
		QT_TRANSLATE_NOOP("eventlist", "START"),
		//: Text shown on stop markers in the main window marker list
		QT_TRANSLATE_NOOP("eventlist", "STOP"),
		//: Text shown on chapter markers in the main window marker list
		QT_TRANSLATE_NOOP("eventlist", "CHAPTER"),
		//: Text shown on bookmark markers in the main window marker list
		QT_TRANSLATE_NOOP("eventlist", "BOOKMARK"),
		//: Text shown on markers of unknown type in the main window marker list
		QT_TRANSLATE_NOOP("eventlist", "Unknown")
	};

	switch (item.evtype) {
	case EventListModel::Start:
		idx = 0;
		break;

	case EventListModel::Stop:
		idx = 1;
		break;

	case EventListModel::Chapter:
		idx = 2;
		break;

	case EventListModel::Bookmark:
		idx = 3;
		break;

	default:
		idx = 4;
		break;
	}

	tmp = QString(markerStyles[idx]).arg(qApp->translate("eventlist", markerNames[idx]));
	ret = ret.arg(item.pic).arg("?IPB????"[item.pictype & 0x7]);
	ret = ret.arg(tmp, timestr(item.pts));
	return ret;
}

EventListDelegate::EventListDelegate(QObject *parent) : QItemDelegate(parent) {

}

void EventListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
	if (!index.isValid()) {
		return;
	}

	EventListModel::EventListItem *item = (EventListModel::EventListItem*)index.internalPointer();
	QLabel label(itemstr(*item));
	int x = option.rect.left() + 3;

	painter->save();
	label.setTextFormat(Qt::RichText);
	label.setFont(option.font);

	if (option.state & QStyle::State_Selected) {
		painter->fillRect(option.rect, option.palette.highlight());
	} else if (item->evtype == EventListModel::Stop) {
		QBrush brush(QColor(224, 224, 224));
		painter->fillRect(option.rect, brush);
	}

	if (!item->preview.isNull()) {
		painter->drawPixmap(x, option.rect.top() + (option.rect.height() - item->preview.height()) / 2, item->preview);
		x += item->preview.width() + 3;
	}

	label.setAttribute(Qt::WA_OpaquePaintEvent);
	// FIXME: QWidget::render(QPainter*,...) is broken, the following
	// line is supposed to work WITHOUT manual offset transformation
	label.render(painter, painter->deviceTransform().map(QPoint(x, option.rect.top() + (option.rect.height() - label.sizeHint().height()) / 2)));
	painter->restore();
}

QSize EventListDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
	if (!index.isValid()) {
		return QSize();
	}

	EventListModel::EventListItem *item = (EventListModel::EventListItem*)index.internalPointer();
	QLabel label(itemstr(*item));
	QSize tmp;
	int w = 3, h = 0;

	label.setTextFormat(Qt::RichText);
	label.setFont(option.font);

	if (!item->preview.isNull()) {
		h += item->preview.height();
		w += item->preview.width() + 3;

	}

	tmp = label.sizeHint();
	h = qMax(h + 6, QApplication::globalStrut().height());
	return QSize(w + tmp.width() + 3, h).expandedTo(tmp);
}

EventListModel::const_iterator EventListModel::constBegin(void) const {
	return _data.constBegin();
}

EventListModel::const_iterator EventListModel::constEnd(void) const {
	return _data.constEnd();
}

void EventListModel::clear(void) {
	beginRemoveRows(QModelIndex(), 0, _data.size() - 1);
	_data.clear();
	endRemoveRows();
}

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

#include <QAbstractListModel>
#include <QAbstractItemDelegate>
#include <QPixmap>
#include <QList>
#include <QItemDelegate>

#include "imageprovider.h"
#include "pts.h"

class EventListDelegate : public QItemDelegate {
public:
	EventListDelegate(QObject *parent = NULL);

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const;
};

class EventListModel : public QAbstractListModel {
public:
	enum EventType {
		None,
		Start,
		Stop,
		Chapter,
		Bookmark
	};

	struct EventListItem {
		QPixmap preview;
		enum EventType evtype;
		int pic, pictype;
		pts_t pts;
	};

	typedef QList<EventListItem>::const_iterator const_iterator;

private:
	QList<EventListItem> _data;
	EventListDelegate _delegate;

public:
	EventListModel(QObject *parent = NULL);
	~EventListModel(void);

	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
	Qt::ItemFlags flags(const QModelIndex &index) const;
	int rowCount(const QModelIndex &parent = QModelIndex()) const;
	QModelIndex index(int row, int col, const QModelIndex &parent = QModelIndex()) const;

	QAbstractItemDelegate *delegate(void);
	void addItem(imageprovider *imgp, EventType type, int pic, int pictype, pts_t pts);
	void convert(QModelIndex &index, EventType evtype);
	void remove(QModelIndex &index);
	void remove(EventType evtype);
	void removeOthers(QModelIndex &index);

	const EventListItem *operator[](int idx) const;
	const EventListItem *operator[](const QModelIndex &index) const;

	const_iterator constBegin() const;
	const_iterator constEnd() const;
	void clear(void);
};

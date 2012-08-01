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

#ifndef _DVBCUT_DVBCUT_QT4_H
#define _DVBCUT_DVBCUT_QT4_H

#include <string>
#include <list>

#include "ui_dvbcutbase.h"
#include "version.h"
#include "buffer.h"
#include "mpgfile.h"
#include "imageprovider.h"
#include "eventlistmodel.h"

#define VERSION_STRING	"dvbcut " VERSION "/" REVISION

class dvbcut : public QMainWindow, private Ui::dvbcutbase {
private:
	Q_OBJECT

public:
  struct quick_picture_lookup_s
  {
    int startpicture;
    pts_t startpts;
    int stoppicture;
    pts_t stoppts;
    int outpicture;
    pts_t outpts;

    quick_picture_lookup_s(int _startpicture, pts_t _startpts, int _stoppicture, pts_t _stoppts, int _outpicture, pts_t _outpts) :
      startpicture(_startpicture), startpts(_startpts), stoppicture(_stoppicture), stoppts(_stoppts), outpicture(_outpicture), outpts(_outpts)
    {
    }

    struct cmp_picture
    {
      bool operator()(int lhs, const quick_picture_lookup_s &rhs) const
      {
        return lhs<rhs.startpicture;
      }
    };
    struct cmp_outpicture
    {
      bool operator()(int lhs, const quick_picture_lookup_s &rhs) const
      {
        return lhs<rhs.outpicture;
      }
    };
  };
  typedef std::vector<quick_picture_lookup_s> quick_picture_lookup_t;

protected:
	quick_picture_lookup_t quick_picture_lookup;
	std::list<pts_t> chapterlist;
	EventListModel eventdata;

	QActionGroup *audioTrackGroup;
	inbuffer buf;
	mpgfile *mpg;
	int pictures;
	int curpic;
	double alpha;
	pts_t firstpts;
	int timeperframe;
	bool showimage;
	bool fine;
	bool jogsliding;
	int jogmiddlepic;
	std::string prjfilen, idxfilen, expfilen;
	QString picfilen;
	std::list<std::string> mpgfilen;
	imageprovider *imgp;
	int busy;
	double viewscalefactor;
	int currentaudiotrack;
	bool nogui;
	int exportformat;
	bool start_bof;
	bool stop_eof;

protected:
	void addtorecentfiles(const std::list<std::string> &filenames, const std::string &idxfilename=std::string());
	void setviewscalefactor(double factor);

	// special event handling (mouse wheel)
	bool eventFilter(QObject *watched, QEvent *e);

	void update_time_display();
	void update_quick_picture_lookup_table();

	// QMessagebox interface
	int question(const QString &caption, const QString &text);
	int critical(const QString &caption, const QString &text);

	// filename handling
	void make_canonical(std::string &filename);
	void make_canonical(std::list<std::string> &filenames);

	// generic event item adder
	void addEventListItem(int pic, EventListModel::EventType type);

public:
	static bool cache_friendly;

	dvbcut(QWidget *parent = NULL);
	~dvbcut(void);

	void open(std::list<std::string> filenames=std::list<std::string>(), std::string idxfilename=std::string(), std::string expfilename=std::string());
	void setbusy(bool b = true);
	void batchmode(bool b = true) { nogui = b; }
	void exportoptions(int format = 0, bool bof = true, bool eof = true) { exportformat = format; start_bof = bof; stop_eof = eof; }
	void addStartStopItems(std::vector<int>, int option=0);
	int getTimePerFrame() { return timeperframe>0 && timeperframe<5000 ? timeperframe : 3003; };

public slots:
	void on_fileNewAction_triggered(void);
	void on_fileOpenAction_triggered(void);
	void on_fileSaveAction_triggered(void);
	void on_fileSaveAsAction_triggered(void);
	void fileExport(void);
	void editAddMarker(QAction *);
	void on_editAutoChaptersAction_triggered();
	void on_editSuggestAction_triggered(void);
	void on_editImportAction_triggered(void);
	void on_editconvertpopup_triggered(QAction *action);
	void editConvert(int);
	void on_viewNormalAction_triggered();
	void on_viewUnscaledAction_triggered();
	void on_viewDifferenceAction_triggered();
	void viewScaleZoom(QAction *);
	void setViewScaleMode(QAction *);
	void on_jogslider_sliderReleased();
	void on_jogslider_valueChanged(int);
	void on_linslider_valueChanged(int);
	void on_eventlist_activated(const QModelIndex&);
	void on_eventlist_customContextMenuRequested(const QPoint&);
	void on_gobutton_clicked();
	void on_gobutton2_clicked();
	void updateimagedisplay();
	void on_audiotrackpopup_triggered(QAction *action);
	void on_recentfilespopup_triggered(QAction *action);
	void on_recentfilespopup_aboutToShow();
};

#endif

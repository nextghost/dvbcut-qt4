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

#include "dvbcut.h"
#include "settings.h"
#include "busyindicator.h"
#include "progressstatusbar.h"
#include "differenceimageprovider.h"
#include "exception.h"
#include "ui_mplayererrorbase.h"
#include "exportdialog.h"
#include "progresswindow.h"
#include "mpegmuxer.h"
#include "lavfmuxer.h"

#include <memory>
#include <cmath>
#include <cerrno>
#include <QFileDialog>
#include <QDir>
#include <QDomDocument>
#include <QTextStream>
#include <QMessageBox>
#include <QPixmap>
#include <QRegExpValidator>
#include <QWheelEvent>

bool dvbcut::cache_friendly = true;

// **************************************************************************
// ***  busy cursor helpers

class dvbcutbusy : public busyindicator {
protected:
	dvbcut *d;
	int bsy;
public:
	dvbcutbusy(dvbcut *_d) : busyindicator(), d(_d), bsy(0) {}

	~dvbcutbusy() {
		while (bsy>0)
			setbusy(false);
		while (bsy<0)
			setbusy(true);
	}

	virtual void setbusy(bool busy = true) {
		if (busy)
			++bsy;
		else {
			if (bsy<=0)
				return;

			--bsy;
		}

		d->setbusy(busy);
	}
};

helpDialog::helpDialog(QWidget *parent, QString file)
  : QDialog(parent)
  {
    vbox = new QVBoxLayout(this);
    resize(640, 480);
    viewer = new QTextBrowser(this);
    hbox = new QHBoxLayout();
    prev = new QPushButton(tr("Prev"), this);
    next = new QPushButton(tr("Next"), this);
    home = new QPushButton(tr("Home"), this);
    close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    vbox->addWidget(viewer);
    vbox->addLayout(hbox);
    hbox->addWidget(prev);
    hbox->addWidget(next);
    hbox->addWidget(home);
    hbox->addWidget(close);
    connect(prev, SIGNAL(clicked()), viewer, SLOT(backward()));
    connect(viewer, SIGNAL(backwardAvailable(bool)), prev, SLOT(setEnabled(bool)));
    connect(next, SIGNAL(clicked()), viewer, SLOT(forward()));
    connect(viewer, SIGNAL(forwardAvailable(bool)), next, SLOT(setEnabled(bool)));
    connect(home, SIGNAL(clicked()), viewer, SLOT(home()));
    connect(close, SIGNAL(clicked()), this, SLOT(accept()));
    viewer->setSource(file);
    setWindowTitle(tr("dvbcut help"));
    show();
  }

helpDialog::~helpDialog() {
  }

class mplayererrorbase : public QDialog, public Ui::mplayererrorbase {
public:
	mplayererrorbase(QWidget *parent = NULL, Qt::WindowFlags f = 0) :
		QDialog(parent, f) {

		setupUi(this);
	}
};

void dvbcut::setbusy(bool b) {
	if (b) {
		if (busy==0)
			QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

		++busy;
	} else if (busy>0) {
		--busy;

		if (busy==0)
			QApplication::restoreOverrideCursor();
	}
}

void dvbcut::addtorecentfiles(const std::list<std::string> &filenames, const std::string &idxfilename) {
  for(std::vector<std::pair<std::list<std::string>,std::string> >::iterator it=settings().recentfiles.begin();
      it!=settings().recentfiles.end();)
    // checking the size and the first/last filename should be enough... but maybe I'm to lazy! ;-)
    if (it->first.size()==filenames.size() && it->first.front()==filenames.front() && it->first.back()==filenames.back())
      it=settings().recentfiles.erase(it);
    else
      ++it;

  settings().recentfiles.insert(settings().recentfiles.begin(),std::pair<std::list<std::string>,std::string>(filenames,idxfilename));

  while (settings().recentfiles.size()>settings().recentfiles_max)
    settings().recentfiles.pop_back();
}

void dvbcut::setviewscalefactor(double factor) {
	if (factor <= 0.0) {
		factor = 1.0;
	}

	viewscalefactor = factor;

	if (imgp) {
		imgp->setviewscalefactor(factor);
		updateimagedisplay();
	}
}

bool dvbcut::eventFilter(QObject *watched, QEvent *e) {
  bool myEvent = true;
  int delta = 0;
  int incr = WHEEL_INCR_NORMAL;

	if (e->type() == QEvent::Wheel && watched == eventlist) {
		watched->event(e);
		return true;
	} else if (e->type() == QEvent::Wheel && watched != jogslider) {
    QWheelEvent *we = (QWheelEvent*)e;
    // Note: delta is a multiple of 120 (see Qt documentation)
    delta = we->delta();
      if (we->modifiers() & Qt::AltModifier)
      incr = WHEEL_INCR_ALT;
      else if (we->modifiers() & Qt::ControlModifier)
      incr = WHEEL_INCR_CTRL;
      else if (we->modifiers() & Qt::ShiftModifier)
      incr = WHEEL_INCR_SHIFT;
  }
  else if (e->type() == QEvent::KeyPress) {
    QKeyEvent *ke = (QKeyEvent*)e;
    delta = ke->count() * settings().wheel_delta;

	if (ke->key() == Qt::Key_Delete && watched == eventlist) {
		QModelIndex index = eventlist->selectionModel()->currentIndex();

		if (index.isValid()) {
			eventdata.remove(index);
		}

		return true;
	} else if (ke->key() == Qt::Key_Right)
      delta = -delta;
    else if (ke->key() != Qt::Key_Left)
      myEvent = false;
    if (ke->modifiers() & Qt::AltModifier)
      incr = WHEEL_INCR_ALT;
    else if (ke->modifiers() & Qt::ControlModifier)
      incr = WHEEL_INCR_CTRL;
    else if (ke->modifiers() & Qt::ShiftModifier)
      incr = WHEEL_INCR_SHIFT;
  }
      else
    myEvent = false;

  if (myEvent) {
	// Playback active, just eat the event
	if (mplayer_process) {
		return true;
	}

    // process scroll event myself
    incr = settings().wheel_increments[incr];
      if (incr != 0) {
        bool save = fine;
	// use fine positioning if incr is small
        fine = (incr < 0 ? -incr : incr) < settings().wheel_threshold;
	// Note: delta is a multiple of 120 (see Qt documentation)
        int newpos = curpic - (delta * incr) / settings().wheel_delta;
        if (newpos < 0)
	  newpos = 0;
	else if (newpos >= pictures)
	  newpos = pictures - 1;
        linslider->setValue(newpos);
        fine = save;
      }
      return true;
    }

  // propagate to base class
  return QMainWindow::eventFilter(watched, e);
}

void dvbcut::update_time_display() {
  const index::picture &idx=(*mpg)[curpic];
  const pts_t pts=idx.getpts()-firstpts;
  const char *AR[]={"forbidden","1:1","4:3","16:9","2.21:1","reserved"};
  const char *FR[]={"forbidden","23.976","24","25","29.97","30","50","59.94","60","reserved"};  
 
  int outpic=0;
  pts_t outpts=0;
  QChar mark = ' ';
  
  // find the entry in the quick_picture_lookup table that corresponds to curpic
  quick_picture_lookup_t::iterator it=
    std::upper_bound(quick_picture_lookup.begin(),quick_picture_lookup.end(),curpic,quick_picture_lookup_s::cmp_picture());
   
  if (it!=quick_picture_lookup.begin())
   {
     // curpic is not before the first entry of the table
     --it;
     if (curpic < it->stoppicture)
     {
       // curpic is between (START and STOP[ pics of the current entry
       outpic=curpic-it->stoppicture+it->outpicture;
       outpts=pts-it->stoppts+it->outpts;
       mark = '*';
     }
     else
     {
       // curpic is after the STOP-1 pic of the current entry
       outpic=it->outpicture;
       outpts=it->outpts;
     }
   }
       
  QString curtime =
    QString(QChar(IDX_PICTYPE[idx.getpicturetype()]))
    + " " + timestr(pts);
  QString outtime =
    QString(mark) + " " + timestr(outpts);
  pictimelabel->setText(curtime);
  pictimelabel2->setText(outtime);
  goinput->setText(QString::number(curpic));
  goinput2->setText(QString::number(outpic));
  
  int res=idx.getresolution();	// are found video resolutions stored in index?
  if (res) {	
    // new index with resolution bits set and lookup table at the end			  
    picinfolabel->setText(QString::number(mpg->getwidth(res)) + "x" 
                        + QString::number(mpg->getheight(res)));
  } else {
    // in case of an old index file type (or if we don't want to change the index format/encoding?)
    // ==> get info directly from each image (which could be somewhat slower?!?)
    QImage p = imageprovider(*mpg, new dvbcutbusy(this), true).getimage(curpic,false);   
    picinfolabel->setText(QString::number(p.width()) + "x" 
                        + QString::number(p.height()));
  }
  picinfolabel2->setText(QString(FR[idx.getframerate()]) + "fps, "
                      + QString(AR[idx.getaspectratio()]));
}

void dvbcut::update_quick_picture_lookup_table() {
  // that's the (only) place where the event list should be scanned for  
  // the exported pictures ranges, i.e. for START/STOP/CHAPTER markers!
  quick_picture_lookup.clear(); 
  chapterlist.clear();
  
  chapterlist.push_back(0);
    
  int startpic, stoppic, outpics=0, lastchapter=-2;
  pts_t startpts, stoppts, outpts=0;
  bool realzero=false;
  
  if(!nogui) {
    // overwrite CLI options
    start_bof = settings().start_bof;
    stop_eof = settings().stop_eof;
  }
  
  if (start_bof) {
    startpic=0;
    startpts=(*mpg)[0].getpts()-firstpts; 
  }
  else {
    startpic=-1;
    startpts=0; 
  }

	EventListModel::const_iterator item;
	for (item = eventdata.constBegin(); item != eventdata.constEnd(); ++item) {
    switch (item->evtype) {
      case EventListModel::Start:
	if (startpic<0 || (start_bof && startpic==0 && !realzero)) {
          startpic = item->pic;
          startpts = item->pts;
          if (startpic==0)
	    realzero=true;        
          // did we have a chapter in the eventlist directly before?
          if(lastchapter==startpic)
            chapterlist.push_back(outpts);
        }
        break;
      case EventListModel::Stop:
        if (startpic>=0) {
          stoppic = item->pic;
          stoppts = item->pts;
          outpics+=stoppic-startpic;
          outpts+=stoppts-startpts;
          
          quick_picture_lookup.push_back(quick_picture_lookup_s(startpic,startpts,stoppic,stoppts,outpics,outpts));

          startpic=-1;
        }
        break;
      case EventListModel::Chapter:
        lastchapter = item->pic;
	if (startpic>=0)
	  chapterlist.push_back(item->pts - startpts + outpts);
	break;
      default:
        break;
      }
    }

  // last item in list was a (real or virtual) START
  if (stop_eof && startpic>=0) {
    // create a new export range by adding a virtual STOP marker at EOF 
    stoppic=pictures-1;
    stoppts=(*mpg)[stoppic].getpts()-firstpts;
    outpics+=stoppic-startpic;
    outpts+=stoppts-startpts;
    
    quick_picture_lookup.push_back(quick_picture_lookup_s(startpic,startpts,stoppic,stoppts,outpics,outpts)); 
  }
  
  update_time_display();
}

int dvbcut::question(const QString & caption, const QString & text) {
	if (nogui) {
		fprintf(stderr, "%s", (const char*)tr("%1\n%2\n(assuming No)\n").arg(caption, text).toLocal8Bit());
		return QMessageBox::No;
	}

	return QMessageBox::question(this, caption, text, QMessageBox::Yes, QMessageBox::No | QMessageBox::Default | QMessageBox::Escape);
}

int dvbcut::critical(const QString & caption, const QString & text) {
	if (nogui) {
		fprintf(stderr, "%s", (const char*)tr("%1\n%2\n(aborting)\n").arg(caption, text).toLocal8Bit());
		return QMessageBox::Abort;
	}

	return QMessageBox::critical(this, caption, text, QMessageBox::Abort, QMessageBox::NoButton);
}

void dvbcut::make_canonical(std::string &filename) {
	QFileInfo finfo(QString::fromStdString(filename));

	if (finfo.exists()) {
		filename = finfo.canonicalFilePath().toStdString();
		return;
	}

	// File does not exist, find canonical dir path and append filename
	QDir dir = finfo.path();

	// If parent directory doesn't exist either, do nothing
	if (dir.exists()) {
		dir = QDir(dir.canonicalPath());
		filename = dir.filePath(finfo.fileName()).toStdString();
	}
}

void dvbcut::make_canonical(std::list<std::string> &filenames) {
	std::list<std::string>::iterator it;

	for (it = filenames.begin(); it != filenames.end(); ++it) {
		make_canonical(*it);
	}
}

void dvbcut::addEventListItem(int pic, EventListModel::EventType type) {
	if (imgp && imgp->rtti() == IMAGEPROVIDER_STANDARD) {
		eventdata.addItem(imgp, type, pic, (*mpg)[pic].getpicturetype(), (*mpg)[pic].getpts() - firstpts);
	} else {
		imageprovider stdimgp(*mpg, new dvbcutbusy(this), false, 4);
		eventdata.addItem(&stdimgp, type, pic, (*mpg)[pic].getpicturetype(), (*mpg)[pic].getpts() - firstpts);
	}
}

void dvbcut::snapshotSave(std::vector<int> piclist, int range, int samples) {
  QString prefix;
  QString type=settings().snapshot_type;
  QString delim=settings().snapshot_delimiter;
  QString ext=settings().snapshot_extension;
  int first = settings().snapshot_first;
  int width = settings().snapshot_width;
  int quality = settings().snapshot_quality;

  // get unique filename
  if (picfilen.isEmpty()) {
    if(settings().snapshot_prefix.isEmpty()) {
      if (!prjfilen.empty())
        prefix = QString::fromStdString(prjfilen);
      else if (!mpgfilen.empty() && !mpgfilen.front().empty())
        prefix = QString::fromStdString(mpgfilen.front());
    } else
      prefix = settings().snapshot_prefix;

    if (!prefix.isEmpty()) {
      int lastdot = prefix.lastIndexOf('.');
      int lastslash = prefix.lastIndexOf('/');
      if (lastdot >= 0 && lastdot > lastslash)
        prefix = prefix.left(lastdot);
      int nr = first;
      picfilen = prefix + delim + QString::number(nr).rightJustified( width, '0' ) + "." + ext;
      while (QFileInfo(picfilen).exists())
        picfilen = prefix + delim + QString::number(++nr).rightJustified( width, '0' )+ "." + ext;
    }
  }  

  QString s = QFileDialog::getSaveFileName(
    this,
    tr("Choose the name of the picture file"),
    picfilen,
    tr("Images (*.%1)").arg(ext));

  if (s.isEmpty())
    return;

  if (QFileInfo(s).exists() && question(
    tr("File exists - dvbcut"),
    tr("%1\nalready exists. Overwrite?").arg(s)) !=
    QMessageBox::Yes)
    return;

  QImage p;
  int pic, i, nr;
  bool ok=false;
  for (std::vector<int>::iterator it = piclist.begin(); it != piclist.end(); ++it) {

    if(samples>1 && range>0)
      pic = chooseBestPicture(*it, range, samples);
    else
      pic = *it+range;

    // save selected picture to file
    if (imgp)
      p = imgp->getimage(pic,fine);
    else
      p = imageprovider(*mpg, new dvbcutbusy(this), false, viewscalefactor).getimage(pic,fine);
    if(p.save(s,type.toAscii(),quality))
      statusBar()->showMessage("Saved snapshot: " + s);
    else
      statusBar()->showMessage("*** Unable to save snapshot: " + s + "! ***");
 
    // try to "increment" the choosen filename for next snapshot (or use old name as default)
    // No usage of "delim", so it's possible to choose any prefix in front of the number field!
    i = s.lastIndexOf(QRegExp("\\d{"+QString::number(width)+","+QString::number(width)+"}\\."+ext+"$"));
    if (i>0) {
      nr = s.mid(i,width).toInt(&ok,10);
      if (ok)
        picfilen = s.left(i) + QString::number(++nr).rightJustified(width, '0')+ "." + ext;
      else
        picfilen = s;
    }
    else
      picfilen = s;

    s = picfilen;
  }
}

int dvbcut::chooseBestPicture(int startpic, int range, int samples) {
  QImage p;
  QRgb col;
  int idx, x, y, w, h, pic, norm, colors;
  int r, g, b, nr=11, ng=16, nb=5;  // "borrowed" the weights from calc. of qGray = (11*r+16*g+5*b)/32
  double entropy;
  std::vector<double> histogram;

  samples = samples>0 ? samples: 1;
  samples = samples>abs(range) ? abs(range)+1: samples;

  int bestpic = startpic+range, bestnr=0;
  double bestval = 0.;
  int dp = range/(samples-1);
  int ncol = nr*ng*nb;

  // choose the best picture among equidistant samples in the range (not for single snapshots!)
  for (int n=0; n<samples && samples>1 && range>0; n++) {
      pic = startpic+n*dp;

      if (imgp)
        p = imgp->getimage(pic,fine);
      else
        p = imageprovider(*mpg, new dvbcutbusy(this), false, viewscalefactor).getimage(pic,fine);

      // get a measure for complexity of picture (at least good enough to discard single colored frames!)

      // index color space and fill histogram
      w = p.width();
      h = p.height();
      histogram.assign(ncol,0.);
      for(x=0; x<w; x++)
        for(y=0; y<h; y++) {
          col=p.pixel(x,y);
          r=nr*qRed(col)/256;
          g=ng*qGreen(col)/256;
          b=nb*qBlue(col)/256;
          idx=r+nr*g+nr*ng*b;
          histogram[idx]++;
        }

      // calc. probability to fall in a given color class
      colors = 0;
      norm = w*h;
      for(unsigned int i=0; i<histogram.size(); i++)
        if(histogram[i]>0) {
          colors++;
          histogram[i] /= norm;
        }

      // calc. the information entropy (complexity) of the picture
      entropy = 0.;
      for(x=0; x<w; x++)
        for(y=0; y<h; y++) {
          col=p.pixel(x,y);
          r=nr*qRed(col)/256;
          g=ng*qGreen(col)/256;
          b=nb*qBlue(col)/256;
          idx=r+nr*g+nr*ng*b;
          entropy-=histogram[idx]*log(histogram[idx]);
        }
      entropy /= log(2.);

      //fprintf(stderr,"frame %7d, sample %4d (%7d): %10d %10.2f\n",startpic,n,pic,colors,entropy);

      // largest "information content"?
      if(entropy>bestval) {
        bestval=entropy;
        bestpic=pic;
        bestnr=n;
      }
  }
  //fprintf(stderr,"choosing sample / frame: %4d / %7d\n!", bestnr, bestpic);

  return bestpic;
}

void dvbcut::on_helpAboutAction_triggered(void) {
  QMessageBox::about(this, tr("dvbcut"), 
    tr("<head></head><body><span style=\"font-family: Helvetica,Arial,sans-serif;\">"
      "<p>dvbcut Version %1</p>"
      "eMail: <a href=\"mailto:dvbcut-user@lists.sourceforge.net?subject=Comment%20about%20dvbcut\">"
      "dvbcut-user@lists.sourceforge.net</a></p>"
      "<p>This program is free software; you can redistribute it and/or "
      "modify it under the terms of the GNU General Public License as "
      "published by the Free Software Foundation; either version 2 of "
      "the License, or (at your option) any later version. This "
      "program is distributed in the hope that it will be useful, "
      "but WITHOUT ANY WARRANTY; without even the implied warranty of "
      "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU "
      "General Public License for more details.</p>"
      "<p>You should have received a copy of the GNU General Public License along "
      "with this program; if not, see "
      "<a href=\"http://www.gnu.org/licenses/\">http://www.gnu.org/licenses/</a>.</p>"
      "</span></body></html>").arg(VERSION_STRING));
}

void dvbcut::on_helpContentAction_triggered(void) {
  QDir appDir(qApp->applicationDirPath());
  // first search in the directory containing dvbcut
  QString helpFile = appDir.absoluteFilePath("dvbcut_en.html");
#ifndef __WIN32__
  // Unix/Linux: search in the associated share subdirectory
  if (!QFile::exists(helpFile)) {
	appDir.cdUp();
    helpFile = appDir.absoluteFilePath("share/help/dvbcut_en.html");
  }
#endif
  if (QFile::exists(helpFile)) {
    helpDialog dlg(this, helpFile);
    dlg.exec();
  }
  else {
    QMessageBox::information(this, tr("dvbcut"),
      tr("Help file %1 not available").arg(helpFile));
  }
}

dvbcut::dvbcut(QWidget *parent) : QMainWindow(parent, Qt::Window),
	audioTrackGroup(new QActionGroup(this)),
	buf(8 << 20, 128 << 20), mpg(NULL), pictures(0), curpic(~0),
	showimage(true), fine(false), jogsliding(false), jogmiddlepic(0),
	mplayer_process(NULL), imgp(NULL), busy(0), viewscalefactor(1.0),
	nogui(false) {

	QActionGroup *group;
	QAction *tmpAction;
	QList<QAction *>::iterator it;
	QList<QAction *> list;
	// FIXME: use system decimal point separator in regexp
	QRegExpValidator *validator = new QRegExpValidator(QRegExp("^\\d+|((\\d+:)?[0-5]?\\d:)?[0-5]?\\d(\\.\\d{1,3}(/\\d{1,2})?)?$"), this);

	setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose);

	goinput->setValidator(validator);
	goinput2->setValidator(validator);

	// Add marker action group
	group = new QActionGroup(this);
	editStartAction->setData(EventListModel::Start);
	editStopAction->setData(EventListModel::Stop);
	editChapterAction->setData(EventListModel::Chapter);
	editBookmarkAction->setData(EventListModel::Bookmark);
	group->addAction(editStartAction);
	group->addAction(editStopAction);
	group->addAction(editChapterAction);
	group->addAction(editBookmarkAction);
	connect(group, SIGNAL(triggered(QAction*)), this, SLOT(editAddMarker(QAction*)));

	// Edit convert actions
	editConvertStart->setData(0);
	editConvertStop->setData(1);
	editConvert4_3->setData(2);
	editConvert16_9->setData(3);

	// View mode action group
	group = new QActionGroup(this);
	group->addAction(viewNormalAction);
	group->addAction(viewUnscaledAction);
	group->addAction(viewDifferenceAction);

	// View scale action group
	group = new QActionGroup(this);
	viewFullSizeAction->setData(1);
	viewHalfSizeAction->setData(2);
	viewQuarterSizeAction->setData(4);
	viewCustomSizeAction->setData(0);
	group->addAction(viewFullSizeAction);
	group->addAction(viewHalfSizeAction);
	group->addAction(viewQuarterSizeAction);
	group->addAction(viewCustomSizeAction);
	connect(group, SIGNAL(triggered(QAction*)), this, SLOT(setViewScaleMode(QAction*)));

	// Restore view scale settings
	tmpAction = viewCustomSizeAction;
	list = group->actions();

	for (it = list.begin(); it != list.end(); ++it) {
		if ((*it)->data().toInt() == settings().viewscalefactor) {
			tmpAction = *it;
			break;
		}
	}

	tmpAction->trigger();

	// Zoom action group
	group = new QActionGroup(this);
	zoomInAction->setData(1/1.2);
	zoomOutAction->setData(1.2);
	group->addAction(zoomInAction);
	group->addAction(zoomOutAction);
	connect(group, SIGNAL(triggered(QAction*)), this, SLOT(viewScaleZoom(QAction*)));


#ifndef HAVE_LIB_AO
	playAudio1Action->setEnabled(false);
	playAudio2Action->setEnabled(false);
	playToolbar->removeAction(playAudio1Action);
	playToolbar->removeAction(playAudio2Action);
	playMenu->removeAction(playAudio1Action);
	playMenu->removeAction(playAudio2Action);
#endif // ! HAVE_LIB_AO

	// install event handler
	linslider->installEventFilter(this);
	eventlist->installEventFilter(this);
	installEventFilter(this);

	// set caption
	setWindowTitle(QString(VERSION_STRING));

	eventlist->setModel(&eventdata);
	eventlist->setItemDelegate(eventdata.delegate());
	eventlist->setSelectionMode(QAbstractItemView::SingleSelection);
}

dvbcut::~dvbcut(void) {
	if (mplayer_process) {
		mplayer_process->terminate();
		mplayer_process->waitForFinished();
		delete mplayer_process;
	}

	delete imgp;
	delete mpg;
}

void dvbcut::open(std::list<std::string> filenames, std::string idxfilename, std::string expfilename) {
  if (filenames.empty()) {
    QStringList fn = QFileDialog::getOpenFileNames(
      this,
      tr("Choose one or more MPEG files to open"),
      settings().lastdir,
      settings().loadfilter);
    if (fn.empty()) {
//      fprintf(stderr,"open(): QFileDialog::getOpenFileNames() returned EMPTY filelist!!!\n");    
//      fprintf(stderr,"        If you didn't saw a FileDialog, please check your 'lastdir' settings variable...");    
      return;
    }  
    for (QStringList::Iterator it = fn.begin(); it != fn.end(); ++it)
      filenames.push_back(it->toStdString());

	if (settings().lastdir_update) {
		QFileInfo info(fn.front());
		settings().lastdir = info.canonicalPath();
	}
  }

  make_canonical(filenames);

  std::string filename = filenames.front();

  // a valid file name has been entered
  setWindowTitle(QString::fromStdString(VERSION_STRING " - " + filename));

  // reset inbuffer
  buf.reset();

  // if an MPEG file has already been opened, close it and reset the UI
  if (mpg) {
    delete mpg;
    mpg=0;
  }

  curpic=~0;
  if (imgp) {
    delete imgp;
    imgp=0;
  }

  eventdata.clear();
  imagedisplay->setAttribute(Qt::WA_OpaquePaintEvent, false);
  imagedisplay->setMinimumSize(QSize(0,0));
  imagedisplay->setPixmap(QPixmap());
  pictimelabel->clear();
  pictimelabel2->clear();
  picinfolabel->clear();
  picinfolabel2->clear();
  linslider->setValue(0);
  jogslider->setValue(0);

	viewNormalAction->setChecked(true);

  fileOpenAction->setEnabled(false);
  fileSaveAction->setEnabled(false);
  fileSaveAsAction->setEnabled(false);
  snapshotSaveAction->setEnabled(false);
  chapterSnapshotsSaveAction->setEnabled(false);
  // enable closing even if no file was loaded (mr)
  //fileCloseAction->setEnabled(false);
  fileExportAction->setEnabled(false);
  playPlayAction->setEnabled(false);
  playStopAction->setEnabled(false);
	audiotrackpopup->setEnabled(false);
  audiotrackpopup->clear();
  editStartAction->setEnabled(false);
  editStopAction->setEnabled(false);
  editChapterAction->setEnabled(false);
  editBookmarkAction->setEnabled(false);
  editAutoChaptersAction->setEnabled(false);
  editSuggestAction->setEnabled(false);
  editImportAction->setEnabled(false);
  //editConvertAction->setEnabled(false);

#ifdef HAVE_LIB_AO

  playAudio1Action->setEnabled(false);
  playAudio2Action->setEnabled(false);
#endif // HAVE_LIB_AO

  viewNormalAction->setEnabled(false);
  viewUnscaledAction->setEnabled(false);
  viewDifferenceAction->setEnabled(false);

  eventlist->setEnabled(false);
  imagedisplay->setEnabled(false);
  pictimelabel->setEnabled(false);
  pictimelabel2->setEnabled(false);
  picinfolabel->setEnabled(false);
  picinfolabel2->setEnabled(false);
  goinput->setEnabled(false);
  gobutton->setEnabled(false);
  goinput2->setEnabled(false);
  gobutton2->setEnabled(false);
  linslider->setEnabled(false);
  jogslider->setEnabled(false);

  std::string prjfilename;
  QDomDocument domdoc;
  {
    QFile fr(QString::fromStdString(filename));
    if (fr.open(QIODevice::ReadOnly)) {
	QTextStream infile(&fr);
      QString line;
      while (line.length()==0) {
        if ((line = infile.readLine(512)).isNull())
          break;
        line=line.trimmed();
      }
      if (line.startsWith(QString("<!DOCTYPE"))
       || line.startsWith(QString("<?xml"))) {
        fr.seek(0);
        QString errormsg;
        if (domdoc.setContent(&fr,false,&errormsg)) {
          QDomElement docelem = domdoc.documentElement();
          if (docelem.tagName() != "dvbcut") {
            critical(tr("Failed to read project file - dvbcut"),
	      tr("%1:\nNot a valid dvbcut project file").arg(QString::fromStdString(filename)));
            fileOpenAction->setEnabled(true);
            return;
          }
	  // parse elements, new-style first
	  QDomNode n;
          filenames.clear();
	  if (!nogui) {    
	    // in batch mode CLI-switches have priority!              
	    idxfilename.clear();
	    expfilename.clear();
	  }    
	  for (n = domdoc.documentElement().firstChild(); !n.isNull(); n = n.nextSibling()) {
	    QDomElement e = n.toElement();
	    if (e.isNull())
	      continue;
	    if (e.tagName() == "mpgfile") {
	      QString qs = e.attribute("path");
	      if (!qs.isEmpty())
		filenames.push_back(qs.toStdString());
	    }
	    else if (e.tagName() == "idxfile" && idxfilename.empty()) {
	      QString qs = e.attribute("path");
	      if (!qs.isEmpty())
		idxfilename = qs.toStdString();
	    }
	    else if (e.tagName() == "expfile" && expfilename.empty()) {           
	      QString qs = e.attribute("path");
	      if (!qs.isEmpty())
		expfilename = qs.toStdString();
	      qs = e.attribute("format");
              bool okay=false;
	      if (!qs.isEmpty()) {
                int val = qs.toInt(&okay,0);
		if(okay) exportformat = val;
              }  
	    }
	  }
	  // try old-style project file format
	  if (filenames.empty()) {
	    QString qs = docelem.attribute("mpgfile");
	    if (!qs.isEmpty())
	      filenames.push_back(qs.toStdString());
	  }
	  if (idxfilename.empty()) {
	    QString qs = docelem.attribute("idxfile");
	    if (!qs.isEmpty())
	      idxfilename = qs.toStdString();
	  }
	  if (expfilename.empty()) {
	    QString qs = docelem.attribute("expfile");
	    if (!qs.isEmpty())
	      expfilename = qs.toStdString();
	  }
	  // sanity check
	  if (filenames.empty()) {
	    critical(tr("Failed to read project file - dvbcut"),
	      tr("%1:\nNo MPEG filename given in project file").arg(QString::fromStdString(filename)));
	    fileOpenAction->setEnabled(true);
	    return;
	  }
          prjfilename = filename;
        }
        else {
          critical(tr("Failed to read project file - dvbcut"),
	    QString::fromStdString(filename) + ":\n" + errormsg);
          fileOpenAction->setEnabled(true);
          return;
        }
      }
    }
  }

  // make filename the first MPEG file
  filename = filenames.front();

  dvbcutbusy busy(this);
  busy.setbusy(true);

  mpg = 0;
  std::string errormessage;
  std::list<std::string>::const_iterator it = filenames.begin();
  while (it != filenames.end() && buf.open(*it, &errormessage))
    ++it;
  buf.setsequential(cache_friendly);
  if (it == filenames.end()) {
    mpg = mpgfile::open(buf, &errormessage);
  }
  busy.setbusy(false);

  if (!mpg) {
    critical(tr("Failed to open file - dvbcut"), QString::fromStdString(errormessage));
    fileOpenAction->setEnabled(true);
    return;
  }

  if (idxfilename.empty()) {
    idxfilename = filename + ".idx";
    if (!nogui) {
	  /*
	   * BEWARE! UGLY HACK BELOW!
	   *
	   * In order to circumvent the QFileDialog's stupid URL parsing,
	   * we need to first change to the directory and then pass the
	   * filename as a relative file: URL. Otherwise, filenames that
	   * contain ":" will not be handled correctly. --mr
	   */
	QFileInfo finfo(QString::fromStdString(idxfilename));
	QString curpath = QDir::currentPath();
	QDir::setCurrent(finfo.path());

	  QString s=QFileDialog::getSaveFileName(
		  this,
		  tr("Choose the name of the index file"),
		  "file:" + finfo.fileName(),
		  settings().idxfilter,
		  NULL,
		  QFileDialog::DontConfirmOverwrite);
	QDir::setCurrent(curpath);
	  if (s.isNull()) {
		delete mpg;
		mpg=0;
		fileOpenAction->setEnabled(true);
		return;
	  }
	  idxfilename=s.toStdString();
	  // it's now a relative name that will be canonicalized again soon
	}
  }

  make_canonical(idxfilename);

  pictures=-1;

  if (!idxfilename.empty()) {
    std::string errorstring;
    busy.setbusy(true);
    pictures=mpg->loadindex(idxfilename.c_str(),&errorstring);
    int serrno=errno;
    busy.setbusy(false);
    if (nogui && pictures > 0)
      fprintf(stderr,"Loaded index with %d pictures!\n",pictures);
    if (pictures==-1 && serrno!=ENOENT) {
      delete mpg;
      mpg=0;
      critical(tr("Failed to open file - dvbcut"),QString::fromStdString(errorstring));
      fileOpenAction->setEnabled(true);
      return;
    }
    if (pictures==-2) {
      delete mpg;
      mpg=0;
      critical(tr("Invalid index file - dvbcut"), QString::fromStdString(errorstring));
      fileOpenAction->setEnabled(true);
      return;
    }
    if (pictures<=-3) {
      delete mpg;
      mpg=0;
      critical(tr("Index file mismatch - dvbcut"), QString::fromStdString(errorstring));
      fileOpenAction->setEnabled(true);
      return;
    }
  }

  if (pictures<0) {
    progressstatusbar psb(statusBar());
    psb.setprogress(500);
    psb.print("Indexing '%s'...",filename.c_str());
    std::string errorstring;
    busy.setbusy(true);
    pictures=mpg->generateindex(idxfilename.empty()?0:idxfilename.c_str(),&errorstring,&psb);
    busy.setbusy(false);
    if (nogui && pictures > 0)
      fprintf(stderr,"Generated index with %d pictures!\n",pictures);

    if (psb.cancelled()) {
      delete mpg;
      mpg=0;
      fileOpenAction->setEnabled(true);
      return;
    }

    if (pictures<0) {
      delete mpg;
      mpg=0;
      critical(tr("Error creating index - dvbcut"),
	       tr("Cannot create index for\n%1:\n%2").arg(QString::fromStdString(filename), QString::fromStdString(errorstring)));
      fileOpenAction->setEnabled(true);
      return;
    } else if (!errorstring.empty()) {
      critical(tr("Error saving index file - dvbcut"), QString::fromStdString(errorstring));
    }
  }

  if (pictures<1) {
    delete mpg;
    mpg=0;
    critical(tr("Invalid MPEG file - dvbcut"),
	     tr("The chosen file\n%1\ndoes not contain any video").arg(QString::fromStdString(filename)));
    fileOpenAction->setEnabled(true);
    return;
  }

  // file loaded, switch to random mode
  buf.setsequential(false);

  mpgfilen=filenames;
  idxfilen=idxfilename;
  prjfilen=prjfilename;
  expfilen=expfilename;
  picfilen=QString::null;
  if (prjfilen.empty())
    addtorecentfiles(mpgfilen,idxfilen);
  else {
    std::list<std::string> dummy_list;
    dummy_list.push_back(prjfilen);
    addtorecentfiles(dummy_list);
  }

  firstpts=(*mpg)[0].getpts();
  timeperframe=(*mpg)[1].getpts()-(*mpg)[0].getpts();

  double fps=27.e6/double(mpgfile::frameratescr[(*mpg)[0].getframerate()]);
  linslider->setMaximum(pictures-1);
  linslider->setSingleStep(int(300*fps));
  linslider->setPageStep(int(900*fps));
  if (settings().lin_interval > 0)
    linslider->setTickInterval(int(settings().lin_interval*fps));

  //alpha=log(jog_maximum)/double(100000-jog_offset);
  // with alternative function
  alpha=log(settings().jog_maximum)/100000.;
  if (settings().jog_interval > 0 && settings().jog_interval <= 100000) 
    jogslider->setTickInterval(int(100000/settings().jog_interval));

  imagedisplay->setAttribute(Qt::WA_OpaquePaintEvent, true);
  curpic=~0;
  showimage=true;
  fine=false;
  jogsliding=false;
  jogmiddlepic=0;
  imgp=new imageprovider(*mpg,new dvbcutbusy(this),false,viewscalefactor);

  on_linslider_valueChanged(0);
  linslider->setValue(0);
  jogslider->setValue(0);

	{
		EventListModel tmpmodel;
		tmpmodel.addItem(imgp, EventListModel::Start, 0, 2, 0);
		eventlist->setMinimumWidth(tmpmodel.delegate()->sizeHint(QStyleOptionViewItem(), tmpmodel.index(0, 0, QModelIndex())).width() + 24);
	}

  if (!domdoc.isNull()) {
    QDomElement e;
    for (QDomNode n=domdoc.documentElement().firstChild();!n.isNull();n=n.nextSibling())
      if (!(e=n.toElement()).isNull()) {
      EventListModel::EventType evt;
      if (e.tagName()=="start")
        evt=EventListModel::Start;
      else if (e.tagName()=="stop")
        evt=EventListModel::Stop;
      else if (e.tagName()=="chapter")
        evt=EventListModel::Chapter;
      else if (e.tagName()=="bookmark")
        evt=EventListModel::Bookmark;
      else
        continue;
      bool okay=false;
      int picnum;
      QString str=e.attribute("picture","-1");
      if (str.contains(':') || str.contains('.')) {
        okay=true;
        picnum=string2pts(str.toStdString())/getTimePerFrame();
      }
      else
        picnum=str.toInt(&okay,0);
      if (okay && picnum>=0 && picnum<pictures) {
		addEventListItem(picnum, evt);
		qApp->processEvents();
      }
    }
  }

  update_quick_picture_lookup_table();

  fileOpenAction->setEnabled(true);
  fileSaveAction->setEnabled(true);
  fileSaveAsAction->setEnabled(true);
  snapshotSaveAction->setEnabled(true);
  chapterSnapshotsSaveAction->setEnabled(true);
  fileCloseAction->setEnabled(true);
  fileExportAction->setEnabled(true);
  playPlayAction->setEnabled(true);
  audiotrackpopup->setEnabled(true);
  playStopAction->setEnabled(false);
  editStartAction->setEnabled(true);
  editStopAction->setEnabled(true);
  editChapterAction->setEnabled(true);
  editBookmarkAction->setEnabled(true);
  editAutoChaptersAction->setEnabled(true);
  editSuggestAction->setEnabled(true);
  editImportAction->setEnabled(true);
  //editConvertAction->setEnabled(true);
  viewNormalAction->setEnabled(true);
  viewUnscaledAction->setEnabled(true);
  viewDifferenceAction->setEnabled(true);

#ifdef HAVE_LIB_AO

  if (mpg->getaudiostreams()) {
    playAudio1Action->setEnabled(true);
    playAudio2Action->setEnabled(true);
  }
#endif // HAVE_LIB_AO

  eventlist->setEnabled(true);
  imagedisplay->setEnabled(true);
  pictimelabel->setEnabled(true);
  pictimelabel2->setEnabled(true);
  picinfolabel->setEnabled(true);
  picinfolabel2->setEnabled(true);
  goinput->setEnabled(true);
  gobutton->setEnabled(true);
  goinput2->setEnabled(true);
  gobutton2->setEnabled(true);
  linslider->setEnabled(true);
  jogslider->setEnabled(true);

  currentaudiotrack = -1;

  for(int a=0;a<mpg->getaudiostreams();++a) {
  	QAction *trackAction = audiotrackpopup->addAction(QString::fromStdString(mpg->getstreaminfo(audiostream(a))));

	audioTrackGroup->addAction(trackAction);
	trackAction->setData(a);
	trackAction->setCheckable(true);

	if (!a) {
		trackAction->setChecked(true);
		currentaudiotrack = a;
	}
  }
}

void dvbcut::addStartStopItems(std::vector<int> cutlist, int option) {
  // take list of frame numbers and set alternating START/STOP markers
  bool alternate=true;
  EventListModel::EventType type=EventListModel::Start;
  if(option==1)
    type=EventListModel::Stop;
  else if(option==2 || option==3)
    alternate=false;   
 
  // make sure list is sorted... 
  sort(cutlist.begin(),cutlist.end());

  // ...AND there are no old START/STOP pairs!!!
	eventdata.remove(EventListModel::Start);
	eventdata.remove(EventListModel::Stop);
  
  for (std::vector<int>::iterator it = cutlist.begin(); it != cutlist.end(); ++it) {   
    if(!alternate) {
      // set START/STOP according aspect ratio (2=4:3, 3=16:9)
      if (option == (*mpg)[*it].getaspectratio()) 
        type=EventListModel::Start;
      else 
        type=EventListModel::Stop;  
    } 

    addEventListItem(*it, type);

    if(alternate) {
      // set START/STOP alternatingly
      if(type==EventListModel::Start) 
        type=EventListModel::Stop;
      else
        type=EventListModel::Start;  
    }
  }
  
  update_quick_picture_lookup_table();
}

void dvbcut::on_fileNewAction_triggered(void) {
	dvbcut *d = new dvbcut;
	d->show();
}

void dvbcut::on_fileOpenAction_triggered(void) {
	open();
}

void dvbcut::on_fileSaveAction_triggered(void) {
  if (prjfilen.empty()) {
    on_fileSaveAsAction_triggered();
    return;
  }

  QFile outfile(QString::fromStdString(prjfilen));
  if (!outfile.open(QIODevice::WriteOnly)) {
    critical(tr("Failed to write project file - dvbcut"),
      tr("%1:\nCould not open file").arg(QString::fromStdString(prjfilen)));
    return;
  }

  QDomDocument doc("dvbcut");
  QDomElement root = doc.createElement("dvbcut");
#if 0
  root.setAttribute("mpgfile",mpgfilen.front());
  if (!idxfilen.empty())
    root.setAttribute("idxfile",idxfilen);
#endif
  doc.appendChild(root);

  std::list<std::string>::const_iterator it = mpgfilen.begin();
  while (it != mpgfilen.end()) {
    QDomElement elem = doc.createElement("mpgfile");
    elem.setAttribute("path", QString::fromStdString(*it));
    root.appendChild(elem);
    ++it;
  }

  if (!idxfilen.empty()) {
    QDomElement elem = doc.createElement("idxfile");
    elem.setAttribute("path", QString::fromStdString(idxfilen));
    root.appendChild(elem);
  }

	EventListModel::const_iterator item;
  for (item = eventdata.constBegin(); item != eventdata.constEnd(); ++item) {
      QString elemname;
      EventListModel::EventType evt = item->evtype;

      if (evt==EventListModel::Start)
	elemname="start";
      else if (evt==EventListModel::Stop)
	elemname="stop";
      else if (evt==EventListModel::Chapter)
	elemname="chapter";
      else if (evt==EventListModel::Bookmark)
	elemname="bookmark";
      else
	continue;

      QDomElement elem=doc.createElement(elemname);
      elem.setAttribute("picture", item->pic);
      root.appendChild(elem);
    }

  QTextStream stream(&outfile);
  stream.setCodec("UTF-8");
  stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  stream << doc.toString();
  outfile.close();
}

void dvbcut::on_fileSaveAsAction_triggered(void) {
  if (prjfilen.empty() && !mpgfilen.empty() && !mpgfilen.front().empty()) {
    std::string prefix = mpgfilen.front();
    int lastdot = prefix.rfind(".");
    int lastslash = prefix.rfind("/");
    if (lastdot >= 0 && lastdot > lastslash)
      prefix = prefix.substr(0, lastdot);
    prjfilen = prefix + ".dvbcut";
    int nr = 0;
    while (QFileInfo(QString::fromStdString(prjfilen)).exists())
      prjfilen = prefix + "_" + (QString::number(++nr).toStdString()) + ".dvbcut";
  }

  QString s=QFileDialog::getSaveFileName(
    this,
    tr("Choose the name of the project file"),
    QString::fromStdString(prjfilen),
    settings().prjfilter);

  if (s.isNull())
    return;

  if (QFileInfo(s).exists() && question(
      tr("File exists - dvbcut"),
      tr("%1\nalready exists. Overwrite?").arg(s)) !=
      QMessageBox::Yes)
    return;

  prjfilen=s.toStdString();
  if (!prjfilen.empty())
    on_fileSaveAction_triggered();
}

void dvbcut::on_snapshotSaveAction_triggered(void) {
  std::vector<int> piclist;
  piclist.push_back(curpic);

  snapshotSave(piclist);
}

void dvbcut::on_chapterSnapshotsSaveAction_triggered(void) {
  std::vector<int> piclist;
	EventListModel::const_iterator item;

	for (item = eventdata.constBegin(); item != eventdata.constEnd(); ++item) {
		if (item->evtype == EventListModel::Chapter) {
			piclist.push_back(item->pic);
		}
	}

  if (piclist.size()) {
    snapshotSave(piclist, settings().snapshot_range, settings().snapshot_samples);
  } else
    statusBar()->showMessage(QString("*** No chapters to save! ***"));
}

void dvbcut::on_fileExportAction_triggered(void) {
  if (expfilen.empty()) {
    std::string newexpfilen;

    if (!prjfilen.empty())
      newexpfilen=prjfilen;
    else if (!mpgfilen.empty() && !mpgfilen.front().empty())
      newexpfilen=mpgfilen.front();

    if (!newexpfilen.empty()) {
      int lastdot(newexpfilen.rfind("."));
      int lastslash(newexpfilen.rfind("/"));
      if (lastdot>=0 && lastslash<lastdot)
        newexpfilen=newexpfilen.substr(0,lastdot);
      expfilen=newexpfilen+".mpg";
      int nr=0;
      while (QFileInfo(QString::fromStdString(expfilen)).exists())
        expfilen=newexpfilen+"_"+(QString::number(++nr).toStdString())+".mpg";
    }
  }

  std::auto_ptr<exportdialog> expd(new exportdialog(QString::fromStdString(expfilen),this));
  expd->muxercombo->addItem(tr("MPEG program stream/DVD (DVBCUT multiplexer)"));
  expd->muxercombo->addItem(tr("MPEG program stream (DVBCUT multiplexer)"));
  expd->muxercombo->addItem(tr("MPEG program stream/DVD (libavformat)"));
  expd->muxercombo->addItem(tr("MPEG transport stream (libavformat)"));
#ifndef __WIN32__
  // add possible user configured pipe commands 
  int pipe_items_start=expd->muxercombo->count();
  for (unsigned int i = 0; i < settings().pipe_command.size(); ++i)
    expd->muxercombo->addItem(settings().pipe_label[i]);
#endif

  if (settings().export_format < 0
      || settings().export_format >= expd->muxercombo->count())
    settings().export_format = 0;
  expd->muxercombo->setCurrentIndex(settings().export_format);

	expd->audiolist->setModel(mpg->audiomodel());
	expd->audiolist->selectAll();

  int expfmt = 0;
  if (!nogui) {
    expd->show();
    if (!expd->exec())
      return;

    settings().export_format = expd->muxercombo->currentIndex();
    expfmt = expd->muxercombo->currentIndex();

    expfilen=expd->filenameline->text().toStdString();
    if (expfilen.empty())
      return;
    expd->hide();
  } else if (exportformat > 0 && exportformat < expd->muxercombo->count()) 
    expfmt = exportformat;  

  // create usable chapter lists 
  std::string chapterstring, chaptercolumn;
  if (!chapterlist.empty()) {
    int nchar=0;
    char chapter[16];
    pts_t lastch=-1;
    for(std::list<pts_t>::const_iterator it=chapterlist.begin();
        it!=chapterlist.end();++it)
      if (*it != lastch) {
        lastch=*it;
        // formatting the chapter string
        if (nchar>0) {
          nchar++; 
          chapterstring+=",";
          chaptercolumn+="\n";
        }  
        nchar+=sprintf(chapter,"%02d:%02d:%02d.%03d",
                       int(lastch/(3600*90000)),
                       int(lastch/(60*90000))%60,
                       int(lastch/90000)%60,
                       int(lastch/90)%1000	);
        // append chapter marks to lists for plain text / dvdauthor xml-file         
        chapterstring+=chapter;
        chaptercolumn+=chapter;
      }
  }

  int child_pid = -1;
  int pipe_fds[2];

#ifndef __WIN32__
  // check for piped output
  std::string expcmd;
  size_t pos;
  int ip=expfmt-pipe_items_start; 
  if(ip>=0) {
    expfmt=settings().pipe_format[ip];
    if (settings().pipe_command[ip].indexOf('|')==-1) 
      expcmd = "|"+settings().pipe_command[ip].toStdString();
    else 
      expcmd = settings().pipe_command[ip].toStdString();
       
    if ((pos=expcmd.find("%OUTPUT%"))!=std::string::npos)
      expcmd.replace(pos,8,expfilen);  
  } else 
    expcmd = expfilen;

  // chapter tag can also be used with input field pipes!
  if ((pos=expcmd.find("%CHAPTERS%"))!=std::string::npos)
    expcmd.replace(pos,10,chapterstring);  

  if ((pos=expcmd.find('|'))!=std::string::npos) {
    pos++;   
    size_t end=expcmd.find(' ',pos);  
    //if (!QFileInfo(expcmd.substr(pos,end-pos)).exists() ||
    //    !QFileInfo(expcmd.substr(pos,end-pos)).isExecutable()) {
    // better test if command is found in $PATH, so one don't needs to give the full path name
    std::string which="which "+expcmd.substr(pos,end-pos)+" >/dev/null";
    int irc = system(which.c_str());
    if(irc!=0) { 
      critical(tr("Command not found - dvbcut"), tr("Problems with piped output to:\n%1").arg(QString::fromStdString(expcmd.substr(pos,end-pos))));
      return; 
    }       

    if (::pipe(pipe_fds) < 0)
      return;
    child_pid = fork();
    if (child_pid == 0) {
      ::close(pipe_fds[1]);
      if (pipe_fds[0] != STDIN_FILENO) {
	dup2(pipe_fds[0], STDIN_FILENO);
      }
      //fprintf(stderr, "Executing %s\n", expcmd.c_str()+pos);
      for (int fd=0; fd<256; ++fd)
	if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO)
	  ::close(fd);
      execl("/bin/sh", "sh", "-c", expcmd.c_str()+pos, (char *)0);
      _exit(127);
    }
    ::close(pipe_fds[0]);
    if (child_pid < 0) {
      ::close(pipe_fds[1]);
      return;
    }
  } else
#endif
  if (QFileInfo(QString::fromStdString(expfilen)).exists() && question(
      tr("File exists - dvbcut"),
      tr("%1\nalready exists. Overwrite?").arg(QString::fromStdString(expfilen))) !=
      QMessageBox::Yes)
    return;

  progresswindow *prgwin = 0;
  logoutput *log;
  if (nogui) {
    log = new logoutput;
  }
  else {
    prgwin = new progresswindow(this);
    prgwin->setWindowTitle(QString::fromStdString("export - " + expfilen));
    log = prgwin;
  }

  //   lavfmuxer mux(fmt,*mpg,outfilename);

  std::auto_ptr<muxer> mux;
  uint32_t audiostreammask(0);
	QModelIndexList streamsel = expd->audiolist->selectionModel()->selectedRows();

	for (QModelIndexList::iterator it = streamsel.begin(); it != streamsel.end(); ++it) {
		audiostreammask |= 1u << it->row();
	}

  std::string out_file = (child_pid < 0) ? expfilen :
    std::string("pipe:") + QString::number(pipe_fds[1]).toStdString();

  switch(expfmt) {
    case 1:
      mux=std::auto_ptr<muxer>(new mpegmuxer(audiostreammask,*mpg,out_file.c_str(),false,0));
      break;
    case 2:
      mux=std::auto_ptr<muxer>(new lavfmuxer("dvd",audiostreammask,*mpg,out_file.c_str()));
      break;
    case 3:
      mux=std::auto_ptr<muxer>(new lavfmuxer("mpegts",audiostreammask,*mpg,out_file.c_str()));
      break;
    case 0:
    default:
      mux=std::auto_ptr<muxer>(new mpegmuxer(audiostreammask,*mpg,out_file.c_str()));
      break;
  }

  if (!mux->ready()) {
#ifndef __WIN32__
    if (child_pid > 0) {
      ::close(pipe_fds[1]);
      int wstatus;
      while (waitpid(child_pid, &wstatus, 0)==-1 && errno==EINTR);
    }
#endif
    log->printerror(tr("Unable to set up muxer!"));
    if (nogui)
      delete log;
    else {
      prgwin->finish();
      delete prgwin;
    }
    return;
  }

  // starting export, switch source to sequential mode
  buf.setsequential(cache_friendly);

  int startpic, stoppic, savedpic=0;
  pts_t startpts=(*mpg)[0].getpts(), stoppts, savedtime=0;

  for(unsigned int num=0; num<quick_picture_lookup.size(); num++) {
      startpic=quick_picture_lookup[num].startpicture;
      startpts=quick_picture_lookup[num].startpts;
      stoppic=quick_picture_lookup[num].stoppicture;
      stoppts=quick_picture_lookup[num].stoppts;
      
      log->printheading(tr("%1. Exporting %2 pictures: %3 .. %4")
			.arg(num+1).arg(stoppic-startpic).arg(ptsstring(startpts).c_str(),ptsstring(stoppts).c_str()));
      mpg->savempg(*mux,startpic,stoppic,savedpic,quick_picture_lookup.back().outpicture,log);

      savedpic=quick_picture_lookup[num].outpicture;
      savedtime=quick_picture_lookup[num].outpts;
  }

  mux.reset();

  log->printheading(tr("Saved %1 pictures (%2:%3:%4.%5)").arg(savedpic)
		    .arg(int(savedtime/(3600*90000)), 2, 10, QChar('0'))
		    .arg(int(savedtime/(60*90000))%60, 2, 10, QChar('0'))
		    .arg(int(savedtime/90000)%60, 2, 10, QChar('0'))
		    .arg(int(savedtime/90)%1000, 2, 10, QChar('0')));

#ifndef __WIN32__
  if (child_pid > 0) {
    ::close(pipe_fds[1]);
    int wstatus;
    while (waitpid(child_pid, &wstatus, 0)==-1 && errno==EINTR);
  }

  // do some post processing if requested
  if (ip>=0 && !settings().pipe_post[ip].isEmpty()) {
    expcmd = settings().pipe_post[ip].toStdString();
       
    if ((pos=expcmd.find("%OUTPUT%"))!=std::string::npos)
      expcmd.replace(pos,8,expfilen);  
    if ((pos=expcmd.find("%CHAPTERS%"))!=std::string::npos)
      expcmd.replace(pos,10,chapterstring);  

    pos=expcmd.find(' ');  
    std::string which="which "+expcmd.substr(0,pos)+" >/dev/null";

    log->print("");
    log->printheading(tr("Performing post processing"));
    int irc = system(which.c_str());

    if(irc!=0) { 
      critical(tr("Command not found - dvbcut"), tr("Problems with post processing command:\n%1").arg(QString::fromStdString(expcmd.substr(0,pos))));
      log->print(tr("Command not found!"));
    } else {      
      int irc = system(expcmd.c_str());
      if(irc!=0) { 
        critical(tr("Post processing error - dvbcut"),
		tr("Post processing command:\n%2\n returned non-zero exit code: %1").arg(irc).arg(QString::fromStdString(expcmd)));
        log->print(tr("Command reported some problems... please check!"));
      } 
      //else      
      //  log->print("Everything seems to be OK...");
    }
  }   
#endif

  // print plain list of chapter marks
  log->print("");
  log->printheading(tr("Chapter list"));
  log->print(chaptercolumn.c_str());

  // simple dvdauthor xml file with chapter marks
  std::string filename,destname;
  if (expfilen.rfind("/")<expfilen.length()) 
    filename=expfilen.substr(expfilen.rfind("/")+1);
  else 
    filename=expfilen;
  destname=filename.substr(0,filename.rfind("."));
  log->print("");
  log->printheading(tr("Simple XML-file for dvdauthor with chapter marks"));
  log->print("<dvdauthor dest=\"%s\">",destname.c_str());
  log->print("  <vmgm />");
  log->print("  <titleset>");
  log->print("    <titles>");
  log->print("      <pgc>");
  log->print("        <vob file=\"%s\" chapters=\"%s\" />",filename.c_str(),chapterstring.c_str());
  log->print("      </pgc>");
  log->print("    </titles>");
  log->print("  </titleset>");
  log->print("</dvdauthor>");

  if (nogui)
    delete log;
  else {
    prgwin->finish();
    delete prgwin;
  }

  // done exporting, switch back to random mode
  buf.setsequential(false);
}

void dvbcut::editAddMarker(QAction *action) {
	EventListModel::EventType type = (EventListModel::EventType)action->data().toInt();

	addEventListItem(curpic, type);

	if (type != EventListModel::Bookmark) {
		update_quick_picture_lookup_table();
	}
}

void dvbcut::on_editAutoChaptersAction_triggered(void) {
  int inpic, chapters = 0;
  quick_picture_lookup_t::iterator it;
  QImage p1, p2;

  // the first chapter at 0sec is ALWAYS set by default from update_quick_picture_lookup_table()
  int chapter_start; 
  if(settings().chapter_interval>0) {
    chapter_start = settings().chapter_interval;  // fixed length of intervals
  } else {
    chapter_start = quick_picture_lookup.back().outpicture/(1-settings().chapter_interval);  // given number of chapters
    chapter_start = chapter_start > settings().chapter_minimum ? chapter_start : settings().chapter_minimum;
  }  
  // don't make a new chapter if it would be shorter than the specified minimal length
  int chapter_max = quick_picture_lookup.back().outpicture - settings().chapter_minimum;

  for(int outpic = chapter_start; outpic < chapter_max; outpic+=chapter_start) 
    if (!quick_picture_lookup.empty()) {
      // find the entry in the quick_picture_lookup table that corresponds to given output picture
      it = std::upper_bound(quick_picture_lookup.begin(),quick_picture_lookup.end(),outpic,quick_picture_lookup_s::cmp_outpicture());
      inpic = outpic - it->outpicture + it->stoppicture;   

      if(inpic+settings().chapter_tolerance>it->stoppicture) {
        if(it == quick_picture_lookup.end()) break;
        // take begin of next START/STOP range as chapter picture if to near at end of current range
        it++;
        inpic=it->startpicture;
      } else if(settings().chapter_tolerance>0) {  
        // look for the next scene change inside specified frame tolerance (VERY SLOW!!!)
        if (!imgp) 
          imgp = new imageprovider(*mpg, new dvbcutbusy(this), false, viewscalefactor, settings().chapter_tolerance);
        p2 = imgp->getimage(inpic,fine);  
        for(int pic=inpic+1; pic<inpic+settings().chapter_tolerance && pic<pictures; pic++) {
          // get next picture
          p1 = p2;
          p2 = imgp->getimage(pic,fine);
          if (p2.size()!=p1.size())
            p2=p2.scaled(p1.size());

          // calculate color distance between two consecutive frames
          double dist=0.;
          if (p2.depth()==32 && p1.depth()==32)
            for (int y=0;y<p1.height();++y) {
              QRgb *col1=(QRgb*)p1.scanLine(y);
              QRgb *col2=(QRgb*)p2.scanLine(y);

              for (int x=p1.width();x>0;--x) {
                dist+=sqrt(pow(qRed(*col1)-qRed(*col2),2)+pow(qGreen(*col1)-qGreen(*col2),2)+pow(qBlue(*col1)-qBlue(*col2),2));
                // that's a bit faster...   
                //dist+=(abs(qRed(*col1)-qRed(*col2))+abs(qGreen(*col1)-qGreen(*col2))+abs(qBlue(*col1)-qBlue(*col2)));
                ++col1;
                ++col2;
              }
            }
          dist/=(p1.height()*p1.width());
 
          // 50. seems to be a good measure for the color distance at scene changes (about sqrt(3)*50. if sum of abs values)! 
          //fprintf(stderr,"%d, DIST=%f\n",pic,dist); 
          if(dist>settings().chapter_threshold) { 
            inpic=pic;
            statusBar()->showMessage(QString().sprintf("%d. Scene change @ %d, DIST=%f\n",chapters+1,inpic,dist));   
            break;
          }  
        }
      }
      
      addEventListItem(inpic, EventListModel::Chapter);
      chapters++;
    }  

  if (chapters)
    update_quick_picture_lookup_table();
}

void dvbcut::on_editSuggestAction_triggered(void) {
  int pic = 0, found=0;
  while ((pic = mpg->nextaspectdiscontinuity(pic)) >= 0) {
    addEventListItem(pic, EventListModel::Bookmark);
    found++;
  }
  if (!found)
    statusBar()->showMessage(QString("*** No aspect ratio changes detected! ***"));
}

void dvbcut::on_editImportAction_triggered(void) {
  int found=0;
  std::vector<int> bookmarks = mpg->getbookmarks();
  for (std::vector<int>::iterator b = bookmarks.begin(); b != bookmarks.end(); ++b) {   
    addEventListItem(*b, EventListModel::Bookmark);
    found++;
  }
  if (!found)  
    statusBar()->showMessage(QString("*** No valid bookmarks available/found! ***"));
}

void dvbcut::on_editconvertpopup_triggered(QAction *action) {
	editConvert(action->data().toInt());
}

void dvbcut::editConvert(int option) {
  // convert Bookmarks to START/STOP markers
  if(option<0 || option>3) return;
  
  int found=0;
  std::vector<int> cutlist;
	EventListModel::const_iterator item;

	for (item = eventdata.constBegin(); item != eventdata.constEnd(); ++item) {
		if (item->evtype == EventListModel::Bookmark) {
			cutlist.push_back(item->pic);
			found++;
		}
	}

	eventdata.remove(EventListModel::Bookmark);

  if (found) {
    addStartStopItems(cutlist, option);

    if (found%2) 
      statusBar()->showMessage(QString("*** No matching stop marker!!! ***"));
  }  
  else
    statusBar()->showMessage(QString("*** No bookmarks to convert! ***"));  
}

void dvbcut::on_viewNormalAction_triggered(void) {
	viewNormalAction->setChecked(true);

	if (!imgp || imgp->rtti() != IMAGEPROVIDER_STANDARD) {
		delete imgp;
		imgp = new imageprovider(*mpg, new dvbcutbusy(this), false, viewscalefactor);
		updateimagedisplay();
	}
}

void dvbcut::on_viewUnscaledAction_triggered(void) {
	viewUnscaledAction->setChecked(true);

	if (!imgp || imgp->rtti() != IMAGEPROVIDER_UNSCALED) {
		delete imgp;
		imgp = new imageprovider(*mpg, new dvbcutbusy(this), true, viewscalefactor);
		updateimagedisplay();
	}
}

void dvbcut::on_viewDifferenceAction_triggered(void) {
	viewDifferenceAction->setChecked(true);

	delete imgp;
	imgp = new differenceimageprovider(*mpg, curpic, new dvbcutbusy(this), false, viewscalefactor);
	updateimagedisplay();
}

void dvbcut::viewScaleZoom(QAction *action) {
	settings().viewscalefactor_custom = viewscalefactor * action->data().toDouble();
	viewCustomSizeAction->trigger();
}

void dvbcut::setViewScaleMode(QAction *action) {
	int scale = action->data().toInt();

	settings().viewscalefactor = scale;
	setviewscalefactor(scale > 0 ? scale : settings().viewscalefactor_custom);
}

void dvbcut::on_playAudio2Action_triggered(void) {
#ifdef HAVE_LIB_AO
  qApp->processEvents();
  try
  {
    mpg->playaudio(currentaudiotrack,curpic,2000);
  }
  catch (const dvbcut_exception &ex)
  {
    ex.show();
  }
#endif // HAVE_LIB_AO
}

void dvbcut::on_playAudio1Action_triggered(void) {
#ifdef HAVE_LIB_AO
  qApp->processEvents();
  try
  {
    mpg->playaudio(currentaudiotrack,curpic,-2000);
  }
  catch (const dvbcut_exception &ex)
  {
    ex.show();
  }
#endif // HAVE_LIB_AO
}

void dvbcut::on_playStopAction_triggered(void) {
  if (mplayer_process)
    mplayer_process->terminate();
}

void dvbcut::on_playPlayAction_triggered(void) {
  if (mplayer_process)
    return;

	QStringList args;

  eventlist->setEnabled(false);
  linslider->setEnabled(false);
  jogslider->setEnabled(false);
  gobutton->setEnabled(false);
  goinput->setEnabled(false);
  gobutton2->setEnabled(false);
  goinput2->setEnabled(false);

#ifdef HAVE_LIB_AO

  playAudio1Action->setEnabled(false);
  playAudio2Action->setEnabled(false);
#endif // HAVE_LIB_AO

  playPlayAction->setEnabled(false);
  playStopAction->setEnabled(true);
	audiotrackpopup->setEnabled(false);
	recentfilespopup->setEnabled(false);

  fileOpenAction->setEnabled(false);
  fileSaveAction->setEnabled(false);
  fileSaveAsAction->setEnabled(false);
  snapshotSaveAction->setEnabled(false);
  chapterSnapshotsSaveAction->setEnabled(false);
  fileExportAction->setEnabled(false);

  showimage=false;
  imagedisplay->setPixmap(QPixmap());

  fine=true;
  linslider->setValue(mpg->lastiframe(curpic));
  dvbcut_off_t offset=(*mpg)[curpic].getpos().packetposition();
  mplayer_curpts=(*mpg)[curpic].getpts();

  dvbcut_off_t partoffset;
  int partindex = buf.getfilenum(offset, partoffset);
  if (partindex == -1)
    return;	// what else can we do?

	args.push_back("-noconsolecontrols");
#ifdef __WIN32__
	args.push_back("-vo");
	args.push_back("directx:noaccel");
#endif
	args.push_back("-wid");
	args.push_back(QString().sprintf("0x%x", int(imagedisplay->winId())));
	args.push_back("-sb");
	args.push_back(QString::number(offset - partoffset));
	args.push_back("-geometry");
	args.push_back(QString().sprintf("%dx%d+0+0", int(imagedisplay->width()), int(imagedisplay->height())));

	if (currentaudiotrack>=0 && currentaudiotrack<mpg->getaudiostreams()) {
		args.push_back("-aid");
		args.push_back(QString().sprintf("0x%x", int(mpg->mplayeraudioid(currentaudiotrack))));
	}

	// for now, pass all filenames from the current one up to the last one
	std::list<std::string>::const_iterator it = mpgfilen.begin();
	for (int i = 0; it != mpgfilen.end(); ++i, ++it) {
		if (i >= partindex) {
			args.push_back(QString::fromStdString(*it));
		}
	}

	mplayer_process = new QProcess(this);
	mplayer_process->setProcessChannelMode(QProcess::MergedChannels);
	mplayer_process->setReadChannel(QProcess::StandardOutput);

  connect(mplayer_process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(mplayer_exited(int, QProcess::ExitStatus)));
  connect(mplayer_process, SIGNAL(readyReadStandardOutput()), this, SLOT(mplayer_readstdout()));

  mplayer_success=false;
  mplayer_process->start("mplayer", args);

  if (!mplayer_process->waitForStarted()) {
    delete mplayer_process;
    mplayer_process=0;
    mplayer_exited(1, QProcess::CrashExit);
    return;
  }
}

void dvbcut::on_jogslider_sliderReleased(void) {
	jogsliding = false;
	jogmiddlepic = curpic;
	jogslider->setValue(0);
}

void dvbcut::on_jogslider_valueChanged(int v) {
  if (!mpg || (v==0 && curpic==jogmiddlepic))
    return;
  jogsliding=true;

  int relpic=0;

  /*
  if (v>jog_offset)
  relpic=int(exp(alpha*(v-jog_offset))+.5);
  else if (v<-jog_offset)
  relpic=-int(exp(alpha*(-v-jog_offset))+.5);
  */
  /*
  alternative function 
  (fits better to external tick interval setting, because jog_offset 
  only affects scale at small numbers AND range of 1 frame is NOT smaller 
  than range of 0 and 2 frames as in old function!)
  */ 
  if (v>0) {
    relpic=int(exp(alpha*v)-settings().jog_offset);
    if (relpic<0) relpic=0;
  }  
  else if (v<0) {
    relpic=-int(exp(-alpha*v)-settings().jog_offset);
    if (relpic>0) relpic=0;
  }  

  int newpic=jogmiddlepic+relpic;
  if (newpic<0)
    newpic=0;
  else if (newpic>=pictures)
    newpic=pictures-1;

  if (relpic>=settings().jog_threshold) {
    newpic=mpg->nextiframe(newpic);
    fine=false;
  } else if (relpic<=-settings().jog_threshold) {
    fine=false;
  } else
    fine=true;

    if (curpic!=newpic)
      linslider->setValue(newpic);

    fine=false;
}

void dvbcut::on_linslider_valueChanged(int newpic) {
	if (!mpg || newpic == curpic)
		return;

	if (!fine)
		newpic = mpg->lastiframe(newpic);

	if (!jogsliding)
		jogmiddlepic=newpic;

	if (newpic == curpic)
		return;

	curpic = newpic;

	if (!jogsliding)
		jogmiddlepic=newpic;
	
	update_time_display();
	updateimagedisplay();
}

void dvbcut::on_eventlist_activated(const QModelIndex &index) {
	const EventListModel::EventListItem *item = eventdata[index];

	if (!item) {
		return;
	}

	fine = true;
	linslider->setValue(item->pic);
	fine = false;
}

void dvbcut::on_eventlist_customContextMenuRequested(const QPoint &pos) {
	QModelIndex index = eventlist->indexAt(pos);
	const EventListModel::EventListItem *item = eventdata[index];

	QAction *action;
	QMenu popup(eventlist), *submenu;
	EventListModel::EventType evtype;

	if (item) {
		popup.addAction("Go to")->setData(1);
		popup.addAction("Delete")->setData(2);
		submenu = popup.addMenu("Mass delete");
		submenu->addAction("Delete others")->setData(3);
	} else {
		submenu = &popup;
	}

	submenu->addAction("Delete all")->setData(4);
	submenu->addAction("Delete all start/stops")->setData(5);
	submenu->addAction("Delete all chapters")->setData(6);
	submenu->addAction("Delete all bookmarks")->setData(7);

	if (item) {
		submenu = popup.addMenu("Convert");

		if (item->evtype != EventListModel::Start) {
			submenu->addAction("Convert to start marker")->setData(8);
		}

		if (item->evtype != EventListModel::Stop) {
			submenu->addAction("Convert to stop marker")->setData(9);
		}

		if (item->evtype != EventListModel::Chapter) {
			submenu->addAction("Convert to chapter marker")->setData(10);
		}

		if (item->evtype != EventListModel::Bookmark) {
			submenu->addAction("Convert to bookmark")->setData(11);
		}

		popup.addAction("Display difference from this picture")->setData(12);
	}

	// Shift by 2 pixels to avoid selecting the first action by accident
	action = popup.exec(eventlist->mapToGlobal(pos) + QPoint(2,2));

	if (!action) {
		return;
	}

	switch (action->data().toInt()) {
	case 1:
		fine = true;
		linslider->setValue(item->pic);
		fine = false;
		break;

	case 2:
		evtype = item->evtype;
		eventdata.remove(index);

		if (evtype != EventListModel::Bookmark) {
			update_quick_picture_lookup_table();
		}
		break;

	case 3:
		eventdata.removeOthers(index);
		update_quick_picture_lookup_table();
		break;

	case 4:
		eventdata.clear();
		update_quick_picture_lookup_table();
		break;

	case 5:
		eventdata.remove(EventListModel::Start);
		eventdata.remove(EventListModel::Stop);
		update_quick_picture_lookup_table();
		break;

	case 6:
		eventdata.remove(EventListModel::Chapter);
		update_quick_picture_lookup_table();
		break;

	case 7:
		eventdata.remove(EventListModel::Bookmark);
		update_quick_picture_lookup_table();
		break;

	case 8:
		eventdata.convert(index, EventListModel::Start);
		update_quick_picture_lookup_table();
		break;

	case 9:
		eventdata.convert(index, EventListModel::Stop);
		update_quick_picture_lookup_table();
		break;

	case 10:
		eventdata.convert(index, EventListModel::Chapter);
		update_quick_picture_lookup_table();
		break;

	case 11:
		eventdata.convert(index, EventListModel::Bookmark);
		update_quick_picture_lookup_table();
		break;

	case 12:
		delete imgp;
		imgp = new differenceimageprovider(*mpg, item->pic, new dvbcutbusy(this), false, viewscalefactor);
		updateimagedisplay();
		viewDifferenceAction->setChecked(true);
		break;
	}
}

void dvbcut::mplayer_exited(int exitcode, QProcess::ExitStatus status) {
  if (mplayer_process) {
    if (!mplayer_success)// && !mplayer_process->normalExit())
    {
      mplayererrorbase *meb = new mplayererrorbase(this, Qt::Dialog);
      meb->setAttribute(Qt::WA_DeleteOnClose);
      meb->textbrowser->setText(mplayer_out);
      meb->show();
    }


    delete mplayer_process;
    mplayer_process=0;
  }

  eventlist->setEnabled(true);
  linslider->setEnabled(true);
  jogslider->setEnabled(true);
  gobutton->setEnabled(true);
  goinput->setEnabled(true);
  gobutton2->setEnabled(true);
  goinput2->setEnabled(true);

#ifdef HAVE_LIB_AO

  playAudio1Action->setEnabled(true);
  playAudio2Action->setEnabled(true);
#endif // HAVE_LIB_AO

  playPlayAction->setEnabled(true);
  playStopAction->setEnabled(false);
	audiotrackpopup->setEnabled(true);
	recentfilespopup->setEnabled(true);

  fileOpenAction->setEnabled(true);
  fileSaveAction->setEnabled(true);
  fileSaveAsAction->setEnabled(true);
  snapshotSaveAction->setEnabled(true);
  chapterSnapshotsSaveAction->setEnabled(true);
  fileExportAction->setEnabled(true);

  int cp=curpic;
  jogmiddlepic=curpic;
  curpic=-1;
  showimage=true;
  fine=true;
  on_linslider_valueChanged(cp);
  linslider->setValue(cp);
  fine=false;
}

void dvbcut::mplayer_readstdout(void) {
  if (!mplayer_process)
    return;

  QString line(mplayer_process->readAllStandardOutput());
  if (!line.length())
    return;

  if (!mplayer_success)
    mplayer_out += line;

  int pos=line.indexOf("V:");
  if (pos<0)
    return;

  line.remove(0,pos+2);
  line=line.trimmed();
  for(pos=0;pos<(signed)line.length();++pos)
    if ((line[pos]<'0' || line[pos]>'9')&&line[pos]!='.')
      break;
  line.truncate(pos);
  if (line.length()==0)
    return;

  bool okay;
  double d=line.toDouble(&okay);
  if (d==0 || !okay)
    return;

  if (!mplayer_success) {
    mplayer_success=true;
    mplayer_out.truncate(0);
  }
  pts_t pts=mplayer_ptsreference(pts_t(d*90000.),mplayer_curpts);


  if (pts==mplayer_curpts)
    return;

  int cp=curpic;

  if (pts>mplayer_curpts) {
    while (cp+1<pictures && pts>mplayer_curpts)
      mplayer_curpts=(*mpg)[++cp].getpts();
  } else if (pts<mplayer_curpts-18000) {
    while (cp>1 && mplayer_curpts>pts)
      mplayer_curpts=(*mpg)[--cp].getpts();
  }

  linslider->setValue(cp);
}

void dvbcut::on_gobutton_clicked(void) {
	QString text = goinput->text();
	int inpic;

	// FIXME: check for system decimal point separator instead
	if (text.contains(':') || text.contains('.')) {
		inpic = string2pts(text.toStdString()) / getTimePerFrame();
	} else {
		inpic = text.toInt();
	}

	fine = true;
	linslider->setValue(inpic);
	fine = false;
}

void dvbcut::on_gobutton2_clicked(void) {
	QString text = goinput2->text();
	int inpic;

	// FIXME: check for system decimal point separator instead
	if (text.contains(':') || text.contains('.')) {
		inpic = string2pts(text.toStdString()) / getTimePerFrame();
	} else {
		inpic = text.toInt();
	}

	if (!quick_picture_lookup.empty()) {
		// find the entry in the quick_picture_lookup table that corresponds to given output picture
		quick_picture_lookup_t::iterator it = std::upper_bound(quick_picture_lookup.begin(), quick_picture_lookup.end(), inpic, quick_picture_lookup_s::cmp_outpicture());
		inpic = inpic - it->outpicture + it->stoppicture;
	}

	fine = true;
	linslider->setValue(inpic);
	fine = false;
}

void dvbcut::updateimagedisplay() {
	if (showimage) {
		if (!imgp)
			imgp = new imageprovider(*mpg, new dvbcutbusy(this), false, viewscalefactor);

		QPixmap px = QPixmap::fromImage(imgp->getimage(curpic, fine));
		imagedisplay->setMinimumSize(px.size());
		imagedisplay->setPixmap(px);
		qApp->processEvents();
	}
}

void dvbcut::on_audiotrackpopup_triggered(QAction *action) {
	currentaudiotrack = action->data().toInt();
}

void dvbcut::on_recentfilespopup_triggered(QAction *action) {
	int id = action->data().toInt();

	open(settings().recentfiles[(unsigned)id].first, settings().recentfiles[(unsigned)id].second);
}

void dvbcut::on_recentfilespopup_aboutToShow() {
	QString menuitem;
	recentfilespopup->clear();

	for(unsigned int id = 0; id < settings().recentfiles.size(); id++) {
		menuitem = QString::fromStdString(settings().recentfiles[id].first.front());
		if(settings().recentfiles[id].first.size() > 1) {
			menuitem += " ... (+" + QString::number(settings().recentfiles[id].first.size()-1) + ")";
		}

		QAction *fileItem = new QAction(menuitem, recentfilespopup);
		fileItem->setData(id);
		recentfilespopup->addAction(fileItem);
	}   
}

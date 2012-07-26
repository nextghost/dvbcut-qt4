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

#include <QFileDialog>
#include <QDir>
#include <QDomDocument>
#include <QTextStream>
#include <QMessageBox>
#include <QPixmap>

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
	// FIXME: implement
}

void dvbcut::update_time_display() {

}

void dvbcut::update_quick_picture_lookup_table() {
	// FIXME: implement
}

int dvbcut::question(const QString & caption, const QString & text) {
	if (nogui) {
		fprintf(stderr, "%s\n%s\n(assuming No)\n", (const char *)caption.toLocal8Bit(), (const char *)text.toLocal8Bit());
		return QMessageBox::No;
	}

	return QMessageBox::question(this, caption, text, QMessageBox::Yes, QMessageBox::No | QMessageBox::Default | QMessageBox::Escape);
}

int dvbcut::critical(const QString & caption, const QString & text) {
	if (nogui) {
		fprintf(stderr, "%s\n%s\n(aborting)\n", (const char *)caption.toLocal8Bit(), (const char *)text.toLocal8Bit());
		return QMessageBox::Abort;
	}

	return QMessageBox::critical(this, caption, text, QMessageBox::Abort, QMessageBox::NoButton);
}

void dvbcut::make_canonical(std::string &filename) {
	QDir dir(filename.c_str());
	QString fname = dir.dirName();
	dir.cdUp();
	dir = QDir(dir.canonicalPath());

	filename = dir.filePath(fname).toStdString();
}

void dvbcut::make_canonical(std::list<std::string> &filenames) {
	std::list<std::string>::iterator it;

	for (it = filenames.begin(); it != filenames.end(); ++it) {
		make_canonical(*it);
	}
}

dvbcut::dvbcut(QWidget *parent) : QMainWindow(parent, Qt::Window),
	audioTrackGroup(new QActionGroup(this)),
	buf(8 << 20, 128 << 20), mpg(NULL), pictures(0), curpic(~0),
	showimage(true), fine(false), jogsliding(false), jogmiddlepic(0),
	imgp(NULL), busy(0), viewscalefactor(1.0), nogui(false) {

	QActionGroup *group;

	setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose);

	// View mode action group
	group = new QActionGroup(this);
	group->addAction(viewNormalAction);
	group->addAction(viewUnscaledAction);
	group->addAction(viewDifferenceAction);

	// View size action group
	group = new QActionGroup(this);
	group->addAction(viewFullSizeAction);
	group->addAction(viewHalfSizeAction);
	group->addAction(viewQuarterSizeAction);
	group->addAction(viewCustomSizeAction);

#ifndef HAVE_LIB_AO
	playAudio1Action->setEnabled(false);
	playAudio2Action->setEnabled(false);
	playToolbar->removeAction(playAudio1Action);
	playToolbar->removeAction(playAudio2Action);
	playMenu->removeAction(playAudio1Action);
	playMenu->removeAction(playAudio2Action);
#endif // ! HAVE_LIB_AO

	setviewscalefactor(settings().viewscalefactor);

	// install event handler
	linslider->installEventFilter(this);

	// set caption
	setWindowTitle(QString(VERSION_STRING));
}

dvbcut::~dvbcut(void) {
	delete imgp;
	delete mpg;
}

void dvbcut::open(std::list<std::string> filenames, std::string idxfilename, std::string expfilename) {
  if (filenames.empty()) {
    QStringList fn = QFileDialog::getOpenFileNames(
      this,
      "Open file...",
      settings().lastdir,
      settings().loadfilter);
    if (fn.empty()) {
//      fprintf(stderr,"open(): QFileDialog::getOpenFileNames() returned EMPTY filelist!!!\n");    
//      fprintf(stderr,"        If you didn't saw a FileDialog, please check your 'lastdir' settings variable...");    
      return;
    }  
    for (QStringList::Iterator it = fn.begin(); it != fn.end(); ++it)
      filenames.push_back(it->toStdString());

/* FIXME: rewrite
    // remember last directory if requested
    if (settings().lastdir_update) {
      QString dir = fn.front();
      int n = dir.findRev('/');
      if (n > 0)
        dir = dir.left(n);
      // adding a slash is NEEDED for top level directories (win or linux), and doesn't matter otherwise!
      dir = dir + "/"; 
      settings().lastdir = dir;
    }
*/
  } 

  make_canonical(filenames);

  std::string filename = filenames.front();

  // a valid file name has been entered
  setWindowTitle((VERSION_STRING " - " + filename).c_str());

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
// FIXME: rewrite
//  eventlist->clear();
//  imagedisplay->setBackgroundMode(Qt::PaletteBackground);
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
    QFile fr(filename.c_str());
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
            critical("Failed to read project file - dvbcut",
	      QString(filename.c_str()) + ":\nNot a valid dvbcut project file");
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
	    critical("Failed to read project file - dvbcut",
	      QString(filename.c_str()) + ":\nNo MPEG filename given in project file");
	    fileOpenAction->setEnabled(true);
	    return;
	  }
          prjfilename = filename;
        }
        else {
          critical("Failed to read project file - dvbcut",
	    QString(filename.c_str()) + ":\n" + errormsg);
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
    critical("Failed to open file - dvbcut", errormessage.c_str());
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
	QDir dir(idxfilename.c_str());
	QString relname = dir.dirName();
	QString curpath = QDir::currentPath();
	dir.cdUp();
	QDir::setCurrent(dir.path());

	  QString s=QFileDialog::getSaveFileName(
		  this,
		  "Choose index file...",
		  "file:" + relname,
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
      critical("Failed to open file - dvbcut",errorstring.c_str());
      fileOpenAction->setEnabled(true);
      return;
    }
    if (pictures==-2) {
      delete mpg;
      mpg=0;
      critical("Invalid index file - dvbcut", errorstring.c_str());
      fileOpenAction->setEnabled(true);
      return;
    }
    if (pictures<=-3) {
      delete mpg;
      mpg=0;
      critical("Index file mismatch - dvbcut", errorstring.c_str());
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
      critical("Error creating index - dvbcut",
	       ("Cannot create index for\n"+filename+":\n"+errorstring).c_str());
      fileOpenAction->setEnabled(true);
      return;
    } else if (!errorstring.empty()) {
      critical("Error saving index file - dvbcut", errorstring.c_str());
    }
  }

  if (pictures<1) {
    delete mpg;
    mpg=0;
    critical("Invalid MPEG file - dvbcut",
	     ("The chosen file\n"+filename+"\ndoes not contain any video").c_str());
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

// FIXME: rewrite?
//  imagedisplay->setBackgroundMode(Qt::NoBackground);
  curpic=~0;
  showimage=true;
  fine=false;
  jogsliding=false;
  jogmiddlepic=0;
  imgp=new imageprovider(*mpg,new dvbcutbusy(this),false,viewscalefactor);

  on_linslider_valueChanged(0);
  linslider->setValue(0);
  jogslider->setValue(0);

/* FIXME: rewrite
  {
    EventListItem *eli=new EventListItem(0,imgp->getimage(0),EventListItem::start,9999999,2,0);
    eventlist->setMinimumWidth(eli->width(eventlist)+24);
    delete eli;
  }
*/

  if (!domdoc.isNull()) {
    QDomElement e;
    for (QDomNode n=domdoc.documentElement().firstChild();!n.isNull();n=n.nextSibling())
      if (!(e=n.toElement()).isNull()) {
/* FIXME: rewrite
      EventListItem::eventtype evt;
      if (e.tagName()=="start")
        evt=EventListItem::start;
      else if (e.tagName()=="stop")
        evt=EventListItem::stop;
      else if (e.tagName()=="chapter")
        evt=EventListItem::chapter;
      else if (e.tagName()=="bookmark")
        evt=EventListItem::bookmark;
      else
        continue;
      bool okay=false;
      int picnum;
      QString str=e.attribute("picture","-1");
      if (str.contains(':') || str.contains('.')) {
        okay=true;
        picnum=string2pts(str)/getTimePerFrame();
      }
      else
        picnum=str.toInt(&okay,0);
      if (okay && picnum>=0 && picnum<pictures) {
        new EventListItem(eventlist,imgp->getimage(picnum),evt,picnum,(*mpg)[picnum].getpicturetype(),(*mpg)[picnum].getpts()-firstpts);
        qApp->processEvents();
      }
*/
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
//  menubar->setItemEnabled(audiotrackmenuid,true);
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
  	QAction *trackAction = audiotrackpopup->addAction(mpg->getstreaminfo(audiostream(a)).c_str());

	audioTrackGroup->addAction(trackAction);
	trackAction->setData(a);
	trackAction->setCheckable(true);

	if (!a) {
		trackAction->setChecked(true);
		currentaudiotrack = a;
	}
  }
}

void dvbcut::addStartStopItems(std::vector<int>, int option) {
	// FIXME: implement
}

void dvbcut::on_fileNewAction_triggered(void) {
	dvbcut *d = new dvbcut;
	d->show();
}

void dvbcut::on_fileOpenAction_triggered(void) {
	open();
}

void dvbcut::fileExport(void) {
	// FIXME: implement
}

void dvbcut::editSuggest(void) {
	// FIXME: implement
}

void dvbcut::editImport(void) {
	// FIXME: implement
}

void dvbcut::editConvert(int) {
	// FIXME: implement
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
		menuitem = QString(settings().recentfiles[id].first.front().c_str());
		if(settings().recentfiles[id].first.size() > 1) {
			menuitem += " ... (+" + QString::number(settings().recentfiles[id].first.size()-1) + ")";
		}

		QAction *fileItem = new QAction(menuitem, recentfilespopup);
		fileItem->setData(id);
		recentfilespopup->addAction(fileItem);
	}   
}

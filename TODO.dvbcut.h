class dvbcut: public dvbcutbase {
protected:
  void exportvideo(const char *fmt);
  void addtorecentfiles(const std::list<std::string> &filenames, const std::string &idxfilename=std::string());	// DONE
  void setviewscalefactor(double factor);	// DONE

  bool eventFilter(QObject *watched, QEvent *e);	// DONE

  void update_time_display();			// DONE
  void update_quick_picture_lookup_table();	// DONE

  int question(const QString & caption, const QString & text);	// DONE
  int critical(const QString & caption, const QString & text);	// DONE

  void make_canonical(std::string &filename);			// DONE
  void make_canonical(std::list<std::string> &filenames);	// DONE

  void addEventListItem(int pic, EventListItem::eventtype type);	// DONE

  void snapshotSave(std::vector<int> piclist, int range=0, int samples=1);	// DONE
  int chooseBestPicture(int startpic, int range, int samples);	// DONE

protected slots:
  virtual void helpAboutAction_activated();	// DONE
  virtual void helpContentAction_activated();	// DONE
	
public:
  ~dvbcut();	// In progress
  dvbcut(QWidget *parent = 0, const char *name = 0, WFlags fl = WType_TopLevel|WDestructiveClose );	// In progress
  void open(std::list<std::string> filenames=std::list<std::string>(), std::string idxfilename=std::string(), std::string expfilename=std::string());	// In progress
  void setbusy(bool b=true);	// DONE
  void batchmode(bool b=true);	// DONE
  void exportoptions(int format=0, bool bof=true, bool eof=true);	// DONE
  void addStartStopItems(std::vector<int>, int option=0);		// DONE
  int getTimePerFrame();	// DONE

public slots:
  virtual void fileNew();	// DONE: on_fileNewAction_triggered()
  virtual void fileOpen();	// DONE: on_fileOpenAction_triggered()
  virtual void fileSaveAs();	// DONE: on_fileSaveAsAction_triggered()
  virtual void fileSave();	// DONE: on_fileSaveAction_triggered()
  virtual void snapshotSave();	// DONE: on_snapshotSaveAction_triggered()
  virtual void chapterSnapshotsSave();	// DONE: on_chapterSnapshotsSaveAction_triggered()
  virtual void fileExport();
  virtual void fileClose();	// DONE: direct connection in UI file
  virtual void editBookmark();	// DONE: editAddMarker()
  virtual void editChapter();	// DONE: editAddMarker()
  virtual void editStop();	// DONE: editAddMarker()
  virtual void editStart();	// DONE: editAddMarker()
  virtual void editAutoChapters();	// DONE: on_editAutoChaptersAction_triggered()
  virtual void editSuggest();	// DONE: on_editSuggestAction_triggered()
  virtual void editImport();	// DONE: on_editImportAction_triggered()
  virtual void editConvert(int);	// DONE: editConvert() + on_editconvertpopup_triggered()
  virtual void abouttoshoweditconvert();	// DONE: direct item placement in UI file
  virtual void viewDifference();	// DONE: on_viewDifferenceAction_triggered()
  virtual void viewUnscaled();		// DONE: on_viewUnscaledAction_triggered()
  virtual void viewNormal();		// DONE: on_viewNormalAction_triggered()
  virtual void zoomIn();		// DONE: viewScaleZoom()
  virtual void zoomOut();		// DONE: viewScaleZoom()
  virtual void viewFullSize();		// DONE: setViewScaleMode()
  virtual void viewHalfSize();		// DONE: setViewScaleMode()
  virtual void viewQuarterSize();	// DONE: setViewScaleMode()
  virtual void viewCustomSize();	// DONE: setViewScaleMode()
  virtual void playAudio2();
  virtual void playAudio1();
  virtual void playStop();
  virtual void playPlay();
  virtual void jogsliderreleased();	// DONE: on_jogslider_sliderReleased()
  virtual void jogslidervalue(int);	// DONE: on_jogslider_valueChanged()
  virtual void linslidervalue(int);	// In progress: on_linslider_valueChanged()
  virtual void doubleclickedeventlist(QListBoxItem *lbi);	// DONE: on_eventlist_activated()
  virtual void eventlistcontextmenu(QListBoxItem *, const QPoint &);	// DONE: on_eventlist_customContextMenuRequested()
  virtual void mplayer_exited();
  virtual void mplayer_readstdout();
  virtual void clickedgo();			// DONE: on_gobutton_clicked()
  virtual void clickedgo2();			// DONE: on_gobutton2_clicked()
  virtual void updateimagedisplay();		// DONE
  virtual void audiotrackchosen(int id);	// DONE: on_audiotrackpopup_triggered()
  virtual void loadrecentfile(int id);		// DONE: on_recentfilespopup_triggered()
  virtual void abouttoshowrecentfiles();	// DONE: on_recentfilespopup_aboutToShow()
  };

/*  dvbcut
    Copyright (c) 2005 Sven Over <svenover@svenover.de>
 
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cstdio>
#include <cstdarg>
#include <cstdlib>

#include <QProgressBar>
#include <QTextBrowser>
#include <QPushButton>
#include <QApplication>
#include <QCloseEvent>
#include "progresswindow.h"

progresswindow::progresswindow(QWidget *parent)
    : QDialog(parent), logoutput(),
    cancelwasclicked(false), waitingforclose(false)
  {
	setupUi(this);

	QPalette pal = cancelbutton->palette();
	pal.setColor(QPalette::Button, QColor(255, 0, 0));
	pal.setColor(QPalette::Light, QColor(255, 64, 64));
	pal.setColor(QPalette::Midlight, QColor(255, 32, 32));
	pal.setColor(QPalette::Dark, QColor(191, 0, 0));
	pal.setColor(QPalette::Mid, QColor(223, 0, 0));
	cancelbutton->setPalette(pal);

	setModal(true);
  show();
  qApp->processEvents();
  }

void progresswindow::closeEvent(QCloseEvent *e)
  {
  if (waitingforclose)
    e->accept();
  else
    e->ignore();
  }

void progresswindow::finish()
  {
	QPalette pal = cancelbutton->palette();

	pal.setColor(QPalette::Button, QColor(0, 255, 0));
	pal.setColor(QPalette::Light, QColor(64, 255, 64));
	pal.setColor(QPalette::Midlight, QColor(32, 255, 32));
	pal.setColor(QPalette::Dark, QColor(0, 191, 0));
	pal.setColor(QPalette::Mid, QColor(0, 223, 0));
  cancelbutton->setEnabled(false);
  waitingforclose=true;
  cancelbutton->setText( tr( "Close" ) );
	cancelbutton->setPalette(pal);
  cancelbutton->setEnabled(true);
  exec();
  }

void progresswindow::setprogress(int permille)
  {
  if (permille==currentprogress)
    return;
  currentprogress=permille;
  progressbar->setValue(permille);
  qApp->processEvents();
  }

void progresswindow::print(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

	logbrowser->setCurrentCharFormat(QTextCharFormat());
	logbrowser->insertPlainText(QString(text) + "\n");
	logbrowser->ensureCursorVisible();
  free(text);
  qApp->processEvents();
  }

void progresswindow::printheading(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
	QTextCharFormat cf;

  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

	cf.setUnderlineStyle(QTextCharFormat::SingleUnderline);
	cf.setFontWeight(700);
	logbrowser->setCurrentCharFormat(cf);
  logbrowser->insertPlainText(QString(text) + "\n");
	logbrowser->ensureCursorVisible();
  free(text);
  qApp->processEvents();
  }

void progresswindow::printinfo(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;

  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

	logbrowser->setCurrentCharFormat(QTextCharFormat());
  logbrowser->insertPlainText(QString(text) + "\n");
	logbrowser->ensureCursorVisible();
  free(text);
  qApp->processEvents();
  }

void progresswindow::printerror(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
	QTextCharFormat cf;

  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

	cf.setUnderlineStyle(QTextCharFormat::SingleUnderline);
	cf.setFontWeight(700);
	logbrowser->setCurrentCharFormat(cf);
	logbrowser->setTextColor(Qt::red);
  logbrowser->insertPlainText(QString(text) + "\n");
	logbrowser->ensureCursorVisible();
  free(text);
  qApp->processEvents();
  }

void progresswindow::printwarning(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

	logbrowser->setCurrentCharFormat(QTextCharFormat());
	logbrowser->setTextColor(Qt::red);
  logbrowser->insertPlainText(QString(text) + "\n");
	logbrowser->ensureCursorVisible();
  free(text);
  qApp->processEvents();
  }

void progresswindow::on_cancelbutton_clicked()
  {
  if ((cancelwasclicked==false) && (waitingforclose==false)) {
	QPalette pal = cancelbutton->palette();

	pal.setColor(QPalette::Button, QColor(0, 255, 0));
	pal.setColor(QPalette::Light, QColor(64, 255, 64));
	pal.setColor(QPalette::Midlight, QColor(32, 255, 32));
	pal.setColor(QPalette::Dark, QColor(0, 191, 0));
	pal.setColor(QPalette::Mid, QColor(0, 223, 0));
    // button function is cancel
    cancelwasclicked=true;
    cancelbutton->setEnabled(false);
    qApp->processEvents();
    cancelbutton->setText( tr( "Close" ) );
	cancelbutton->setPalette(pal);
    cancelbutton->setEnabled(true);
  } else {
    // button function is close
    close();
  }
  }

QString progresswindow::quotetext(const char *text)
  {
  return QString(text).replace('&',QString("&amp;")).replace('<',QString("&lt;")).replace('>',QString("&gt;"));
  }

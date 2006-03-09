// qtractorMainForm.ui.h
//
// ui.h extension file included from the uic-generated form implementation.
/****************************************************************************
   Copyright (C) 2005-2006, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*****************************************************************************/

#include "qtractorAbout.h"
#include "qtractorOptions.h"
#include "qtractorInstrument.h"
#include "qtractorMessages.h"
#include "qtractorFiles.h"
#include "qtractorTracks.h"

#include "qtractorTrackList.h"
#include "qtractorTrackTime.h"
#include "qtractorTrackView.h"

#include "qtractorAudioPeak.h"
#include "qtractorAudioEngine.h"

#include "qtractorSessionDocument.h"
#include "qtractorSessionCursor.h"

#include "qtractorPropertyCommand.h"

#include "qtractorSpinBox.h"

#include "qtractorSessionForm.h"
#include "qtractorOptionsForm.h"
#include "qtractorInstrumentForm.h"

#include <qapplication.h>
#include <qeventloop.h>
#include <qworkspace.h>
#include <qmessagebox.h>
#include <qdragobject.h>
#include <qobjectlist.h>
#include <qregexp.h>
#include <qfiledialog.h>
#include <qfileinfo.h>
#include <qfile.h>
#include <qtextstream.h>
#include <qstatusbar.h>
#include <qlabel.h>
#include <qtimer.h>
#include <qdatetime.h>

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

// Timer constant stuff.
#define QTRACTOR_TIMER_MSECS    50
#define QTRACTOR_TIMER_DELAY    200

// Status bar item indexes
#define QTRACTOR_STATUS_NAME    0       // Active session track caption.
#define QTRACTOR_STATUS_MOD     1       // Current session modification state.
#define QTRACTOR_STATUS_REC     2       // Current session recording state.
#define QTRACTOR_STATUS_MUTE    3       // Current session muting state.
#define QTRACTOR_STATUS_SOLO    4       // Current session soloing state.
#define QTRACTOR_STATUS_LOOP    5       // Current session looping state.
#define QTRACTOR_STATUS_TIME    6       // Current session length time.
#define QTRACTOR_STATUS_RATE    7       // Current session sample rate.


// Specialties for thread-callback comunication.
#define QTRACTOR_PEAK_EVENT     QEvent::Type(QEvent::User + 1)
#define QTRACTOR_XRUN_EVENT     QEvent::Type(QEvent::User + 2)
#define QTRACTOR_SHUT_EVENT     QEvent::Type(QEvent::User + 3)


//-------------------------------------------------------------------------
// qtractorMainForm -- Main window form implementation.

// Kind of constructor.
void qtractorMainForm::init (void)
{
	// Initialize some pointer references.
	m_pOptions = NULL;
	m_pSession = new qtractorSession();
	m_pCommands = new qtractorCommandList(this);
	m_pInstruments = new qtractorInstrumentList();

	// To remember last time we've shown the playhead.
	m_iPlayHead = 0;

	// All child forms are to be created later, not earlier than setup.
	m_pMessages = NULL;
	m_pFiles    = NULL;
	m_pTracks   = NULL;

	// We'll start clean.
	m_iUntitled   = 0;
	m_iDirtyCount = 0;

	m_iPeakTimer = 0;
	m_iPlayTimer = 0;
	m_iTransport = 0;

	m_iXrunCount = 0;
	m_iXrunSkip  = 0;
	m_iXrunTimer = 0;

	// Configure the audio engine event handling...
	qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
	if (pAudioEngine) {
		pAudioEngine->setNotifyWidget(this);
		pAudioEngine->setNotifyShutdownType(QTRACTOR_SHUT_EVENT);
		pAudioEngine->setNotifyXrunType(QTRACTOR_XRUN_EVENT);
	}

	// Configure the audio file peak factory...
	qtractorAudioPeakFactory *pAudioPeakFactory
		= m_pSession->audioPeakFactory();
	if (pAudioPeakFactory) {
		pAudioPeakFactory->setNotifyWidget(this);
		pAudioPeakFactory->setNotifyPeakType(QTRACTOR_PEAK_EVENT);
	}

#ifdef HAVE_SIGNAL_H
	// Set to ignore any fatal "Broken pipe" signals.
	::signal(SIGPIPE, SIG_IGN);
#endif

	// Make it an MDI workspace.
	m_pWorkspace = new QWorkspace(this);
	m_pWorkspace->setScrollBarsEnabled(true);
	// Set the activation connection.
	QObject::connect(m_pWorkspace, SIGNAL(windowActivated(QWidget *)),
		this, SLOT(stabilizeForm()));
	// Make it shine :-)
	setCentralWidget(m_pWorkspace);

#if 0
	// Have some effective feedback when toggling play/pause...
	QIconSet icons;
	icons.setPixmap(QPixmap::fromMimeSource("transportPlay.png"),
		QIconSet::Automatic, QIconSet::Active, QIconSet::Off);
	icons.setPixmap(QPixmap::fromMimeSource("transportPause.png"),
		QIconSet::Automatic, QIconSet::Active, QIconSet::On);
	transportPlayAction->setIconSet(icons);
#endif

	// HACK: transport toolbar controls needs be auto-repeatable ...
	QObjectList *pObjList = transportToolbar->queryList(
		"QToolButton", "^transport(Backward|Forward)Action");
	if (pObjList) {	// Iterate over the intended transport tool-buttons...	
		QObjectListIt it(*pObjList);
		for (QObject *pObj; (pObj = it.current()) != NULL; ++it) {
			QToolButton *pToolButton = static_cast<QToolButton *> (pObj);
			if (pToolButton)
				pToolButton->setAutoRepeat(true);
		}
		delete pObjList;
	}

	// Additional toolbar controls...
	const QString sTime("00:00:00.000");
//	timeToolbar->addSeparator();
	m_pTransportTime = new QLabel(sTime, timeToolbar);
	m_pTransportTime->setFont(QFont(font().family(), 10));
	m_pTransportTime->setFrameShape(QFrame::Panel);
	m_pTransportTime->setFrameShadow(QFrame::Sunken);
	m_pTransportTime->setPaletteBackgroundColor(Qt::black);
//	m_pTransportTime->setPaletteForegroundColor(Qt::green);
	m_pTransportTime->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	m_pTransportTime->setMaximumHeight(
		m_pTransportTime->sizeHint().height() + 2);
	m_pTransportTime->setMinimumWidth(
		m_pTransportTime->sizeHint().width() + 2);
	QToolTip::add(m_pTransportTime, tr("Current transport time (playhead)"));
	timeToolbar->addSeparator();
	m_pTempoSpinBox = new qtractorSpinBox(timeToolbar);
	m_pTempoSpinBox->setAlignment(Qt::AlignHCenter);
	m_pTempoSpinBox->setMinValue(1);
	m_pTempoSpinBox->setMaxValue(99999);
	QToolTip::add(m_pTempoSpinBox, tr("Tempo (BPM)"));

	QObject::connect(m_pTempoSpinBox, SIGNAL(valueChanged(int)),
		this, SLOT(tempoChanged()));

	// Create some statusbar labels...
	QLabel *pLabel;
	// Track status.
	pLabel = new QLabel(tr("Track"), this);
	pLabel->setAlignment(Qt::AlignLeft);
	QToolTip::add(pLabel, tr("Current track name"));
	m_statusItems[QTRACTOR_STATUS_NAME] = pLabel;
	statusBar()->addWidget(pLabel, 2);
	// Session modification status.
	pLabel = new QLabel(tr("MOD"), this);
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint());
	QToolTip::add(pLabel, tr("Session modification state"));
	m_statusItems[QTRACTOR_STATUS_MOD] = pLabel;
	statusBar()->addWidget(pLabel);
	// Session recording status.
	pLabel = new QLabel(tr("REC"), this);
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint());
	QToolTip::add(pLabel, tr("Session record state"));
	m_statusItems[QTRACTOR_STATUS_REC] = pLabel;
	statusBar()->addWidget(pLabel);
	// Session muting status.
	pLabel = new QLabel(tr("MUTE"), this);
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint());
	QToolTip::add(pLabel, tr("Session muting state"));
	m_statusItems[QTRACTOR_STATUS_MUTE] = pLabel;
	statusBar()->addWidget(pLabel);
	// Session soloing status.
	pLabel = new QLabel(tr("SOLO"), this);
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint());
	QToolTip::add(pLabel, tr("Session soloing state"));
	m_statusItems[QTRACTOR_STATUS_SOLO] = pLabel;
	statusBar()->addWidget(pLabel);
	// Session looping status.
	pLabel = new QLabel(tr("LOOP"), this);
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint());
	QToolTip::add(pLabel, tr("Session looping state"));
	m_statusItems[QTRACTOR_STATUS_LOOP] = pLabel;
	statusBar()->addWidget(pLabel);
	// Session length time.
	pLabel = new QLabel(sTime, this);
	pLabel->setAlignment(Qt::AlignRight);
	pLabel->setMinimumSize(pLabel->sizeHint());
	QToolTip::add(pLabel, tr("Session total time"));
	m_statusItems[QTRACTOR_STATUS_TIME] = pLabel;
	statusBar()->addWidget(pLabel);
	// Session sample rate.
	const QString sRate("44100 Hz");
	pLabel = new QLabel(sRate, this);
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint());
	QToolTip::add(pLabel, tr("Session sample rate"));
	m_statusItems[QTRACTOR_STATUS_RATE] = pLabel;
	statusBar()->addWidget(pLabel);

	// Create the recent files sub-menu.
	m_pRecentFilesMenu = new QPopupMenu(this);
	fileMenu->insertItem(tr("Open &Recent"), m_pRecentFilesMenu, 0, 3);
}


// Kind of destructor.
void qtractorMainForm::destroy (void)
{
	// Drop any widgets around (not really necessary)...
	if (m_pTracks)
		delete m_pTracks;
	if (m_pFiles)
		delete m_pFiles;
	if (m_pMessages)
		delete m_pMessages;
	if (m_pWorkspace)
		delete m_pWorkspace;

	//  Free some data around...
	if (m_pInstruments)
		delete m_pInstruments;
	if (m_pCommands)
		delete m_pCommands;
	if (m_pSession)
		delete m_pSession;

	// Finally, delete recent files menu.
	if (m_pRecentFilesMenu)
		delete m_pRecentFilesMenu;
}


// Make and set a proper setup options step.
void qtractorMainForm::setup ( qtractorOptions *pOptions )
{
	// We got options?
	m_pOptions = pOptions;

	// Some child forms are to be created right now.
	m_pMessages = new qtractorMessages(this);
	m_pFiles = new qtractorFiles(this);
	m_pFiles->audioListView()->setRecentDir(m_pOptions->sAudioDir);
	m_pFiles->midiListView()->setRecentDir(m_pOptions->sMidiDir);

	// Set message defaults...
	updateMessagesFont();
	updateMessagesLimit();
	updateMessagesCapture();

	// Set the visibility signal.
	QObject::connect(m_pMessages, SIGNAL(visibilityChanged(bool)),
		this, SLOT(stabilizeForm()));
	QObject::connect(m_pFiles, SIGNAL(visibilityChanged(bool)),
		this, SLOT(stabilizeForm()));
	// Contents change stuff...
	QObject::connect(m_pFiles->audioListView(), SIGNAL(activated(const QString&)),
		this, SLOT(activateAudioFile(const QString&)));
	QObject::connect(m_pFiles->midiListView(), SIGNAL(activated(const QString&)),
		this, SLOT(activateMidiFile(const QString&)));
	QObject::connect(m_pFiles->audioListView(), SIGNAL(contentsChanged()),
		this, SLOT(contentsChanged()));
	QObject::connect(m_pFiles->midiListView(), SIGNAL(contentsChanged()),
		this, SLOT(contentsChanged()));

	// Initial decorations toggle state.
	viewMenubarAction->setOn(m_pOptions->bMenubar);
	viewStatusbarAction->setOn(m_pOptions->bStatusbar);
	viewToolbarFileAction->setOn(m_pOptions->bFileToolbar);
	viewToolbarEditAction->setOn(m_pOptions->bEditToolbar);
	viewToolbarTrackAction->setOn(m_pOptions->bTrackToolbar);
	viewToolbarTransportAction->setOn(m_pOptions->bTransportToolbar);
	viewToolbarTimeAction->setOn(m_pOptions->bTimeToolbar);

	transportFollowAction->setOn(m_pOptions->bFollowPlayhead);

	// Initial decorations visibility state.
	viewMenubar(m_pOptions->bMenubar);
	viewStatusbar(m_pOptions->bStatusbar);
	viewToolbarFile(m_pOptions->bFileToolbar);
	viewToolbarEdit(m_pOptions->bEditToolbar);
	viewToolbarTrack(m_pOptions->bTrackToolbar);
	viewToolbarTransport(m_pOptions->bTransportToolbar);
	viewToolbarTime(m_pOptions->bTimeToolbar);

	// Restore whole dock windows state.
	QString sDockables = m_pOptions->settings().readEntry(
		"/Layout/DockWindows", QString::null);
	if (sDockables.isEmpty()) {
		// Message window is forced to dock on the bottom.
		moveDockWindow(m_pMessages, Qt::DockBottom);
		moveDockWindow(m_pFiles, Qt::DockRight);
	} else {
		// Make it as the last time.
		QTextIStream istr(&sDockables);
		istr >> *this;
	}
	// Try to restore old window positioning.
	m_pOptions->loadWidgetGeometry(this);

	// Load instrument definition files...
	for (QStringList::Iterator iter = m_pOptions->instrumentFiles.begin();
			iter != m_pOptions->instrumentFiles.end(); ++iter) {
		m_pInstruments->load(*iter);
	}

	// Primary startup stabilization...
	updateRecentFilesMenu();
	updatePeakAutoRemove();

	// Is any session pending to be loaded?
	if (!m_pOptions->sSessionFile.isEmpty()) {
		// Just load the prabably startup session...
		if (loadSessionFile(m_pOptions->sSessionFile))
			m_pOptions->sSessionFile = QString::null;
	} else {
		// Open up with a new empty session...
		newSession();
	}

	// Make it ready :-)
	statusBar()->message(tr("Ready"), 3000);

	// Register the first timer slot.
	QTimer::singleShot(QTRACTOR_TIMER_MSECS, this, SLOT(timerSlot()));
}


// The main MDI workspace widget accessor.
QWorkspace *qtractorMainForm::workspace (void)
{
	return m_pWorkspace;
}


// Window close event handlers.
bool qtractorMainForm::queryClose (void)
{
	bool bQueryClose = closeSession();

	// Try to save current general state...
	if (m_pOptions) {
		// Some windows default fonts is here on demand too.
		if (bQueryClose && m_pMessages)
			m_pOptions->sMessagesFont = m_pMessages->messagesFont().toString();
		// Save recent directories...
		if (bQueryClose && m_pFiles) {
			m_pOptions->sAudioDir = m_pFiles->audioListView()->recentDir();
			m_pOptions->sMidiDir = m_pFiles->midiListView()->recentDir();
		}
		// Try to save current positioning.
		if (bQueryClose) {
			// Save decorations state.
			m_pOptions->bMenubar = MenuBar->isVisible();
			m_pOptions->bStatusbar = statusBar()->isVisible();
			m_pOptions->bFileToolbar = fileToolbar->isVisible();
			m_pOptions->bEditToolbar = editToolbar->isVisible();
			m_pOptions->bTrackToolbar = trackToolbar->isVisible();
			m_pOptions->bTransportToolbar = transportToolbar->isVisible();
			m_pOptions->bTimeToolbar = timeToolbar->isVisible();
			m_pOptions->bFollowPlayhead = transportFollowAction->isOn();
			// Save instrument definition file list...
			m_pOptions->instrumentFiles = m_pInstruments->files();
			// Save the dock windows state.
			QString sDockables;
			QTextOStream ostr(&sDockables);
			ostr << *this;
			m_pOptions->settings().writeEntry("/Layout/DockWindows", sDockables);
			// And the main windows state.
			m_pOptions->saveWidgetGeometry(this);
		}
	}

	return bQueryClose;
}


void qtractorMainForm::closeEvent ( QCloseEvent *pCloseEvent )
{
	if (queryClose())
		pCloseEvent->accept();
	else
		pCloseEvent->ignore();
}


// Drag'n'drop file handler.
bool qtractorMainForm::decodeDragFiles ( const QMimeSource *pEvent,
	QStringList& files )
{
	bool bDecode = false;

	if (QTextDrag::canDecode(pEvent)) {
		QString sText;
		bDecode = QTextDrag::decode(pEvent, sText);
		if (bDecode) {
			files = QStringList::split('\n', sText);
			for (QStringList::Iterator iter = files.begin();
					iter != files.end(); ++iter) {
				*iter = QUrl((*iter).stripWhiteSpace()
					.replace(QRegExp("^file:"), QString::null)).path();
			}
		}
	}

	return bDecode;
}


// Window drag-n-drop event handlers.
void qtractorMainForm::dragEnterEvent ( QDragEnterEvent* pDragEnterEvent )
{
	QStringList files;
	pDragEnterEvent->accept(decodeDragFiles(pDragEnterEvent, files));
}


void qtractorMainForm::dropEvent ( QDropEvent* pDropEvent )
{
	QStringList files;

	if (!decodeDragFiles(pDropEvent, files))
		return;

	for (QStringList::Iterator iter = files.begin();
			iter != files.end(); ++iter) {
		const QString& sPath = *iter;
		// Close current session and try to load the new one...
		if (closeSession())
			loadSessionFile(sPath);
		// Make it look responsive...:)
		QApplication::eventLoop()->processEvents(QEventLoop::ExcludeUserInput);
	}
}


// Custome event handler.
void qtractorMainForm::customEvent ( QCustomEvent *pCustomEvent )
{
#ifdef CONFIG_DEBUG_0
	appendMessages("qtractorMainForm::customEvent(" + QString::number((int) pCustomEvent->type()) + ")");
#endif

	switch (pCustomEvent->type()) {
	case QTRACTOR_PEAK_EVENT:
		// A peak file has just been (re)created;
		// try to postpone the event effect a little more...
		m_iPeakTimer += QTRACTOR_TIMER_DELAY;
		break;
	case QTRACTOR_XRUN_EVENT:
		// An XRUN has just been notified...
		m_iXrunCount++;
		// Skip this one, maybe we're under some kind of storm;
		if (m_iXrunTimer > 0)
			m_iXrunSkip++;
		// Defer the informative effect...
		m_iXrunTimer += QTRACTOR_TIMER_DELAY;
		break;
	case QTRACTOR_SHUT_EVENT:
		// Just in case we were in the middle of something...
		if (m_pSession->isPlaying()) {
			transportPlayAction->setOn(false);
			transportPlay(); // Toggle playing!
		}
		// Engine shutdown is on demand...
		m_pSession->close();
		// Send an informative message box...
		appendMessagesError(
			tr("The audio engine has been shutdown.\n\n"
			"Make sure the JACK audio server (jackd)\n"
			"is up and running and then restart session."));
		// Make things just bearable...
		stabilizeForm();
		// Fall thru.
	default:
		break;
	}
}


// Context menu event handler.
void qtractorMainForm::contextMenuEvent( QContextMenuEvent *pEvent )
{
	stabilizeForm();

	// Primordial edit menu should be available...
	editMenu->exec(pEvent->globalPos());
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Brainless public property accessors.

// The global options settings property.
qtractorOptions *qtractorMainForm::options (void)
{
	return m_pOptions;
}

// The global session reference.
qtractorSession *qtractorMainForm::session (void)
{
	return m_pSession;
}

// The global session tracks reference.
qtractorTracks *qtractorMainForm::tracks (void)
{
	return m_pTracks;
}

// The global undoable command list reference.
qtractorCommandList *qtractorMainForm::commands (void)
{
	return m_pCommands;
}

// The global instruments repository.
qtractorInstrumentList *qtractorMainForm::instruments (void)
{
	return m_pInstruments;
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Session file stuff.

// Format the displayable session filename.
QString qtractorMainForm::sessionName ( const QString& sFilename )
{
	bool bCompletePath = (m_pOptions && m_pOptions->bCompletePath);
	QString sSessionName = sFilename;
	if (sSessionName.isEmpty())
		sSessionName = tr("Untitled") + QString::number(m_iUntitled);
	else if (!bCompletePath)
		sSessionName = QFileInfo(sSessionName).fileName();
	return sSessionName;
}


// Create a new session file from scratch.
bool qtractorMainForm::newSession (void)
{
	// Check if we can do it.
	if (!closeSession())
		return false;

	// Ok increment untitled count.
	m_iUntitled++;

	// Stabilize form.
	m_sFilename = QString::null;
	m_iDirtyCount = 0;
	appendMessages(tr("New session: \"%1\".").arg(sessionName(m_sFilename)));

	// Give us what we got, right now...
	updateSession();

	return true;
}


// Open an existing sampler session.
bool qtractorMainForm::openSession (void)
{
	if (m_pOptions == NULL)
		return false;

	// Ask for the filename to open...
	QString sFilename = QFileDialog::getOpenFileName(
		m_pOptions->sSessionDir,                // Start here.
		tr("Session files") + " (*.qtr)",       // Filter files.
		this, 0,                                // Parent and name (none)
		tr("Open Session") + " - " QTRACTOR_TITLE // Caption.
	);

	// Have we cancelled?
	if (sFilename.isEmpty())
		return false;

	// Check if we're going to discard safely the current one...
	if (!closeSession())
		return false;

	// Load it right away.
	return loadSessionFile(sFilename);
}


// Save current sampler session with another name.
bool qtractorMainForm::saveSession ( bool bPrompt )
{
	if (m_pOptions == NULL)
		return false;

	QString sFilename = m_sFilename;

	// Ask for the file to save, if there's none...
	if (bPrompt || sFilename.isEmpty()) {
		// If none is given, assume default directory.
		if (sFilename.isEmpty()) {
			sFilename = QFileInfo(m_pOptions->sSessionDir,
				m_pSession->sessionName()).filePath();
		}
		// Prompt the guy...
		sFilename = QFileDialog::getSaveFileName(
			sFilename,                              // Start here.
			tr("Session files") + " (*.qtr)",       // Filter files.
			this, 0,                                // Parent and name (none)
			tr("Save Session") + " - " QTRACTOR_TITLE // Caption.
		);
		// Have we cancelled it?
		if (sFilename.isEmpty())
			return false;
		// Enforce .qtr extension...
		if (QFileInfo(sFilename).extension().isEmpty())
			sFilename += ".qtr";
		// Check if already exists...
		if (sFilename != m_sFilename && QFileInfo(sFilename).exists()) {
			if (QMessageBox::warning(this,
				tr("Warning") + " - " QTRACTOR_TITLE,
				tr("The file already exists:\n\n"
				"\"%1\"\n\n"
				"Do you want to replace it?")
				.arg(sFilename),
				tr("Replace"), tr("Cancel")) > 0)
				return false;
		}
	}

	// Save it right away.
	return saveSessionFile(sFilename);
}


// Close current session.
bool qtractorMainForm::closeSession (void)
{
	bool bClose = true;

	// Are we dirty enough to prompt it?
	if (m_iDirtyCount > 0) {
		switch (QMessageBox::warning(this,
			tr("Warning") + " - " QTRACTOR_TITLE,
			tr("The current session has been changed:\n\n"
			"\"%1\"\n\n"
			"Do you want to save the changes?")
			.arg(sessionName(m_sFilename)),
			tr("Save"), tr("Discard"), tr("Cancel"))) {
		case 0:     // Save...
			bClose = saveSession(false);
			// Fall thru....
		case 1:     // Discard
			break;
		default:    // Cancel.
			bClose = false;
			break;
		}
	}

	// If we may close it, dot it.
	if (bClose) {
		// Just in case we were in the middle of something...
		if (m_pSession->isPlaying()) {
			transportPlayAction->setOn(false);
			transportPlay(); // Toggle playing!
		}
		// Close session engines.
		m_pSession->close();
		// Reset session to default.
		m_pCommands->clear();
		m_pSession->clear();
		m_pFiles->clear();
		// Surely this will be deleted next...
		m_pTracks = NULL;
		// Reset playhead.
		m_iPlayHead = 0;
		// Remove all channel strips from sight...
		m_pWorkspace->setUpdatesEnabled(false);
		QWidgetList wlist = m_pWorkspace->windowList();
		for (int i = 0; i < (int) wlist.count(); i++)
			delete wlist.at(i);
		m_pWorkspace->setUpdatesEnabled(true);
		// We're now clean, for sure.
		m_iDirtyCount = 0;
		appendMessages(tr("Session closed."));
	}

	return bClose;
}


// Load a session from specific file path.
bool qtractorMainForm::loadSessionFile ( const QString& sFilename )
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::loadSessionFile(\"" + sFilename + "\")");
#endif

	// Read the file.
	QDomDocument doc("qtractorSession");
	if (!qtractorSessionDocument(&doc, m_pSession, m_pFiles).load(sFilename)) {
		appendMessagesError(
			tr("Session could not be loaded\n"
			"from \"%1\".\n\n"
			"Sorry.").arg(sFilename));
	}

	// Save as default session directory.
	if (m_pOptions)
		m_pOptions->sSessionDir = QFileInfo(sFilename).dirPath(true);
	// We're not dirty anymore.
	m_iDirtyCount = 0;
	// Stabilize form...
	m_sFilename = sFilename;
	updateRecentFiles(sFilename);
	appendMessages(tr("Open session: \"%1\".").arg(sessionName(m_sFilename)));

	// Now we'll try to create (update) the whole GUI session.
	updateSession();

	return true;
}


// Save current session to specific file path.
bool qtractorMainForm::saveSessionFile ( const QString& sFilename )
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::saveSessionFile(\"" + sFilename + "\")");
#endif

	// Have we any errors?
	QDomDocument doc("qtractorSession");
	if (!qtractorSessionDocument(&doc, m_pSession, m_pFiles).save(sFilename)) {
		appendMessagesError(
			tr("Some settings could not be saved\n"
			"to \"%1\" session file.\n\n"
			"Sorry.").arg(sFilename));
	}

	// Save as default session directory.
	if (m_pOptions)
		m_pOptions->sSessionDir = QFileInfo(sFilename).dirPath(true);
	// We're not dirty anymore.
	m_iDirtyCount = 0;
	// Stabilize form...
	m_sFilename = sFilename;
	updateRecentFiles(sFilename);
	appendMessages(tr("Save session: \"%1\".").arg(sessionName(m_sFilename)));
	stabilizeForm();
	return true;
}


//-------------------------------------------------------------------------
// qtractorMainForm -- File Action slots.

// Create a new sampler session.
void qtractorMainForm::fileNew (void)
{
	// Of course we'll start clean new.
	newSession();
}


// Open an existing sampler session.
void qtractorMainForm::fileOpen (void)
{
	// Open it right away.
	openSession();
}


// Open a recent file session.
void qtractorMainForm::fileOpenRecent ( int iIndex )
{
	// Check if we can safely close the current session...
	if (m_pOptions && closeSession()) {
		QString sFilename = m_pOptions->recentFiles[iIndex];
		loadSessionFile(sFilename);
	}
}


// Save current sampler session.
void qtractorMainForm::fileSave (void)
{
	// Save it right away.
	saveSession(false);
}


// Save current sampler session with another name.
void qtractorMainForm::fileSaveAs (void)
{
	// Save it right away, maybe with another name.
	saveSession(true);
}


// Edit session properties.
void qtractorMainForm::fileProperties (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::fileProperties()");
#endif

	// Session Properties...
	qtractorSessionForm sessionForm(this);
	sessionForm.setSession(m_pSession);
	if (sessionForm.exec()) {
		m_pCommands->exec(
			new qtractorPropertyCommand<qtractorSession::Properties> (this,
				tr("session properties"), m_pSession->properties(),
					sessionForm.properties()));
	}
}


// Exit application program.
void qtractorMainForm::fileExit (void)
{
	// Go for close the whole thing.
	close();
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Edit Action slots.

// Undo last action.
void qtractorMainForm::editUndo (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::editUndo()");
#endif

	m_pCommands->undo();
}


// Redo last action.
void qtractorMainForm::editRedo (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::editRedo()");
#endif

	m_pCommands->redo();
}


// Cut selection to clipboard.
void qtractorMainForm::editCut (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::editCut()");
#endif

	// Cut selection...
	if (m_pTracks)
		m_pTracks->cutClipSelect();
}


// Copy selection to clipboard.
void qtractorMainForm::editCopy (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::editCopy()");
#endif

	// Copy selection...
	if (m_pTracks)
		m_pTracks->copyClipSelect();

	stabilizeForm();
}


// Paste clipboard contents.
void qtractorMainForm::editPaste (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::editPaste()");
#endif

	// Paste selection...
	if (m_pTracks)
		m_pTracks->pasteClipSelect();
}


// Delete selection.
void qtractorMainForm::editDelete (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::editDelete()");
#endif

	// Delete selection...
	if (m_pTracks)
		m_pTracks->deleteClipSelect();
}


// Mark track as selected.
void qtractorMainForm::editSelectTrack (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::editSelectTrack()");
#endif

	// Select Track...
	if (m_pTracks)
		m_pTracks->selectCurrentTrack();

	stabilizeForm();
}


// Mark all as selected.
void qtractorMainForm::editSelectAll (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::editSelectAll()");
#endif

	// Select All...
	if (m_pTracks)
		m_pTracks->selectAll();

	stabilizeForm();
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Track Action slots.

// Add a new track to session.
void qtractorMainForm::trackAdd (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::trackAdd()");
#endif

	// Add Track...
	m_pTracks->addTrack();
}


// Remove current track from session.
void qtractorMainForm::trackRemove (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::trackAdd()");
#endif

	// Remove Track...
	if (m_pTracks)
		m_pTracks->removeTrack();
}


// Edit track properties on session.
void qtractorMainForm::trackProperties (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::trackProperties()");
#endif

	// Track Properties...
	if (m_pTracks)
		m_pTracks->editTrack();
}


// Import some tracks from Audio file.
void qtractorMainForm::trackImportAudio (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::trackImportAudio()");
#endif

	// Import Audio files into tracks...
	if (m_pTracks) {
		unsigned long iClipStart = m_pSession->editHead();
		m_pTracks->addAudioTracks(m_pFiles->audioListView()->openFileNames(),
			iClipStart);
		m_pTracks->trackView()->ensureVisibleFrame(iClipStart);
	}
}


// Import some tracks from MIDI file.
void qtractorMainForm::trackImportMidi (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::trackImportMidi()");
#endif

	// Import MIDI files into tracks...
	if (m_pTracks) {
		unsigned long iClipStart = m_pSession->editHead();
		m_pTracks->addMidiTracks(m_pFiles->midiListView()->openFileNames(),
			iClipStart);
		m_pTracks->trackView()->ensureVisibleFrame(iClipStart);
	}
}


//-------------------------------------------------------------------------
// qtractorMainForm -- View Action slots.

// Show/hide the main program window menubar.
void qtractorMainForm::viewMenubar ( bool bOn )
{
	if (bOn)
		MenuBar->show();
	else
		MenuBar->hide();
}


// Show/hide the main program window statusbar.
void qtractorMainForm::viewStatusbar ( bool bOn )
{
	if (bOn)
		statusBar()->show();
	else
		statusBar()->hide();
}


// Show/hide the file-toolbar.
void qtractorMainForm::viewToolbarFile ( bool bOn )
{
	if (bOn) {
		fileToolbar->show();
	} else {
		fileToolbar->hide();
	}
}


// Show/hide the edit-toolbar.
void qtractorMainForm::viewToolbarEdit ( bool bOn )
{
	if (bOn) {
		editToolbar->show();
	} else {
		editToolbar->hide();
	}
}


// Show/hide the track-toolbar.
void qtractorMainForm::viewToolbarTrack ( bool bOn )
{
	if (bOn) {
		trackToolbar->show();
	} else {
		trackToolbar->hide();
	}
}


// Show/hide the transport toolbar.
void qtractorMainForm::viewToolbarTransport ( bool bOn )
{
	if (bOn) {
		transportToolbar->show();
	} else {
		transportToolbar->hide();
	}
}


// Show/hide the time toolbar.
void qtractorMainForm::viewToolbarTime ( bool bOn )
{
	if (bOn) {
		timeToolbar->show();
	} else {
		timeToolbar->hide();
	}
}


// Show/hide the messages window logger.
void qtractorMainForm::viewMessages ( bool bOn )
{
	if (bOn)
		m_pMessages->show();
	else
		m_pMessages->hide();
}


// Show/hide the files window view.
void qtractorMainForm::viewFiles ( bool bOn )
{
	if (bOn)
		m_pFiles->show();
	else
		m_pFiles->hide();
}


// Refresh view display.
void qtractorMainForm::viewRefresh (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::viewRefresh()");
#endif

	// Update the whole session view dependables...
	m_pSession->updateTimeScale();
	m_pSession->updateSessionLength();
	if (m_pTracks)
		m_pTracks->updateContents(true);

	stabilizeForm();
}


// Show instruments dialog.
void qtractorMainForm::viewInstruments (void)
{
	// Just set and show the instruments dialog...
	qtractorInstrumentForm instrumentForm(this);
	instrumentForm.setInstruments(m_pInstruments);
	instrumentForm.setOptions(m_pOptions);
	instrumentForm.exec();
}


// Show options dialog.
void qtractorMainForm::viewOptions (void)
{
	if (m_pOptions == NULL)
		return;

	// Check out some initial nullities(tm)...
	if (m_pOptions->sMessagesFont.isEmpty() && m_pMessages)
		m_pOptions->sMessagesFont = m_pMessages->messagesFont().toString();
	// To track down deferred or immediate changes.
	QString sOldMessagesFont    = m_pOptions->sMessagesFont;
	bool    bOldStdoutCapture   = m_pOptions->bStdoutCapture;
	int     bOldMessagesLimit   = m_pOptions->bMessagesLimit;
	int     iOldMessagesLimitLines = m_pOptions->iMessagesLimitLines;
	bool    bOldCompletePath    = m_pOptions->bCompletePath;
	bool    bOldPeakAutoRemove  = m_pOptions->bPeakAutoRemove;
	int     iOldMaxRecentFiles  = m_pOptions->iMaxRecentFiles;
	// Load the current setup settings.
	qtractorOptionsForm optionsForm(this);
	optionsForm.setOptions(m_pOptions);
	// Show the setup dialog...
	if (optionsForm.exec()) {
		// Warn if something will be only effective on next run.
		if (( bOldStdoutCapture && !m_pOptions->bStdoutCapture) ||
			(!bOldStdoutCapture &&  m_pOptions->bStdoutCapture)) {
			QMessageBox::information(this,
				tr("Information") + " - " QTRACTOR_TITLE,
				tr("Some settings may be only effective\n"
				"next time you start this program."), tr("OK"));
			updateMessagesCapture();
		}
		// Check wheather something immediate has changed.
		if (( bOldCompletePath && !m_pOptions->bCompletePath) ||
			(!bOldCompletePath &&  m_pOptions->bCompletePath) ||
			(iOldMaxRecentFiles != m_pOptions->iMaxRecentFiles))
			updateRecentFilesMenu();
		if (( bOldPeakAutoRemove && !m_pOptions->bPeakAutoRemove) ||
			(!bOldPeakAutoRemove &&  m_pOptions->bPeakAutoRemove))
			updatePeakAutoRemove();
		if (sOldMessagesFont != m_pOptions->sMessagesFont)
			updateMessagesFont();
		if (( bOldMessagesLimit && !m_pOptions->bMessagesLimit) ||
			(!bOldMessagesLimit &&  m_pOptions->bMessagesLimit) ||
			(iOldMessagesLimitLines !=  m_pOptions->iMessagesLimitLines))
			updateMessagesLimit();
	}

	// This makes it.
	stabilizeForm();
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Transport Action slots.

// Transport rewind.
void qtractorMainForm::transportRewind (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::transportRewind()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Move playhead to edit-tail, head or full session-start.
	unsigned long iPlayHead = m_pSession->playHead();
	if (iPlayHead > m_pSession->editTail() && !m_pSession->isPlaying())
		iPlayHead = m_pSession->editTail();
	else
	if (iPlayHead > m_pSession->editHead())
		iPlayHead = m_pSession->editHead();
	else
		iPlayHead = 0;
	m_pSession->setPlayHead(iPlayHead);
	m_iTransport++;

	stabilizeForm();
}


// Transport backward
void qtractorMainForm::transportBackward (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::transportBackward()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Move playhead one second backward....
	unsigned long iPlayHead = m_pSession->playHead();
	if (iPlayHead > m_pSession->sampleRate())
		iPlayHead -= m_pSession->sampleRate();
	else
		iPlayHead = 0;
	m_pSession->setPlayHead(iPlayHead);
	m_iTransport++;

	stabilizeForm();
}


// Transport forward
void qtractorMainForm::transportForward (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::transportForward()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Move playhead one second forward....
	m_pSession->setPlayHead(m_pSession->playHead() + m_pSession->sampleRate());
	m_iTransport++;

	stabilizeForm();
}


// Transport fast-forward
void qtractorMainForm::transportFastForward (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::transportFastForward()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Move playhead to edit-head, tail or full session-end.
	unsigned long iPlayHead = m_pSession->playHead();
	if (iPlayHead < m_pSession->editHead())
		iPlayHead = m_pSession->editHead();
	else
	if (iPlayHead < m_pSession->editTail())
		iPlayHead = m_pSession->editTail();
	else
		iPlayHead = m_pSession->sessionLength();
	m_pSession->setPlayHead(iPlayHead);
	m_iTransport++;

	stabilizeForm();
}


// Transport loop.
void qtractorMainForm::transportLoop (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::transportLoop()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Do the loop switch...
	if (!m_pSession->isLooping()) {
		m_pSession->setLoop(m_pSession->editHead(), m_pSession->editTail());
	} else {
		m_pSession->setLoop(0, 0);
	}

	// Refresh track views...
	if (m_pTracks) {
		m_pTracks->trackTime()->updateContents();
		m_pTracks->trackView()->updateContents();
	}

	// Done with loop switch...
	stabilizeForm();
}


// Transport play.
void qtractorMainForm::transportPlay (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::transportPlay()");
#endif

	// Make sure session is activated...
	if (!checkRestartSession())
		return;

	// In case of (re)starting playback, send now
	// all tracks MIDI bank select/program changes...
	bool bPlaying = !m_pSession->isPlaying();
	if (bPlaying)
		m_pSession->setMidiPatch(m_pInstruments);
	// Toggle engine play status...
	m_pSession->setPlaying(bPlaying);
	m_iTransport++;

	// Done with playback switch...
	stabilizeForm();
}


// Transport record.
void qtractorMainForm::transportRecord (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::transportRecord()");
#endif

	// Make sure session is activated...
	if (!checkRestartSession())
		return;

	//
	// TODO: Record switch...
	//

	// Done with record switch...
	stabilizeForm();
}


// Transport follow playhead
void qtractorMainForm::transportFollow (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::transportFollow()");
#endif

	// Toggle follow-playhead...
	stabilizeForm();
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Help Action slots.

// Show information about the Qt toolkit.
void qtractorMainForm::helpAboutQt (void)
{
	QMessageBox::aboutQt(this);
}


// Show information about application program.
void qtractorMainForm::helpAbout (void)
{
	// Stuff the about box text...
	QString sText = "<p>\n";
	sText += "<b>" QTRACTOR_TITLE " - " + tr(QTRACTOR_SUBTITLE) + "</b><br />\n";
	sText += "<br />\n";
	sText += tr("Version") + ": <b>" QTRACTOR_VERSION "</b><br />\n";
	sText += "<small>" + tr("Build") + ": " __DATE__ " " __TIME__ "</small><br />\n";
#ifdef CONFIG_DEBUG
	sText += "<small><font color=\"red\">";
	sText += tr("Debugging option enabled.");
	sText += "</font></small><br />";
#endif
	sText += "<br />\n";
	sText += tr("Website") + ": <a href=\"" QTRACTOR_WEBSITE "\">" QTRACTOR_WEBSITE "</a><br />\n";
	sText += "<br />\n";
	sText += "<small>";
	sText += QTRACTOR_COPYRIGHT "<br />\n";
	sText += "<br />\n";
	sText += tr("This program is free software; you can redistribute it and/or modify it") + "<br />\n";
	sText += tr("under the terms of the GNU General Public License version 2 or later.");
	sText += "</small>";
	sText += "</p>\n";

	QMessageBox::about(this, tr("About") + " " QTRACTOR_TITLE, sText);
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Main window stabilization.

void qtractorMainForm::updateTransportTime ( unsigned long iPlayHead )
{
	m_pTransportTime->setText(
		m_pSession->timeFromFrame(iPlayHead,
			m_pOptions && m_pOptions->bTransportTime)
	);
}


void qtractorMainForm::updateActionCommand ( QAction *pAction,
	qtractorCommand *pCommand )
{
	const QRegExp rxBrackets(" \\([^\\)]+\\)$");
	pAction->setMenuText(pAction->menuText().remove(rxBrackets));
	pAction->setStatusTip(pAction->statusTip().remove(rxBrackets));
	pAction->setToolTip(pAction->toolTip().remove(rxBrackets));
	if (pCommand) {
		const QString sBrackets = QString(" (%1)").arg(pCommand->name());
		pAction->setMenuText(pAction->menuText() + sBrackets);
		pAction->setStatusTip(pAction->statusTip() + sBrackets);
		pAction->setToolTip(pAction->toolTip() + sBrackets);
	}
	pAction->setEnabled(pCommand != NULL);
}


void qtractorMainForm::stabilizeForm (void)
{
#ifdef CONFIG_DEBUG_0
	appendMessages("qtractorMainForm::stabilizeForm()");
#endif

	// Update the main application caption...
	QString sSessionName = sessionName(m_sFilename);
	if (m_iDirtyCount > 0)
		sSessionName += ' ' + tr("[modified]");
	setCaption(sSessionName + " - " QTRACTOR_TITLE);

	// Update the main menu state...
	fileSaveAction->setEnabled(m_iDirtyCount > 0);

	// Update edit menu state...
	updateActionCommand(editUndoAction, m_pCommands->lastCommand());
	updateActionCommand(editRedoAction, m_pCommands->nextCommand());

	unsigned long iSessionLength = m_pSession->sessionLength();
	bool bEnabled = (m_pTracks && m_pTracks->currentTrack() != NULL);
	bool bSelected = (m_pTracks && m_pTracks->isClipSelected());
	bool bSelectable = (iSessionLength > 0);

	editCutAction->setEnabled(bSelected);
	editCopyAction->setEnabled(bSelected);
	editPasteAction->setEnabled(m_pTracks && !m_pTracks->isClipboardEmpty());
	editDeleteAction->setEnabled(bSelected);
	editSelectTrackAction->setEnabled(bEnabled && bSelectable);
	editSelectAllAction->setEnabled(bSelectable);

	// Update track menu state...
	trackRemoveAction->setEnabled(bEnabled);
	trackPropertiesAction->setEnabled(bEnabled);
	trackImportAudioAction->setEnabled(m_pTracks != NULL);
	trackImportMidiAction->setEnabled(m_pTracks != NULL);

	// Update view menu state...
	viewMessagesAction->setOn(m_pMessages && m_pMessages->isVisible());
	viewFilesAction->setOn(m_pFiles && m_pFiles->isVisible());

	// Recent files menu.
	m_pRecentFilesMenu->setEnabled(m_pOptions->recentFiles.count() > 0);

	// Always make the latest message visible.
	if (m_pMessages)
		m_pMessages->scrollToBottom();

	// Session status...
	updateTransportTime(m_iPlayHead);

	if (m_pTracks && m_pTracks->currentTrack()) {
		m_statusItems[QTRACTOR_STATUS_NAME]->setText(
			m_pTracks->currentTrack()->trackName().simplifyWhiteSpace());
	} else {
		m_statusItems[QTRACTOR_STATUS_NAME]->clear();
	}

	if (m_iDirtyCount > 0)
		m_statusItems[QTRACTOR_STATUS_MOD]->setText(tr("MOD"));
	else
		m_statusItems[QTRACTOR_STATUS_MOD]->clear();

	if (m_pSession->recordTracks() > 0)
		m_statusItems[QTRACTOR_STATUS_REC]->setText(tr("REC"));
	else
		m_statusItems[QTRACTOR_STATUS_REC]->clear();

	if (m_pSession->muteTracks() > 0)
		m_statusItems[QTRACTOR_STATUS_MUTE]->setText(tr("MUTE"));
	else
		m_statusItems[QTRACTOR_STATUS_MUTE]->clear();

	if (m_pSession->soloTracks() > 0)
		m_statusItems[QTRACTOR_STATUS_SOLO]->setText(tr("SOLO"));
	else
		m_statusItems[QTRACTOR_STATUS_SOLO]->clear();

	if (m_pSession->isLooping())
		m_statusItems[QTRACTOR_STATUS_LOOP]->setText(tr("LOOP"));
	else
		m_statusItems[QTRACTOR_STATUS_LOOP]->clear();

	m_statusItems[QTRACTOR_STATUS_TIME]->setText(
		m_pSession->timeFromFrame(iSessionLength));

	m_statusItems[QTRACTOR_STATUS_RATE]->setText(
		tr("%1 Hz").arg(m_pSession->sampleRate()));

	const QColor& backColor = statusBar()->paletteBackgroundColor();
	m_statusItems[QTRACTOR_STATUS_REC]->setPaletteBackgroundColor(
		transportRecordAction->isOn() ? Qt::red : backColor);
	m_statusItems[QTRACTOR_STATUS_MUTE]->setPaletteBackgroundColor(
		m_pSession->muteTracks() > 0 ? Qt::yellow : backColor);
	m_statusItems[QTRACTOR_STATUS_SOLO]->setPaletteBackgroundColor(
		m_pSession->soloTracks() > 0 ? Qt::cyan : backColor);
	m_statusItems[QTRACTOR_STATUS_LOOP]->setPaletteBackgroundColor(
		m_pSession->isLooping() ? Qt::green : backColor);

	// Transport stuff...	
	m_pTransportTime->setPaletteForegroundColor(
		m_pSession->isActivated() ? Qt::green : Qt::darkGreen);
	transportRewindAction->setEnabled(m_iPlayHead > 0);
	transportBackwardAction->setEnabled(m_iPlayHead > 0);
	transportFastForwardAction->setEnabled(m_iPlayHead < iSessionLength);
	transportLoopAction->setEnabled(m_pSession->isLooping()
		|| m_pSession->editHead() < m_pSession->editTail());
	transportRecordAction->setEnabled(m_pSession->recordTracks() > 0);
}


// Actually start all session engines.
bool qtractorMainForm::startSession (void)
{
	m_iXrunCount = 0;
	m_iXrunSkip  = 0;
	m_iXrunTimer = 0;

	bool bResult = m_pSession->open(QTRACTOR_TITLE);
	if (bResult) {
		appendMessages(tr("Session started."));
	} else {
		appendMessagesError(
			tr("The audio engine could not be started.\n\n"
			"Make sure the JACK audio server (jackd)\n"
			"is up and running and then restart session."));
	}

	return bResult;
}


// Check and restart session, if applicable.
bool qtractorMainForm::checkRestartSession (void)
{
	// Whether session is currently activated,
	// try to (re)open the whole thing...
	if (!m_pSession->isActivated()) {
		// Save current playhead position, if any...
		unsigned long iPlayHead = m_pSession->playHead();
		// Bail out if can't start it...
		if (!startSession()) {
			// HACK: Auto-repeatable transport toolbar controls needs be up...
			QObjectList *pObjList = transportToolbar->queryList(
				"QToolButton", "^transport(Backward|Forward)Action");
			if (pObjList) {	// Iterate over the intended transport tool-buttons...	
				QObjectListIt it(*pObjList);
				for (QObject *pObj; (pObj = it.current()) != NULL; ++it) {
					QToolButton *pToolButton = static_cast<QToolButton *> (pObj);
					if (pToolButton)
						pToolButton->setDown(false);
				}
				delete pObjList;
			}
			// Can go on with no-business...
			transportPlayAction->setOn(false);
			stabilizeForm();
			return false;
		}
		// Restore previous playhead position...
		m_pSession->setPlayHead(iPlayHead);
	}

	return true;
}


// Grab and restore current sampler channels session.
void qtractorMainForm::updateSession (void)
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::updateSession()");
#endif

	// Stabilize session name.
	if (m_pSession->sessionName().isEmpty())
		m_pSession->setSessionName(QFileInfo(sessionName(m_sFilename)).baseName());

	// Initialize toolbar widgets...
	m_pTempoSpinBox->setValueFloat(m_pSession->tempo());

	// Time to create the main session track list view...
	if (m_pTracks == NULL) {
		// Create the main tracks widget...
		m_pTracks = new qtractorTracks(this, workspace());
		QObject::connect(m_pTracks, SIGNAL(selectionChangeSignal()),
			this, SLOT(selectionChanged()));
		QObject::connect(m_pTracks, SIGNAL(contentsChangeSignal()),
			this, SLOT(contentsChanged()));
		QObject::connect(m_pTracks, SIGNAL(closeNotifySignal()),
			this, SLOT(tracksClosed()));
		QObject::connect(m_pTracks->trackList(),
			SIGNAL(doubleClicked(QListViewItem*, const QPoint&, int)),
			this, SLOT(trackProperties()));
		QObject::connect(m_pTracks->trackList(), SIGNAL(selectionChanged()),
			this, SLOT(stabilizeForm()));
		//
		m_pTracks->showMaximized();
		// Log this rather important event...
		appendMessages(tr("Tracks open."));
	}

	// Remake loop state...
	transportLoopAction->setOn(m_pSession->isLooping());

	//  Actually (re)start session engines...
	if (startSession()) {
		// (Re)set playhead...
		m_pSession->setPlayHead(0);
		// (Re)initialize MIDI instrument patching...
		m_pSession->setMidiPatch(m_pInstruments);
	}

	// Update the session views...
	viewRefresh();
}


// Update the recent files list and menu.
void qtractorMainForm::updateRecentFiles ( const QString& sFilename )
{
	if (m_pOptions == NULL)
		return;

	// Remove from list if already there (avoid duplicates)
	QStringList::Iterator iter = m_pOptions->recentFiles.find(sFilename);
	if (iter != m_pOptions->recentFiles.end())
		m_pOptions->recentFiles.remove(iter);
	// Put it to front...
	m_pOptions->recentFiles.push_front(sFilename);

	// May update the menu.
	updateRecentFilesMenu();
}


// Update the recent files list and menu.
void qtractorMainForm::updateRecentFilesMenu (void)
{
	if (m_pOptions == NULL)
		return;

	// Time to keep the list under limits.
	int iRecentFiles = m_pOptions->recentFiles.count();
	while (iRecentFiles > m_pOptions->iMaxRecentFiles) {
		m_pOptions->recentFiles.pop_back();
		iRecentFiles--;
	}

	// rebuild the recent files menu...
	m_pRecentFilesMenu->clear();
	for (int i = 0; i < iRecentFiles; i++) {
		const QString& sFilename = m_pOptions->recentFiles[i];
		if (QFileInfo(sFilename).exists()) {
			m_pRecentFilesMenu->insertItem(QString("&%1 %2")
				.arg(i + 1).arg(sessionName(sFilename)),
				this, SLOT(fileOpenRecent(int)), 0, i);
		}
	}
}


// Force update of the peak-files auto-remove mode.
void qtractorMainForm::updatePeakAutoRemove (void)
{
	if (m_pOptions == NULL)
		return;

	qtractorAudioPeakFactory *pAudioPeakFactory
		= m_pSession->audioPeakFactory();
	if (pAudioPeakFactory)
		pAudioPeakFactory->setAutoRemove(m_pOptions->bPeakAutoRemove);	
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Messages window form handlers.

// Messages output methods.
void qtractorMainForm::appendMessages( const QString& s )
{
	if (m_pMessages)
		m_pMessages->appendMessages(s);

	statusBar()->message(s, 3000);
}

void qtractorMainForm::appendMessagesColor( const QString& s, const QString& c )
{
	if (m_pMessages)
		m_pMessages->appendMessagesColor(s, c);

	statusBar()->message(s, 3000);
}

void qtractorMainForm::appendMessagesText( const QString& s )
{
	if (m_pMessages)
		m_pMessages->appendMessagesText(s);
}

void qtractorMainForm::appendMessagesError( const QString& s )
{
	if (m_pMessages)
		m_pMessages->show();

	appendMessagesColor(s.simplifyWhiteSpace(), "#ff0000");

	QMessageBox::critical(this,
		tr("Error") + " - " QTRACTOR_TITLE, s, tr("Cancel"));
}


// Force update of the messages font.
void qtractorMainForm::updateMessagesFont (void)
{
	if (m_pOptions == NULL)
		return;

	if (m_pMessages && !m_pOptions->sMessagesFont.isEmpty()) {
		QFont font;
		if (font.fromString(m_pOptions->sMessagesFont))
			m_pMessages->setMessagesFont(font);
	}
}


// Update messages window line limit.
void qtractorMainForm::updateMessagesLimit (void)
{
	if (m_pOptions == NULL)
		return;

	if (m_pMessages) {
		if (m_pOptions->bMessagesLimit)
			m_pMessages->setMessagesLimit(m_pOptions->iMessagesLimitLines);
		else
			m_pMessages->setMessagesLimit(-1);
	}
}


// Enablement of the messages capture feature.
void qtractorMainForm::updateMessagesCapture (void)
{
	if (m_pOptions == NULL)
		return;

	if (m_pMessages)
		m_pMessages->setCaptureEnabled(m_pOptions->bStdoutCapture);
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Timer stuff.

// Timer slot funtion.
void qtractorMainForm::timerSlot (void)
{
	// Playhead and transport status...
	unsigned long iPlayHead = m_pSession->playHead();
	if (iPlayHead != m_iPlayHead) {
		if (m_pTracks && m_pTracks->trackView()) {
			m_pTracks->trackView()->setPlayHead(iPlayHead,
				transportFollowAction->isOn());
		}
		m_iPlayHead = iPlayHead;
	}
	// Check if its time to refresh playhead timer...
	if (m_pSession->isPlaying() &&
		m_iPlayTimer < QTRACTOR_TIMER_DELAY) {
		m_iPlayTimer += QTRACTOR_TIMER_MSECS;
		if (m_iPlayTimer >= QTRACTOR_TIMER_DELAY) {
			m_iPlayTimer = 0;
			updateTransportTime(m_iPlayHead);
		}
	}

	// Transport status...
	if (m_iTransport > 0) {
		m_iTransport = 0;
		if (m_pTracks)
			m_pTracks->trackView()->ensureVisibleFrame(m_iPlayHead);
		stabilizeForm();
	}

	// Check if its time to refresh some tracks...
	if (m_iPeakTimer > 0) {
		m_iPeakTimer -= QTRACTOR_TIMER_MSECS;
		if (m_iPeakTimer < QTRACTOR_TIMER_MSECS) {
			m_iPeakTimer = 0;
			if (m_pTracks && m_pTracks->trackView())
				m_pTracks->trackView()->updateContents();
		}
	}

	// Check if we've got some XRUN callbacks...
	if (m_iXrunTimer > 0) {
		m_iXrunTimer -= QTRACTOR_TIMER_MSECS;
		if (m_iXrunTimer < QTRACTOR_TIMER_MSECS) {
			m_iXrunTimer = 0;
			// Did we skip any?
			if (m_iXrunSkip > 0) {
				appendMessagesColor(
					tr("XRUN(%1 skipped)").arg(m_iXrunSkip), "#cc99cc");
				m_iXrunSkip = 0;
			}
			// Just post an informative message...
			appendMessagesColor(
				tr("XRUN(%1): some frames might have been lost.")
				.arg(m_iXrunCount), "#cc0033");
		}
	}

	// Register the next timer slot.
	QTimer::singleShot(QTRACTOR_TIMER_MSECS, this, SLOT(timerSlot()));
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Tracks stuff.

// Tracks view close slot funtion.
void qtractorMainForm::tracksClosed (void)
{
	// Log this simple event.
	appendMessages(tr("Tracks closed."));
	// Just reset the tracks handler, before something else does.
	m_pTracks = NULL;
}


//-------------------------------------------------------------------------
// qtractorMainForm -- General contents change stuff.

// Audio file addition slot funtion.
void qtractorMainForm::addAudioFile  ( const QString& sFilename )
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::addAudioFile(\"" + sFilename + "\")");
#endif

	// Add the just dropped audio file...
	if (m_pFiles)
		m_pFiles->addAudioFile(sFilename);

	stabilizeForm();
}


// Audio file activation slot funtion.
void qtractorMainForm::activateAudioFile  ( const QString& /* sFilename */ )
{
	//
	// TODO: Activate the just selected audio file...
	//

	stabilizeForm();
}


// MIDI file addition slot funtion.
void qtractorMainForm::addMidiFile  ( const QString& sFilename )
{
#ifdef CONFIG_DEBUG
	appendMessages("qtractorMainForm::addMidiFile(\"" + sFilename + "\")");
#endif

	// Add the just dropped MIDI file...
	if (m_pFiles)
		m_pFiles->addMidiFile(sFilename);

	stabilizeForm();
}


// MIDI file activation slot funtion.
void qtractorMainForm::activateMidiFile  ( const QString& /* sFilename */ )
{
	//
	// TODO: Activate the just selected MIDI file...
	//

	stabilizeForm();
}


// Tracks view selection change slot.
void qtractorMainForm::selectionChanged (void)
{
#ifdef CONFIG_DEBUG_0
	appendMessages("qtractorMainForm::selectionChanged()");
#endif

	stabilizeForm();
}


// Tracks view contents change slot.
void qtractorMainForm::contentsChanged (void)
{
#ifdef CONFIG_DEBUG_0
	appendMessages("qtractorMainForm::contentsChanged()");
#endif

	// Stabilize session toolbar widgets...
	m_pTempoSpinBox->setValueFloat(m_pSession->tempo());

	m_iDirtyCount++;
	stabilizeForm();
}


// Tempo spin-box change slot.
void qtractorMainForm::tempoChanged (void)
{
#ifdef CONFIG_DEBUG_0
	appendMessages("qtractorMainForm::tempoChanged()");
#endif

	// Get a copy of current session properties...
	qtractorSession::Properties props(m_pSession->properties());
	// Set the new property locally...
	props.tempo = m_pTempoSpinBox->valueFloat();
	// Now, express the change as a undoable command...
	m_pCommands->exec(
		new qtractorPropertyCommand<qtractorSession::Properties> (this,
			tr("session tempo"), m_pSession->properties(), props));
}


// end of qtractorMainForm.ui.h

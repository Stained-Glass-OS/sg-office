/*
 * SG Office -- the program: its windows, one per document, in one process.
 *
 * A second start of SG Office (a file double-clicked in File Explorer, Start's
 * SG Office Documents) hands its files to the one already running, over a
 * socket in the session's runtime directory, and leaves: a file that is open
 * already has its window brought forward instead of a second copy. The
 * editors' File menu (New, Open, Open Recent, Close) works on the same
 * windows, and the recent files are kept here.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include "formats.h"

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

class EditorWindow;
class QLocalServer;
class QWidget;

class Office : public QObject
{
	Q_OBJECT
public:
	static Office& instance();

	// Before the QApplication: hand files (absolute paths) or a new document
	// ("documents"...) to the SG Office already running. True: it took them.
	static bool handOff(const QStringList& files, const QString& newKind);
	// The socket a running SG Office listens on ("" when the session has no
	// runtime directory: every start is then on its own)
	static QString socketPath();
	// Listen for later starts' files.
	void listen();
	// The taskbar's icons for SG Office's windows: under Wine's profile
	// (%LOCALAPPDATA%\Stained Glass\Linux app icons\<window class>.ico, the
	// folder the taskbar reads a Linux program's icon from)
	void installTaskbarIcons();

	// A window for this file (the one it is open in already, brought
	// forward); nullptr and a message when SG Office cannot open it.
	EditorWindow* open(const QString& path, bool autopilot = false);
	EditorWindow* create(Kind kind, bool autopilot = false);
	// File > Open: pick files, each opened as above
	void openDialog(QWidget* parent, Kind preferred);
	QList<EditorWindow*> windows() const;

	// The recent files, newest first: [{path, type (format code), id}]
	QJsonArray recents() const;
	void addRecent(const QString& path, int formatCode);

	// The session's light or dark look (Settings > Colors), and its changes
	bool dark() const { return m_dark; }

signals:
	void recentsChanged();
	void darkChanged(bool dark);

private:
	Office();
	void adopt(EditorWindow* w);
	void readLook();
	void accept();
	QString recentsFile() const;

	QList<QPointer<EditorWindow>> m_windows;
	QLocalServer* m_server = nullptr;
	bool m_dark = false;
};

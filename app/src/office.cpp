/*
 * SG Office -- the program: its windows, the hand-off from later starts,
 * the recent files and the session's look.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "office.h"
#include "appicon.h"
#include "document.h"
#include "filedialogs.h"
#include "window.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{
constexpr int kMaxRecents = 20;

void debugLog(const QString& line)
{
	if (!qEnvironmentVariableIsEmpty("SG_OFFICE_LOG"))
		std::fprintf(stderr, "sg-office: %s\n", qPrintable(line));
}

bool kindFromName(const QString& n, Kind* k)
{
	if (n == QLatin1String("documents") || n == QLatin1String("word"))
		*k = Kind::Word;
	else if (n == QLatin1String("spreadsheets") || n == QLatin1String("cell"))
		*k = Kind::Cell;
	else if (n == QLatin1String("presentations") || n == QLatin1String("slide"))
		*k = Kind::Slide;
	else
		return false;
	return true;
}

QString gtkSettingsFile()
{
	QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
	if (base.isEmpty())
		base = QDir::homePath() + QStringLiteral("/.config");
	return base + QStringLiteral("/gtk-3.0/settings.ini");
}
}

Office& Office::instance()
{
	static Office* office = new Office;
	return *office;
}

Office::Office()
{
	readLook();
	// Settings > Colors writes GTK's settings.ini (sg-settingsctl): follow it
	auto* watcher = new QFileSystemWatcher(this);
	const QString file = gtkSettingsFile();
	QDir().mkpath(QFileInfo(file).absolutePath());
	watcher->addPath(QFileInfo(file).absolutePath());
	if (QFile::exists(file))
		watcher->addPath(file);
	auto changed = [this, watcher, file] {
		if (QFile::exists(file) && !watcher->files().contains(file))
			watcher->addPath(file);
		const bool was = m_dark;
		readLook();
		if (was != m_dark)
			emit darkChanged(m_dark);
	};
	connect(watcher, &QFileSystemWatcher::fileChanged, this, changed);
	connect(watcher, &QFileSystemWatcher::directoryChanged, this, changed);
}

void Office::readLook()
{
	// Settings > Colors' light or dark: GTK's settings.ini, which
	// sg-settingsctl writes for the session's Linux programs
	m_dark = false;
#ifdef SG_MUTANT_NO_DARK
	return;
#endif
	QFile f(gtkSettingsFile());
	if (!f.open(QIODevice::ReadOnly))
		return;
	for (const QByteArray& raw : f.readAll().split('\n'))
	{
		const QByteArray line = raw.trimmed();
		if (line.startsWith("gtk-application-prefer-dark-theme"))
		{
			const QByteArray v = line.mid(line.indexOf('=') + 1).trimmed().toLower();
			m_dark = v == "true" || v == "1";
		}
	}
}

// ---- the hand-off ---------------------------------------------------------------------

QString Office::socketPath()
{
	const QByteArray rt = qgetenv("XDG_RUNTIME_DIR");
	if (rt.isEmpty() || !qEnvironmentVariableIsEmpty("SG_OFFICE_NO_HANDOFF"))
		return {};
	return QString::fromLocal8Bit(rt) + QStringLiteral("/sg-office.sock");
}

// Plain sockets: this runs before the QApplication exists (a start that hands
// off never needs the display).
bool Office::handOff(const QStringList& files, const QString& newKind)
{
	const QByteArray path = QFile::encodeName(socketPath());
	if (path.isEmpty() || path.size() >= static_cast<int>(sizeof(sockaddr_un::sun_path)))
		return false;
	const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return false;
	sockaddr_un addr{};
	addr.sun_family = AF_UNIX;
	std::memcpy(addr.sun_path, path.constData(), path.size());
	timeval tv{10, 0};
	::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
	{
		::close(fd);
		return false;
	}
	QByteArray msg;
	for (const QString& f : files)
		msg += "open\t" + f.toUtf8() + '\n';
	if (!newKind.isEmpty())
		msg += "new\t" + newKind.toUtf8() + '\n';
	msg += "end\n";
	bool ok = true;
	for (qsizetype off = 0; ok && off < msg.size();)
	{
		const ssize_t n = ::write(fd, msg.constData() + off, msg.size() - off);
		ok = n > 0;
		off += n > 0 ? n : 0;
	}
	char reply[8] = {};
	ok = ok && ::read(fd, reply, sizeof(reply) - 1) >= 2 && std::strncmp(reply, "ok", 2) == 0;
	::close(fd);
	return ok;
}

void Office::listen()
{
	const QString path = socketPath();
	if (path.isEmpty())
		return;
	m_server = new QLocalServer(this);
	m_server->setSocketOptions(QLocalServer::UserAccessOption);
	if (!m_server->listen(path))
	{
		// another SG Office's socket that no one answers (it crashed): ours now
		QLocalSocket probe;
		probe.connectToServer(path);
		if (probe.waitForConnected(500))
		{
			debugLog(QStringLiteral("another SG Office listens at ") + path);
			delete m_server;
			m_server = nullptr;
			return;
		}
		QLocalServer::removeServer(path);
		if (!m_server->listen(path))
		{
			debugLog(QStringLiteral("cannot listen at %1: %2").arg(path, m_server->errorString()));
			delete m_server;
			m_server = nullptr;
			return;
		}
	}
	debugLog(QStringLiteral("listening at ") + path);
	connect(m_server, &QLocalServer::newConnection, this, &Office::accept);
}

void Office::accept()
{
	while (QLocalSocket* s = m_server->nextPendingConnection())
	{
		auto* buf = new QByteArray;
		connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
		connect(s, &QObject::destroyed, s, [buf] { delete buf; });
		connect(s, &QLocalSocket::readyRead, this, [this, s, buf] {
			buf->append(s->readAll());
			if (!buf->endsWith("end\n"))
				return;
			QStringList files;
			QString newKind;
			for (const QByteArray& line : buf->split('\n'))
			{
				const int tab = line.indexOf('\t');
				if (tab < 0)
					continue;
				const QString value = QString::fromUtf8(line.mid(tab + 1));
				if (line.startsWith("open\t"))
					files << value;
				else if (line.startsWith("new\t"))
					newKind = value;
			}
			buf->clear();
			s->write("ok\n");
			s->flush();
			s->disconnectFromServer();
			// opened after the reply: the other start is not kept waiting
			QTimer::singleShot(0, this, [this, files, newKind] {
				debugLog(QStringLiteral("handed over: %1%2").arg(files.join(QLatin1Char(' ')),
				         newKind.isEmpty() ? QString() : QStringLiteral(" new ") + newKind));
				for (const QString& f : files)
					open(f);
				Kind k;
				if (!newKind.isEmpty() && kindFromName(newKind, &k))
					create(k);
			});
		});
	}
}

// ---- the taskbar's icons ----------------------------------------------------------------

void Office::installTaskbarIcons()
{
#ifdef SG_MUTANT_NO_TASKBAR_ICON
	return;
#endif
	// started from Wine (Start, File Explorer): its prefix, and the user's
	// profile in it, as Wine names it (the login)
	const QString prefix = qEnvironmentVariable("WINEPREFIX");
	const QString user = qEnvironmentVariable("USER");
	if (prefix.isEmpty() || user.isEmpty() || user.contains(QLatin1Char('/')))
		return;
	const QString profile = prefix + QStringLiteral("/drive_c/users/") + user;
	if (!QFileInfo(profile).isDir())
		return;
	const QString dir = profile + QStringLiteral("/AppData/Local/Stained Glass/Linux app icons");
	QDir().mkpath(dir);
	for (Kind k : {Kind::Word, Kind::Cell, Kind::Slide})
	{
		const QString file = dir + QLatin1Char('/') + Formats::programId(k) + QStringLiteral(".ico");
		const QByteArray ico = AppIcon::ico(k);
		QFile old(file);
		if (old.open(QIODevice::ReadOnly) && old.readAll() == ico)
			continue;
		old.close();
		QSaveFile f(file);
		if (f.open(QIODevice::WriteOnly))
		{
			f.write(ico);
			f.commit();
		}
	}
}

// ---- windows ----------------------------------------------------------------------------

QList<EditorWindow*> Office::windows() const
{
	QList<EditorWindow*> out;
	for (const QPointer<EditorWindow>& w : m_windows)
		if (w)
			out << w;
	return out;
}

void Office::adopt(EditorWindow* w)
{
	m_windows.removeAll(nullptr);
	m_windows << w;
	w->show();
}

EditorWindow* Office::open(const QString& file, bool autopilot)
{
	const QFileInfo fi(file);
	const QString real = fi.exists() ? fi.canonicalFilePath() : fi.absoluteFilePath();
	for (EditorWindow* w : windows())
		if (!w->document()->path().isEmpty() && w->document()->path() == real)
		{
			debugLog(QStringLiteral("already open: %1 (its window brought forward)").arg(real));
			w->bringForward();
			return w;
		}
	bool ok = false;
	const Kind kind = Formats::kindOfExt(fi.suffix(), &ok);
	if (!ok || !fi.isFile())
	{
		QMessageBox box(QMessageBox::Warning, QStringLiteral("SG Office"),
		                fi.exists() || !ok ? QStringLiteral("SG Office cannot open \"%1\".").arg(fi.fileName())
		                                   : QStringLiteral("\"%1\" cannot be found. It may have been moved or deleted.").arg(fi.fileName()),
		                QMessageBox::Ok);
		box.setWindowIcon(qApp->windowIcon());
		box.exec();
		return nullptr;
	}
	auto* w = new EditorWindow(new Document(file, kind), autopilot);
	adopt(w);
	debugLog(QStringLiteral("opened ") + real);
	return w;
}

EditorWindow* Office::create(Kind kind, bool autopilot)
{
	auto* w = new EditorWindow(new Document(QString(), kind), autopilot);
	adopt(w);
	debugLog(QStringLiteral("new %1").arg(Formats::kindName(kind)));
	return w;
}

void Office::openDialog(QWidget* parent, Kind preferred)
{
	static const QString filters[] = {
		QStringLiteral("Documents (*.docx *.docm *.dotx *.dotm *.doc *.dot *.odt *.ott *.rtf *.txt)"),
		QStringLiteral("Spreadsheets (*.xlsx *.xlsm *.xltx *.xltm *.xlsb *.xls *.xlt *.ods *.ots *.csv)"),
		QStringLiteral("Presentations (*.pptx *.pptm *.ppsx *.ppsm *.potx *.potm *.ppt *.pps *.pot *.odp *.otp)"),
	};
	const QString all = QStringLiteral("All SG Office files (*.docx *.docm *.dotx *.dotm *.doc *.dot *.odt *.ott *.rtf *.txt "
	                                   "*.xlsx *.xlsm *.xltx *.xltm *.xlsb *.xls *.xlt *.ods *.ots *.csv "
	                                   "*.pptx *.pptm *.ppsx *.ppsm *.potx *.potm *.ppt *.pps *.pot *.odp *.otp)");
	QString selected = filters[static_cast<int>(preferred)];
	const QStringList list = {all, filters[0], filters[1], filters[2]};
	const QStringList files = Dialogs::open(parent, QStringLiteral("Open"),
		QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), list.join(QStringLiteral(";;")), &selected, true);
	for (const QString& f : files)
		open(f);
}

// ---- recent files -------------------------------------------------------------------------

QString Office::recentsFile() const
{
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/sg-office/recent.json");
}

QJsonArray Office::recents() const
{
	QFile f(recentsFile());
	if (!f.open(QIODevice::ReadOnly))
		return {};
	QJsonArray out;
	int id = 0;
	for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).array())
	{
		QJsonObject o = v.toObject();
		if (o.value(QStringLiteral("path")).toString().isEmpty())
			continue;
		o.insert(QStringLiteral("id"), id++);
		out.append(o);
	}
	return out;
}

void Office::addRecent(const QString& path, int formatCode)
{
	if (path.isEmpty() || !formatCode)
		return;
	QJsonArray list;
	list.append(QJsonObject{{QStringLiteral("path"), path}, {QStringLiteral("type"), formatCode}});
	for (const QJsonValue& v : recents())
	{
		const QJsonObject o = v.toObject();
		if (o.value(QStringLiteral("path")).toString() != path && list.size() < kMaxRecents)
			list.append(QJsonObject{{QStringLiteral("path"), o.value(QStringLiteral("path"))},
			                        {QStringLiteral("type"), o.value(QStringLiteral("type"))}});
	}
	QDir().mkpath(QFileInfo(recentsFile()).absolutePath());
	QSaveFile f(recentsFile());
	if (f.open(QIODevice::WriteOnly))
	{
		f.write(QJsonDocument(list).toJson());
		f.commit();
	}
	emit recentsChanged();
}

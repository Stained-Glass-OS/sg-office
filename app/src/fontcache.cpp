/*
 * SG Office -- the per-user font tables the editors and the converter use.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "fontcache.h"
#include "appicon.h"
#include "paths.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QWidget>

FontCache& FontCache::instance()
{
	static FontCache cache;
	return cache;
}

QString FontCache::dir() const
{
	return Paths::fontCacheDir();
}

QString FontCache::allFontsJs() const
{
	return dir() + QStringLiteral("/AllFonts.js");
}

QByteArray FontCache::fingerprint() const
{
	QCryptographicHash h(QCryptographicHash::Sha256);
	const QFileInfo gen(Paths::engineDir() + QStringLiteral("/allfontsgen"));
	h.addData(QStringLiteral("allfontsgen %1 %2\n").arg(gen.size()).arg(gen.lastModified().toMSecsSinceEpoch()).toUtf8());
	// fontconfig's own list: it notices a font added to or removed from any
	// of its folders (it checks their times), and costs milliseconds
	QProcess fc;
	fc.start(QStringLiteral("fc-list"), {QStringLiteral("--format"), QStringLiteral("%{file}\n")});
	QStringList files;
	if (fc.waitForFinished(20000) && fc.exitStatus() == QProcess::NormalExit && fc.exitCode() == 0)
		files = QString::fromLocal8Bit(fc.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	else
		return {};                                   // unknown: always generate
	files.sort();
	files.removeDuplicates();
	for (const QString& f : std::as_const(files))
	{
		const QFileInfo fi(f);
		h.addData(QStringLiteral("%1 %2 %3\n").arg(f).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).toUtf8());
	}
	return h.result().toHex();
}

bool FontCache::ensure(QString* error)
{
	QDir().mkpath(dir());
	const QString stampFile = dir() + QStringLiteral("/fonts.stamp");
	const QByteArray stamp = fingerprint();
	m_generated = false;
	{
		QFile f(stampFile);
#ifndef SG_MUTANT_FONTS_EVERY_START
		if (!stamp.isEmpty() && f.open(QIODevice::ReadOnly) && f.readAll().trimmed() == stamp
		    && QFile::exists(allFontsJs()) && QFile::exists(dir() + QStringLiteral("/font_selection.bin"))
		    && QFile::exists(dir() + QStringLiteral("/fonts_thumbnail.png")))
		{
			m_loaded = false;
			return true;
		}
#endif
	}
	QFile::remove(stampFile);

	// half a minute on a slow machine: say so, in a window of our own
	QWidget* note = nullptr;
	if (qobject_cast<QApplication*>(QCoreApplication::instance()) && qEnvironmentVariableIsEmpty("SG_OFFICE_NO_SPLASH"))
	{
		note = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
		note->setWindowTitle(QStringLiteral("SG Office"));
		note->setAttribute(Qt::WA_DeleteOnClose, false);
		auto* lay = new QHBoxLayout(note);
		lay->setContentsMargins(22, 18, 26, 18);
		lay->setSpacing(14);
		auto* pic = new QLabel(note);
		pic->setPixmap(AppIcon::of(Kind::Word).pixmap(40, 40));
		auto* text = new QLabel(QStringLiteral("<b>SG Office is getting ready</b><br>"
		                                      "Listing this computer's fonts; this happens once, "
		                                      "and again only when fonts change."), note);
		text->setWordWrap(true);
		text->setMinimumWidth(320);
		lay->addWidget(pic);
		lay->addWidget(text, 1);
		note->setStyleSheet(QStringLiteral("QWidget { background: palette(window); }"));
		note->adjustSize();
		note->show();
		QCoreApplication::processEvents();
	}

	QProcess p;
	p.setWorkingDirectory(Paths::engineDir());
	p.setProgram(Paths::engineDir() + QStringLiteral("/allfontsgen"));
	p.setArguments({QStringLiteral("--use-system=1"),
	                QStringLiteral("--allfonts=") + allFontsJs(),
	                QStringLiteral("--selection=") + dir() + QStringLiteral("/font_selection.bin"),
	                QStringLiteral("--images=") + dir()});
	p.setProcessChannelMode(QProcess::MergedChannels);
	QEventLoop loop;
	QObject::connect(&p, &QProcess::finished, &loop, &QEventLoop::quit);
	QObject::connect(&p, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
	QTimer::singleShot(300000, &loop, &QEventLoop::quit);
	p.start();
	if (p.state() != QProcess::NotRunning)
		loop.exec();
	delete note;
	if (p.state() != QProcess::NotRunning)
	{
		p.kill();
		p.waitForFinished(2000);
	}
	m_generated = true;
	if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0 || !QFile::exists(allFontsJs()))
	{
		if (error)
			*error = QStringLiteral("allfontsgen failed: ") + QString::fromLocal8Bit(p.readAll()).right(400);
		return false;
	}
	if (!stamp.isEmpty())
	{
		QSaveFile f(stampFile);
		if (f.open(QIODevice::WriteOnly))
		{
			f.write(stamp + '\n');
			f.commit();
		}
	}
	m_loaded = false;
	return true;
}

void FontCache::loadList()
{
	m_fonts.clear();
	QFile f(allFontsJs());
	if (f.open(QIODevice::ReadOnly))
	{
		const QString js = QString::fromUtf8(f.readAll());
		const int start = js.indexOf(QStringLiteral("window[\"__fonts_files\"]"));
		const int end = js.indexOf(QStringLiteral("];"), start);
		if (start >= 0 && end > start)
		{
			static const QRegularExpression rx(QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\""));
			auto it = rx.globalMatch(js.mid(start, end - start));
			it.next();       // the key itself
			while (it.hasNext())
				m_fonts.insert(it.next().captured(1).replace(QStringLiteral("\\\\"), QStringLiteral("\\")));
		}
	}
	m_loaded = true;
}

bool FontCache::isKnownFont(const QString& path)
{
	if (!m_loaded)
		loadList();
	return m_fonts.contains(path);
}

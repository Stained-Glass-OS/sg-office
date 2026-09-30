/*
 * SG Office -- the per-user font tables the editors and the converter use.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "fontcache.h"
#include "paths.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>

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

bool FontCache::ensure(QString* error)
{
	QDir().mkpath(dir());
	QProcess p;
	p.setWorkingDirectory(Paths::engineDir());
	p.setProgram(Paths::engineDir() + QStringLiteral("/allfontsgen"));
	p.setArguments({QStringLiteral("--use-system=1"),
	                QStringLiteral("--allfonts=") + allFontsJs(),
	                QStringLiteral("--selection=") + dir() + QStringLiteral("/font_selection.bin"),
	                QStringLiteral("--images=") + dir()});
	p.setProcessChannelMode(QProcess::MergedChannels);
	p.start();
	if (!p.waitForFinished(180000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0
	    || !QFile::exists(allFontsJs()))
	{
		if (error)
			*error = QStringLiteral("allfontsgen failed: ") + QString::fromLocal8Bit(p.readAll()).right(400);
		return false;
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

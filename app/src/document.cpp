/*
 * SG Office -- one open document.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "document.h"
#include "fontcache.h"
#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QUuid>

namespace
{
QHash<QString, Document*>& registry()
{
	static QHash<QString, Document*> docs;
	return docs;
}

QString xml(const QString& s)
{
	return s.toHtmlEscaped();
}

int nextUntitled(Kind kind)
{
	static int counters[3] = {0, 0, 0};
	return ++counters[static_cast<int>(kind)];
}

QString templateFor(Kind kind)
{
	// the new-document templates, from ONLYOFFICE's document-templates (Apache-2.0)
	static const char* names[] = {"new.docx", "new.xlsx", "new.pptx"};
	return Paths::engineDir() + QStringLiteral("/empty/") + QLatin1String(names[static_cast<int>(kind)]);
}
}

Document::Document(const QString& sourcePath, Kind kind, QObject* parent)
	: QObject(parent), m_kind(kind)
{
	m_id = QUuid::createUuid().toString(QUuid::Id128);
	m_workDir = Paths::documentsDir() + QLatin1Char('/') + m_id;
	QDir().mkpath(m_workDir + QStringLiteral("/changes"));
	if (!sourcePath.isEmpty())
	{
		// the real path: a file opened from the Windows side arrives through
		// the prefix's drive links (.../dosdevices/c:/users/...)
		const QFileInfo fi(sourcePath);
		m_path = fi.exists() ? fi.canonicalFilePath() : fi.absoluteFilePath();
		m_format = Formats::byExt(QFileInfo(sourcePath).suffix());
	}
	else
		m_untitledNumber = nextUntitled(kind);
	registry().insert(m_id, this);
}

Document::~Document()
{
	registry().remove(m_id);
	QDir(m_workDir).removeRecursively();
}

Document* Document::byId(const QString& id)
{
	return registry().value(id, nullptr);
}

QString Document::title() const
{
	if (!m_path.isEmpty())
		return QFileInfo(m_path).fileName();
	static const char* base[] = {"Document", "Book", "Presentation"};
	return QLatin1String(base[static_cast<int>(m_kind)]) + QString::number(m_untitledNumber);
}

void Document::setModified(bool modified)
{
	if (m_modified == modified)
		return;
	m_modified = modified;
	emit modifiedChanged(modified);
}

void Document::open(std::function<void(bool, const QString&)> done)
{
	const QString from = m_path.isEmpty() ? templateFor(m_kind) : m_path;
	const QString params = QStringLiteral(
		"<?xml version=\"1.0\" encoding=\"utf-8\"?><TaskQueueDataConvert>"
		"<m_sFileFrom>%1</m_sFileFrom><m_sFileTo>%2/Editor.bin</m_sFileTo>"
		"<m_nFormatTo>8192</m_nFormatTo><m_sThemeDir>%3</m_sThemeDir>"
		"<m_bDontSaveAdditional>true</m_bDontSaveAdditional>"
		"<m_sFontDir>%4</m_sFontDir><m_sTempDir>%2/tmp-open</m_sTempDir>"
		"</TaskQueueDataConvert>")
		.arg(xml(from), xml(m_workDir), xml(Paths::shareDir() + QStringLiteral("/sdkjs/slide/themes")),
		     xml(FontCache::instance().dir()));
	QDir().mkpath(m_workDir + QStringLiteral("/tmp-open"));
	runX2t(params, [this, done](int code, const QString& log) {
		QDir(m_workDir + QStringLiteral("/tmp-open")).removeRecursively();
		const bool ok = code == 0 && QFile::exists(m_workDir + QStringLiteral("/Editor.bin"));
		done(ok, ok ? QString() : QStringLiteral("x2t could not open the file (%1): %2").arg(code).arg(log.right(300)));
	});
}

void Document::addChanges(const QByteArrayList& entries, int deleteIndex)
{
	if (deleteIndex >= 0 && deleteIndex < m_changes.size())
		m_changes.erase(m_changes.begin() + deleteIndex, m_changes.end());
	m_changes.append(entries);
	writeChanges();
}

void Document::writeChanges()
{
	// x2t reads changes/changes0.json as "entry","entry",... -- the form the
	// ONLYOFFICE desktop app writes -- and wraps it in [ ] itself
	const QString file = m_workDir + QStringLiteral("/changes/changes0.json");
	if (m_changes.isEmpty())
	{
		QFile::remove(file);
		return;
	}
	QSaveFile f(file);
	if (!f.open(QIODevice::WriteOnly))
		return;
	for (const QByteArray& c : std::as_const(m_changes))
	{
		f.write("\"");
		f.write(c);
		f.write("\",");
	}
	f.commit();
}

void Document::save(const QString& target, int formatCode, const QByteArray& jsonParams,
                    std::function<void(bool, const QString&)> done)
{
	const Format* fmt = formatCode ? Formats::byCode(formatCode) : m_format;
	if (formatCode == 513)
	{
		static const Format pdf{QStringLiteral("pdf"), QStringLiteral("PDF"), 513, m_kind, true};
		fmt = &pdf;
	}
	if (!fmt || !fmt->writable)
	{
		done(false, QStringLiteral("SG Office cannot write this format"));
		return;
	}
	const QString absTarget = QFileInfo(target).absoluteFilePath();
	// written beside the target, renamed over it when complete
	const QString partial = absTarget + QStringLiteral(".sgoffice-partial.") + fmt->ext;
	const QString tmp = m_workDir + QStringLiteral("/tmp-save");
	QDir(tmp).removeRecursively();
	QDir().mkpath(tmp);
	const bool fromChanges = QFile::exists(m_workDir + QStringLiteral("/changes/changes0.json"));
	const QByteArray json = jsonParams.isEmpty() ? QByteArrayLiteral("{\"spreadsheetLayout\":{\"fitToWidth\":1,\"fitToHeight\":1}}") : jsonParams;
	const QString params = QStringLiteral(
		"<?xml version=\"1.0\" encoding=\"utf-8\"?><TaskQueueDataConvert>"
		"<m_sFileFrom>%1/Editor.bin</m_sFileFrom><m_sFileTo>%2</m_sFileTo>"
		"<m_nFormatTo>%3</m_nFormatTo><m_sThemeDir>%4</m_sThemeDir>"
		"<m_bFromChanges>%5</m_bFromChanges><m_bDontSaveAdditional>true</m_bDontSaveAdditional>"
		"<m_sAllFontsPath>%6/AllFonts.js</m_sAllFontsPath>"
		"<m_nCsvTxtEncoding>46</m_nCsvTxtEncoding><m_nCsvDelimiter>4</m_nCsvDelimiter>"
		"<m_sFontDir>%6</m_sFontDir><m_sJsonParams>%7</m_sJsonParams>"
		"<m_nDoctParams>1</m_nDoctParams><m_sTempDir>%8</m_sTempDir>"
		"</TaskQueueDataConvert>")
		.arg(xml(m_workDir), xml(partial), QString::number(fmt->code),
		     xml(Paths::shareDir() + QStringLiteral("/sdkjs/slide/themes")),
		     fromChanges ? QStringLiteral("true") : QStringLiteral("false"),
		     xml(FontCache::instance().dir()), xml(QString::fromUtf8(json)), xml(tmp));
	QPointer<Document> self(this);
	runX2t(params, [self, absTarget, partial, fmt, done, tmp](int code, const QString& log) {
		QDir(tmp).removeRecursively();
		if (code != 0 || !QFileInfo(partial).isFile() || QFileInfo(partial).size() == 0)
		{
			QFile::remove(partial);
			done(false, QStringLiteral("x2t could not save (%1): %2").arg(code).arg(log.right(300)));
			return;
		}
		QFile::remove(absTarget);
		if (!QFile::rename(partial, absTarget))
		{
			done(false, QStringLiteral("could not replace %1").arg(absTarget));
			return;
		}
		if (self && fmt->code != 513)
		{
			const bool changed = self->m_path != absTarget || self->m_format != fmt;
			self->m_path = absTarget;
			self->m_format = Formats::byCode(fmt->code);
			if (changed)
				emit self->identityChanged();
		}
		done(true, QString());
	});
}

void Document::runX2t(const QString& paramsXml, std::function<void(int, const QString&)> done)
{
	static int serial = 0;
	const QString paramsFile = m_workDir + QStringLiteral("/params-%1.xml").arg(++serial);
	{
		QFile f(paramsFile);
		if (!f.open(QIODevice::WriteOnly))
		{
			done(-1, QStringLiteral("cannot write ") + paramsFile);
			return;
		}
		f.write(paramsXml.toUtf8());
	}
	auto* p = new QProcess(this);
	p->setWorkingDirectory(Paths::engineDir());
	p->setProgram(Paths::engineDir() + QStringLiteral("/x2t"));
	p->setArguments({paramsFile});
	p->setProcessChannelMode(QProcess::MergedChannels);
	connect(p, &QProcess::finished, this, [p, done, paramsFile](int exitCode, QProcess::ExitStatus st) {
		const QString log = QString::fromLocal8Bit(p->readAll());
		QFile::remove(paramsFile);
		p->deleteLater();
		done(st == QProcess::NormalExit ? exitCode : -2, log);
	});
	connect(p, &QProcess::errorOccurred, this, [p, done, paramsFile](QProcess::ProcessError e) {
		if (e != QProcess::FailedToStart)
			return;
		QFile::remove(paramsFile);
		p->deleteLater();
		done(-3, QStringLiteral("x2t failed to start"));
	});
	p->start();
}

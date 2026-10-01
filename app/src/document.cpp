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

// A text file's encoding, as the engine numbers them
// (core/UnicodeConverter/UnicodeConverter_Encodings.h): UTF-8 when it is
// valid UTF-8 (or says so), UTF-16 by its byte-order mark, else Windows-1252
// -- what Office assumes for a CSV without a mark on a Western system.
int textEncoding(const QByteArray& head)
{
	if (head.startsWith("\xEF\xBB\xBF"))
		return 46;
	if (head.startsWith("\xFF\xFE"))
		return 48;
	if (head.startsWith("\xFE\xFF"))
		return 49;
	// valid UTF-8 (a sequence cut at the end of the sample does not count)
	int i = 0;
	const int n = head.size();
	while (i < n)
	{
		const unsigned char c = static_cast<unsigned char>(head[i]);
		int more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
		if (more < 0)
			return 44;
		if (i + more >= n)
			break;
		for (int k = 1; k <= more; ++k)
			if ((static_cast<unsigned char>(head[i + k]) & 0xC0) != 0x80)
				return 44;
		i += more + 1;
	}
	return 46;
}

// A CSV's delimiter, as the engine numbers them (1 tab, 2 semicolon, 4
// comma): the one its first lines use most, outside quotes -- semicolons
// are what a CSV from a comma-decimal locale has.
int csvDelimiter(const QByteArray& head)
{
	int counts[3] = {0, 0, 0};          // tab, semicolon, comma
	bool quoted = false;
	int lines = 0;
	for (char c : head)
	{
		if (c == '"')
			quoted = !quoted;
		else if (quoted)
			continue;
		else if (c == '\n' && ++lines >= 20)
			break;
		else if (c == '\t')
			++counts[0];
		else if (c == ';')
			++counts[1];
		else if (c == ',')
			++counts[2];
	}
	if (counts[0] > counts[2] && counts[0] >= counts[1])
		return 1;
	if (counts[1] > counts[2])
		return 2;
	return 4;
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
	// an empty file (File Explorer's New > Document makes one) opens as a new
	// document of its kind, saved back to that file
	const bool blank = m_path.isEmpty() || QFileInfo(m_path).size() == 0;
	const QString from = blank ? templateFor(m_kind) : m_path;
	// a CSV or plain text file: the engine needs its encoding (and a CSV's
	// delimiter) said, or it refuses to open it
	QString text;
	const QString ext = QFileInfo(from).suffix().toLower();
#ifndef SG_MUTANT_CSV_NO_PARAMS
	if (ext == QLatin1String("csv") || ext == QLatin1String("txt"))
	{
		QFile f(from);
		QByteArray head;
		if (f.open(QIODevice::ReadOnly))
			head = f.read(64 * 1024);
		m_textEncoding = textEncoding(head);
		text = QStringLiteral("<m_nCsvTxtEncoding>%1</m_nCsvTxtEncoding>").arg(m_textEncoding);
		if (ext == QLatin1String("csv"))
		{
			m_csvDelimiter = csvDelimiter(head);
			text += QStringLiteral("<m_nCsvDelimiter>%1</m_nCsvDelimiter>").arg(m_csvDelimiter);
		}
	}
#endif
	const QString params = QStringLiteral(
		"<?xml version=\"1.0\" encoding=\"utf-8\"?><TaskQueueDataConvert>"
		"<m_sFileFrom>%1</m_sFileFrom><m_sFileTo>%2/Editor.bin</m_sFileTo>"
		"<m_nFormatTo>8192</m_nFormatTo><m_sThemeDir>%3</m_sThemeDir>"
		"<m_bDontSaveAdditional>true</m_bDontSaveAdditional>%5"
		"<m_sFontDir>%4</m_sFontDir><m_sTempDir>%2/tmp-open</m_sTempDir>"
		"</TaskQueueDataConvert>")
		.arg(xml(from), xml(m_workDir), xml(Paths::shareDir() + QStringLiteral("/sdkjs/slide/themes")),
		     xml(FontCache::instance().dir()), text);
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
		"<m_nCsvTxtEncoding>%9</m_nCsvTxtEncoding><m_nCsvDelimiter>%10</m_nCsvDelimiter>"
		"<m_sFontDir>%6</m_sFontDir><m_sJsonParams>%7</m_sJsonParams>"
		"<m_nDoctParams>1</m_nDoctParams><m_sTempDir>%8</m_sTempDir>"
		"</TaskQueueDataConvert>")
		.arg(xml(m_workDir), xml(partial), QString::number(fmt->code),
		     xml(Paths::shareDir() + QStringLiteral("/sdkjs/slide/themes")),
		     fromChanges ? QStringLiteral("true") : QStringLiteral("false"),
		     xml(FontCache::instance().dir()), xml(QString::fromUtf8(json)), xml(tmp),
		     // a CSV or text file saved back as it was read: its encoding and delimiter
		     QString::number(fmt == m_format ? m_textEncoding : 46), QString::number(fmt == m_format ? m_csvDelimiter : 4));
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

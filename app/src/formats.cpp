/*
 * SG Office -- the file formats each editor opens and saves.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "formats.h"

namespace Formats
{
const QList<Format>& all()
{
	static const QList<Format> list = {
		// Documents: Word formats first -- .docx is the default
		{QStringLiteral("docx"), QStringLiteral("Word Document"), 65, Kind::Word, true},
		{QStringLiteral("docm"), QStringLiteral("Word Macro-Enabled Document"), 75, Kind::Word, true},
		{QStringLiteral("dotx"), QStringLiteral("Word Template"), 76, Kind::Word, true},
		{QStringLiteral("doc"), QStringLiteral("Word 97-2003 Document"), 66, Kind::Word, false},
		{QStringLiteral("odt"), QStringLiteral("OpenDocument Text"), 67, Kind::Word, true},
		{QStringLiteral("ott"), QStringLiteral("OpenDocument Text Template"), 79, Kind::Word, true},
		{QStringLiteral("rtf"), QStringLiteral("Rich Text Format"), 68, Kind::Word, true},
		{QStringLiteral("txt"), QStringLiteral("Plain Text"), 69, Kind::Word, true},
		{QStringLiteral("pdf"), QStringLiteral("PDF"), 513, Kind::Word, true},
		// Spreadsheets
		{QStringLiteral("xlsx"), QStringLiteral("Excel Workbook"), 257, Kind::Cell, true},
		{QStringLiteral("xlsm"), QStringLiteral("Excel Macro-Enabled Workbook"), 261, Kind::Cell, true},
		{QStringLiteral("xltx"), QStringLiteral("Excel Template"), 262, Kind::Cell, true},
		{QStringLiteral("xls"), QStringLiteral("Excel 97-2003 Workbook"), 258, Kind::Cell, false},
		{QStringLiteral("ods"), QStringLiteral("OpenDocument Spreadsheet"), 259, Kind::Cell, true},
		{QStringLiteral("ots"), QStringLiteral("OpenDocument Spreadsheet Template"), 266, Kind::Cell, true},
		{QStringLiteral("csv"), QStringLiteral("CSV (Comma delimited)"), 260, Kind::Cell, true},
		{QStringLiteral("pdf"), QStringLiteral("PDF"), 513, Kind::Cell, true},
		// Presentations
		{QStringLiteral("pptx"), QStringLiteral("PowerPoint Presentation"), 129, Kind::Slide, true},
		{QStringLiteral("pptm"), QStringLiteral("PowerPoint Macro-Enabled Presentation"), 133, Kind::Slide, true},
		{QStringLiteral("ppsx"), QStringLiteral("PowerPoint Show"), 132, Kind::Slide, true},
		{QStringLiteral("potx"), QStringLiteral("PowerPoint Template"), 135, Kind::Slide, true},
		{QStringLiteral("ppt"), QStringLiteral("PowerPoint 97-2003 Presentation"), 130, Kind::Slide, false},
		{QStringLiteral("odp"), QStringLiteral("OpenDocument Presentation"), 131, Kind::Slide, true},
		{QStringLiteral("otp"), QStringLiteral("OpenDocument Presentation Template"), 138, Kind::Slide, true},
		{QStringLiteral("pdf"), QStringLiteral("PDF"), 513, Kind::Slide, true},
	};
	return list;
}

const Format* byExt(const QString& ext)
{
	const QString e = ext.toLower();
	for (const Format& f : all())
		if (f.ext == e && f.code != 513)
			return &f;
	return nullptr;
}

const Format* byCode(int code)
{
	for (const Format& f : all())
		if (f.code == code)
			return &f;
	return nullptr;
}

QList<const Format*> saveChoices(Kind kind)
{
	QList<const Format*> out;
	for (const Format& f : all())
		if (f.kind == kind && f.writable)
			out.append(&f);
	return out;
}

int canvasCode(Kind kind)
{
	switch (kind)
	{
	case Kind::Word: return 8193;
	case Kind::Cell: return 8194;
	case Kind::Slide: return 8195;
	}
	return 8192;
}

QString kindName(Kind kind)
{
	switch (kind)
	{
	case Kind::Word: return QStringLiteral("word");
	case Kind::Cell: return QStringLiteral("cell");
	case Kind::Slide: return QStringLiteral("slide");
	}
	return {};
}

QString editorApp(Kind kind)
{
	switch (kind)
	{
	case Kind::Word: return QStringLiteral("documenteditor");
	case Kind::Cell: return QStringLiteral("spreadsheeteditor");
	case Kind::Slide: return QStringLiteral("presentationeditor");
	}
	return {};
}

QString productName(Kind kind)
{
	switch (kind)
	{
	case Kind::Word: return QStringLiteral("SG Office Documents");
	case Kind::Cell: return QStringLiteral("SG Office Spreadsheets");
	case Kind::Slide: return QStringLiteral("SG Office Presentations");
	}
	return QStringLiteral("SG Office");
}

Kind kindOfExt(const QString& ext, bool* ok)
{
	const Format* f = byExt(ext);
	if (ok)
		*ok = f != nullptr;
	return f ? f->kind : Kind::Word;
}
}

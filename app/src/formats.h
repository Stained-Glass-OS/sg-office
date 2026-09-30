/*
 * SG Office -- the file formats each editor opens and saves, and the engine's
 * codes for them (core/Common/OfficeFileFormats.h).
 *
 * Microsoft's formats are the defaults (a new document is .docx/.xlsx/.pptx;
 * Save keeps a .docx a .docx); OpenDocument and PDF are offered in Save As.
 * The binary Office formats (.doc, .xls, .ppt) open but are not written:
 * saving one asks where to save it, the Microsoft format of its kind first.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <QList>
#include <QString>

enum class Kind { Word, Cell, Slide };

struct Format
{
	QString ext;          // "docx"
	QString label;        // "Word Document"
	int code;             // engine code, e.g. 65
	Kind kind;
	bool writable;        // x2t can write it
};

namespace Formats
{
const QList<Format>& all();
const Format* byExt(const QString& ext);
const Format* byCode(int code);
// Save As choices for an editor, the default (MS) first.
QList<const Format*> saveChoices(Kind kind);
// The engine's code for the editor's own format (Editor.bin).
int canvasCode(Kind kind);
QString kindName(Kind kind);          // "word" / "cell" / "slide" -- the editors' documentType
QString editorApp(Kind kind);         // "documenteditor" ...
QString productName(Kind kind);       // "SG Office Documents" ...
Kind kindOfExt(const QString& ext, bool* ok = nullptr);
}

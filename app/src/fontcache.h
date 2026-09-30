/*
 * SG Office -- the per-user font tables the editors and the converter use.
 *
 * allfontsgen (our engine's) lists the system's and the user's fonts into
 * AllFonts.js and font_selection.bin, and draws the font menu's thumbnails;
 * it rebuilds only when the fonts changed (it keeps fonts.log), so it runs
 * at every start.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <QSet>
#include <QString>

class FontCache
{
public:
	static FontCache& instance();

	// Run allfontsgen (blocking; seconds on first use, fast afterwards).
	bool ensure(QString* error = nullptr);

	QString dir() const;                  // AllFonts.js, font_selection.bin, thumbnails
	QString allFontsJs() const;
	// Is this a font file the tables list? (The editors may only read those.)
	bool isKnownFont(const QString& path);

private:
	FontCache() = default;
	void loadList();
	QSet<QString> m_fonts;
	bool m_loaded = false;
};

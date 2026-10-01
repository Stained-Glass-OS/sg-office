/*
 * SG Office -- the per-user font tables the editors and the converter use.
 *
 * allfontsgen (our engine's) lists the system's and the user's fonts into
 * AllFonts.js and font_selection.bin, and draws the font menu's thumbnails --
 * half a minute on a slow machine, every time it runs. So it runs only when
 * the fonts changed: fontconfig's list of font files (with each file's size
 * and time) and allfontsgen itself are fingerprinted into fonts.stamp, and an
 * unchanged fingerprint with the tables in place skips it. While it runs, a
 * small window says SG Office is getting ready.
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

	// The tables, up to date: allfontsgen when the fonts changed (a window
	// says so meanwhile), nothing otherwise. Needs the QApplication.
	bool ensure(QString* error = nullptr);
	// Did the last ensure() run allfontsgen? (The gate's question.)
	bool generated() const { return m_generated; }
	// fontconfig's font files (and allfontsgen) as one fingerprint
	QByteArray fingerprint() const;

	QString dir() const;                  // AllFonts.js, font_selection.bin, thumbnails
	QString allFontsJs() const;
	// Is this a font file the tables list? (The editors may only read those.)
	bool isKnownFont(const QString& path);

private:
	FontCache() = default;
	void loadList();
	QSet<QString> m_fonts;
	bool m_loaded = false;
	bool m_generated = false;
};

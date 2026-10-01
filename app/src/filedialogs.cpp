/*
 * SG Office -- its Open and Save As dialogs.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "filedialogs.h"

#include <QDir>
#include <QFileDialog>
#include <QList>
#include <QStandardPaths>
#include <QUrl>
#include <QFileInfo>
#include <cstdio>

namespace
{
void prepare(QFileDialog& d, const QString& filters, const QString* selected)
{
	QList<QUrl> places;
	for (auto loc : {QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation, QStandardPaths::DownloadLocation,
	                 QStandardPaths::PicturesLocation, QStandardPaths::MusicLocation, QStandardPaths::MoviesLocation})
	{
		const QString p = QStandardPaths::writableLocation(loc);
		if (!p.isEmpty() && p != QDir::homePath() && QDir(p).exists())
			places << QUrl::fromLocalFile(p);
	}
	places.prepend(QUrl::fromLocalFile(QDir::homePath()));
#ifndef SG_MUTANT_NO_PLACES
	d.setSidebarUrls(places);
#endif
	d.setViewMode(QFileDialog::Detail);
	d.setNameFilters(filters.split(QStringLiteral(";;"), Qt::SkipEmptyParts));
	if (selected && !selected->isEmpty())
		d.selectNameFilter(*selected);
	// the gate's look at the places list
	if (!qEnvironmentVariableIsEmpty("SG_OFFICE_LOG"))
	{
		QStringList names;
		for (const QUrl& u : d.sidebarUrls())
			names << QFileInfo(u.toLocalFile()).fileName();
		std::fprintf(stderr, "sg-office: dialog places %s\n", qPrintable(names.join(QLatin1Char('|'))));
	}
}
}

namespace Dialogs
{
QStringList open(QWidget* parent, const QString& title, const QString& dir, const QString& filters,
                 QString* selectedFilter, bool multi)
{
	QFileDialog d(parent, title, dir);
	d.setAcceptMode(QFileDialog::AcceptOpen);
	d.setFileMode(multi ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
	prepare(d, filters, selectedFilter);
	if (d.exec() != QDialog::Accepted)
		return {};
	if (selectedFilter)
		*selectedFilter = d.selectedNameFilter();
	return d.selectedFiles();
}

QString save(QWidget* parent, const QString& title, const QString& path, const QString& filters,
             QString* selectedFilter)
{
	QFileDialog d(parent, title, QFileInfo(path).absolutePath());
	d.setAcceptMode(QFileDialog::AcceptSave);
	d.setFileMode(QFileDialog::AnyFile);
	d.selectFile(QFileInfo(path).fileName());
	prepare(d, filters, selectedFilter);
	if (d.exec() != QDialog::Accepted || d.selectedFiles().isEmpty())
		return {};
	if (selectedFilter)
		*selectedFilter = d.selectedNameFilter();
	return d.selectedFiles().first();
}
}

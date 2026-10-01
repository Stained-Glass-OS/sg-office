/*
 * SG Office -- printing: the editor's File > Print hands the program its
 * choices (pages, copies, sides; or Quick Print), the program makes a PDF of
 * the document (x2t) and sends it to a printer through CUPS (lp), the one
 * picked in the print dialog -- or the default one, for Quick Print. "Print
 * to File" in the dialog writes the PDF where the person says.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

class QWidget;

namespace Printing
{
struct Job
{
	QString printer;          // CUPS destination; "" = the default
	QString toFile;           // or: the PDF, written here ("Print to File")
	int copies = 1;
	QString pages;            // "" = all, else CUPS page-ranges ("2", "1-3,5")
	QString sides;            // "" or one-sided / two-sided-long-edge / two-sided-short-edge
	QString title;
};

// The editor's choices (its nativeOptions) as a job, before the dialog
Job fromEditor(const QJsonObject& nativeOptions, const QString& title);
// The print dialog, filled in from the job; false when cancelled.
// SG_OFFICE_PRINTER=NAME answers it (the gates): no dialog.
bool ask(QWidget* parent, Job* job);
// lp's arguments for the job (SG_OFFICE_LP names another program: the gate's)
QStringList lpArguments(const Job& job, const QString& pdf);
// Send the PDF: lp, or a copy for Print to File. done(ok, error) when lp has it.
void submit(const Job& job, const QString& pdf, std::function<void(bool ok, const QString& error)> done);
}

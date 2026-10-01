/*
 * SG Office -- printing through CUPS.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "printing.h"

#include <QFile>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QRegularExpression>
#include <cstdio>

namespace Printing
{
Job fromEditor(const QJsonObject& o, const QString& title)
{
	Job job;
	job.title = title;
	job.copies = qMax(1, o.value(QStringLiteral("copies")).toInt(1));
	const QJsonValue pages = o.value(QStringLiteral("pages"));
	const QString p = pages.isString() ? pages.toString().trimmed() : QString();
	if (p == QLatin1String("current"))
		job.pages = QString::number(qMax(1, o.value(QStringLiteral("currentPage")).toInt(1)));
	else if (!p.isEmpty() && p != QLatin1String("all"))
	{
		// what the editor's Pages box takes: "1-3, 5" -- CUPS's own form, without spaces
		QString r = p;
		r.remove(QLatin1Char(' '));
		if (QRegularExpression(QStringLiteral("^\\d+(-\\d+)?(,\\d+(-\\d+)?)*$")).match(r).hasMatch())
			job.pages = r;
	}
	const QString sides = o.value(QStringLiteral("sides")).toString();
	job.sides = sides == QLatin1String("both-long") ? QStringLiteral("two-sided-long-edge")
	          : sides == QLatin1String("both-short") ? QStringLiteral("two-sided-short-edge")
	          : sides == QLatin1String("one") ? QStringLiteral("one-sided") : QString();
	return job;
}

bool ask(QWidget* parent, Job* job)
{
	const QString forced = qEnvironmentVariable("SG_OFFICE_PRINTER");
	if (!forced.isEmpty())
	{
		if (forced.startsWith(QLatin1String("file:")))
			job->toFile = forced.mid(5);
		else
			job->printer = forced;
		return true;
	}
	QPrinter printer(QPrinter::HighResolution);
	printer.setDocName(job->title);
	printer.setCopyCount(job->copies);
	printer.setDuplex(job->sides == QLatin1String("two-sided-long-edge") ? QPrinter::DuplexLongSide
	                  : job->sides == QLatin1String("two-sided-short-edge") ? QPrinter::DuplexShortSide
	                  : QPrinter::DuplexNone);
	QPrintDialog dlg(&printer, parent);
	dlg.setWindowTitle(QStringLiteral("Print"));
	dlg.setOptions(QAbstractPrintDialog::PrintToFile | QAbstractPrintDialog::PrintPageRange
	               | QAbstractPrintDialog::PrintCollateCopies);
	dlg.setMinMax(1, 9999);
	const QRegularExpressionMatch single = QRegularExpression(QStringLiteral("^(\\d+)(?:-(\\d+))?$")).match(job->pages);
	if (single.hasMatch())
	{
		dlg.setPrintRange(QAbstractPrintDialog::PageRange);
		dlg.setFromTo(single.captured(1).toInt(), single.captured(2).isEmpty() ? single.captured(1).toInt()
		                                                                        : single.captured(2).toInt());
	}
	if (dlg.exec() != QDialog::Accepted)
		return false;
	if (printer.outputFormat() == QPrinter::PdfFormat && !printer.outputFileName().isEmpty())
	{
		job->toFile = printer.outputFileName();
		return true;
	}
	job->printer = printer.printerName();
	job->copies = qMax(1, printer.copyCount());
	switch (printer.duplex())
	{
	case QPrinter::DuplexLongSide: job->sides = QStringLiteral("two-sided-long-edge"); break;
	case QPrinter::DuplexShortSide: job->sides = QStringLiteral("two-sided-short-edge"); break;
	case QPrinter::DuplexNone: job->sides = QStringLiteral("one-sided"); break;
	default: break;
	}
	// the dialog's range; but the editor's list of pages ("1-2,5"), which the
	// dialog cannot show, stands unless the person chose a range there
	if (dlg.printRange() == QAbstractPrintDialog::PageRange)
		job->pages = QStringLiteral("%1-%2").arg(dlg.fromPage()).arg(dlg.toPage());
	else if (single.hasMatch())
		job->pages.clear();
	return true;
}

QStringList lpArguments(const Job& job, const QString& pdf)
{
	QStringList args;
	if (!job.printer.isEmpty())
		args << QStringLiteral("-d") << job.printer;
	if (job.copies > 1)
		args << QStringLiteral("-n") << QString::number(job.copies);
	if (!job.pages.isEmpty())
		args << QStringLiteral("-o") << QStringLiteral("page-ranges=") + job.pages;
	if (!job.sides.isEmpty())
		args << QStringLiteral("-o") << QStringLiteral("sides=") + job.sides;
	if (!job.title.isEmpty())
		args << QStringLiteral("-t") << job.title;
	args << QStringLiteral("--") << pdf;
	return args;
}

void submit(const Job& job, const QString& pdf, std::function<void(bool, const QString&)> done)
{
	if (!job.toFile.isEmpty())
	{
		QFile::remove(job.toFile);
		const bool ok = QFile::copy(pdf, job.toFile);
		done(ok, ok ? QString() : QStringLiteral("could not write ") + job.toFile);
		return;
	}
	const QString lp = qEnvironmentVariable("SG_OFFICE_LP", QStringLiteral("lp"));
	auto* p = new QProcess;
	p->setProcessChannelMode(QProcess::MergedChannels);
	QObject::connect(p, &QProcess::finished, p, [p, done](int code, QProcess::ExitStatus st) {
		const QString out = QString::fromLocal8Bit(p->readAll()).trimmed();
		p->deleteLater();
		done(st == QProcess::NormalExit && code == 0, out);
	});
	QObject::connect(p, &QProcess::errorOccurred, p, [p, done](QProcess::ProcessError e) {
		if (e != QProcess::FailedToStart)
			return;
		p->deleteLater();
		done(false, QStringLiteral("the printing system (CUPS) is not installed"));
	});
	p->start(lp, lpArguments(job, pdf));
}
}

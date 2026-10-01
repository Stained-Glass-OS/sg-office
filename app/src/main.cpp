/*
 * SG Office -- Documents, Spreadsheets and Presentations: Stained Glass OS's
 * office suite, based on ONLYOFFICE (see NOTICE).
 *
 *   sg-office FILE...                    open files, each in its editor
 *   sg-office --new documents|spreadsheets|presentations
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "appicon.h"
#include "fontcache.h"
#include "formats.h"
#include "office.h"
#include "schemehandler.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPalette>
#include <QStyleFactory>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <cstdio>

namespace
{
void injectBridge(QWebEngineProfile* profile)
{
	// SG_OFFICE_BRIDGE: another bridge.js, for the tests' mutants
	const QString alt = qEnvironmentVariable("SG_OFFICE_BRIDGE");
	QFile f(alt.isEmpty() ? QStringLiteral(":/shell/bridge.js") : alt);
	f.open(QIODevice::ReadOnly);
	QWebEngineScript s;
	s.setName(QStringLiteral("sg-office-bridge"));
	s.setSourceCode(QString::fromUtf8(f.readAll()));
	s.setInjectionPoint(QWebEngineScript::DocumentCreation);
	s.setRunsOnSubFrames(true);                    // the editor runs in DocsAPI's frame
	s.setWorldId(QWebEngineScript::MainWorld);     // the editors look for it on window
	profile->scripts()->insert(s);
}

// SG Office's own dialogs (Save As, Open, Print, messages) in the session's
// light or dark look
void applyDialogLook(bool dark)
{
	static const QPalette light = QApplication::palette();
	if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
		QApplication::setStyle(fusion);
	if (!dark)
	{
		QApplication::setPalette(light);
		return;
	}
	QPalette p;
	const QColor window(0x2B, 0x2B, 0x2B), base(0x1E, 0x1E, 0x1E), text(0xF0, 0xF0, 0xF0), accent(0x8A, 0x4F, 0xD4);
	p.setColor(QPalette::Window, window);
	p.setColor(QPalette::WindowText, text);
	p.setColor(QPalette::Base, base);
	p.setColor(QPalette::AlternateBase, window);
	p.setColor(QPalette::ToolTipBase, window);
	p.setColor(QPalette::ToolTipText, text);
	p.setColor(QPalette::Text, text);
	p.setColor(QPalette::Button, QColor(0x33, 0x33, 0x33));
	p.setColor(QPalette::ButtonText, text);
	p.setColor(QPalette::BrightText, Qt::white);
	p.setColor(QPalette::Highlight, accent);
	p.setColor(QPalette::HighlightedText, Qt::white);
	p.setColor(QPalette::Link, QColor(0xB0, 0x8C, 0xF0));
	p.setColor(QPalette::PlaceholderText, QColor(0x90, 0x90, 0x90));
	p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
	p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
	p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
	QApplication::setPalette(p);
}

bool kindFromName(const QString& n, Kind* k)
{
	if (n == QLatin1String("documents") || n == QLatin1String("word"))
		*k = Kind::Word;
	else if (n == QLatin1String("spreadsheets") || n == QLatin1String("cell"))
		*k = Kind::Cell;
	else if (n == QLatin1String("presentations") || n == QLatin1String("slide"))
		*k = Kind::Slide;
	else
		return false;
	return true;
}
}

int main(int argc, char* argv[])
{
	// the arguments, read before anything else: a start that only hands its
	// files to the SG Office already running needs no display, no fonts
	QStringList args;
	for (int i = 0; i < argc; ++i)
		args << QString::fromLocal8Bit(argv[i]);
	QCommandLineParser cli;
	cli.setApplicationDescription(QStringLiteral("SG Office, based on ONLYOFFICE"));
	const QCommandLineOption helpOpt = cli.addHelpOption();
	QCommandLineOption newOpt(QStringLiteral("new"), QStringLiteral("A new document of this kind."),
	                          QStringLiteral("documents|spreadsheets|presentations"));
	cli.addOption(newOpt);
	cli.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Files to open."), QStringLiteral("[file...]"));
	if (!cli.parse(args))
	{
		std::fprintf(stderr, "sg-office: %s\n", qPrintable(cli.errorText()));
		return 2;
	}
	Kind newKind = Kind::Word;
	if (cli.isSet(newOpt) && !kindFromName(cli.value(newOpt), &newKind))
	{
		std::fprintf(stderr, "sg-office: --new takes documents, spreadsheets or presentations\n");
		return 2;
	}
	QStringList files;
	for (const QString& f : cli.positionalArguments())
		files << QFileInfo(f).absoluteFilePath();
	// no files: a new document (of the kind asked for, else a text document)
	const QString handKind = cli.isSet(newOpt) ? cli.value(newOpt) : files.isEmpty() ? QStringLiteral("documents") : QString();
#ifndef SG_MUTANT_NO_HANDOFF
	if (!cli.isSet(helpOpt) && Office::handOff(files, handKind))
	{
		if (!qEnvironmentVariableIsEmpty("SG_OFFICE_LOG"))
			std::fprintf(stderr, "sg-office: handed to the SG Office already running\n");
		return 0;
	}
#endif

	// Stained Glass OS's session shows X11 windows on its taskbar (XWayland
	// under sg-compositor): SG Office's window is one, unless told otherwise
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))
		qputenv("QT_QPA_PLATFORM", "xcb");
	SchemeHandler::registerSchemes();
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("SG Office"));
	QApplication::setOrganizationName(QStringLiteral("Stained Glass OS"));
	QApplication::setDesktopFileName(QStringLiteral("sg-office"));
	QApplication::setWindowIcon(AppIcon::of(cli.isSet(newOpt) ? newKind : Kind::Word));
	if (cli.isSet(helpOpt))
		cli.showHelp(0);

	// the session's look for SG Office's own dialogs too (Save As, Print)
	Office& office = Office::instance();
	applyDialogLook(office.dark());
	QObject::connect(&office, &Office::darkChanged, &app, [](bool dark) { applyDialogLook(dark); });

	QString err;
	if (!FontCache::instance().ensure(&err))
	{
		std::fprintf(stderr, "sg-office: %s\n", qPrintable(err));
		QMessageBox::critical(nullptr, QStringLiteral("SG Office"),
		                      QStringLiteral("SG Office could not list this computer's fonts.\n\n") + err);
		return 1;
	}
	if (FontCache::instance().generated() && !qEnvironmentVariableIsEmpty("SG_OFFICE_LOG"))
		std::fprintf(stderr, "sg-office: font tables generated\n");

	auto* handler = new SchemeHandler(&app);
	QWebEngineProfile* profile = QWebEngineProfile::defaultProfile();
	profile->installUrlSchemeHandler("sgoffice", handler);
	profile->installUrlSchemeHandler("ascdesktop", handler);
	injectBridge(profile);
	office.listen();

	int opened = 0;
	for (const QString& file : files)
		if (office.open(file, true))
			++opened;
	if (cli.isSet(newOpt) || files.isEmpty())
	{
		office.create(newKind, true);
		++opened;
	}
	if (opened == 0)
		return 1;
	return app.exec();
}

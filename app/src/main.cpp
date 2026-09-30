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
#include "document.h"
#include "fontcache.h"
#include "formats.h"
#include "schemehandler.h"
#include "window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
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
	SchemeHandler::registerSchemes();
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("SG Office"));
	QApplication::setOrganizationName(QStringLiteral("Stained Glass OS"));
	QApplication::setDesktopFileName(QStringLiteral("sg-office"));

	QCommandLineParser cli;
	cli.setApplicationDescription(QStringLiteral("SG Office, based on ONLYOFFICE"));
	cli.addHelpOption();
	QCommandLineOption newOpt(QStringLiteral("new"), QStringLiteral("A new document of this kind."),
	                          QStringLiteral("documents|spreadsheets|presentations"));
	cli.addOption(newOpt);
	cli.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Files to open."), QStringLiteral("[file...]"));
	cli.process(app);

	QString err;
	if (!FontCache::instance().ensure(&err))
	{
		std::fprintf(stderr, "sg-office: %s\n", qPrintable(err));
		QMessageBox::critical(nullptr, QStringLiteral("SG Office"),
		                      QStringLiteral("SG Office could not list this computer's fonts.\n\n") + err);
		return 1;
	}

	auto* handler = new SchemeHandler(&app);
	QWebEngineProfile* profile = QWebEngineProfile::defaultProfile();
	profile->installUrlSchemeHandler("sgoffice", handler);
	profile->installUrlSchemeHandler("ascdesktop", handler);
	injectBridge(profile);

	int opened = 0;
	for (const QString& file : cli.positionalArguments())
	{
		bool ok = false;
		const Kind kind = Formats::kindOfExt(QFileInfo(file).suffix(), &ok);
		if (!ok || !QFileInfo(file).isFile())
		{
			QMessageBox::warning(nullptr, QStringLiteral("SG Office"),
			                     QStringLiteral("SG Office cannot open \"%1\".").arg(QFileInfo(file).fileName()));
			continue;
		}
		(new EditorWindow(new Document(file, kind)))->show();
		++opened;
	}
	if (cli.isSet(newOpt) || opened == 0)
	{
		Kind kind = Kind::Word;
		if (cli.isSet(newOpt) && !kindFromName(cli.value(newOpt), &kind))
		{
			std::fprintf(stderr, "sg-office: --new takes documents, spreadsheets or presentations\n");
			return 2;
		}
		if (cli.isSet(newOpt) || cli.positionalArguments().isEmpty())
		{
			(new EditorWindow(new Document(QString(), kind)))->show();
			++opened;
		}
	}
	if (opened == 0)
		return 1;
	return app.exec();
}

/*
 * SG Office -- how the editors reach the files and the program.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "schemehandler.h"
#include "document.h"
#include "fontcache.h"
#include "paths.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QUrlQuery>
#include <QUuid>
#include <QWebEngineUrlScheme>

#include <pwd.h>
#include <unistd.h>

namespace
{
QHash<QString, DocumentHost*>& hosts()
{
	static QHash<QString, DocumentHost*> h;
	return h;
}

// a path under root, with no way out of it
QString resolveUnder(const QString& root, const QString& rel)
{
	const QString clean = QDir::cleanPath(QStringLiteral("/") + rel);
	if (clean.contains(QStringLiteral("/../")) || clean.endsWith(QStringLiteral("/..")))
		return {};
	const QString full = QDir::cleanPath(root + clean);
	if (!full.startsWith(QDir::cleanPath(root) + QLatin1Char('/')))
		return {};
	return full;
}

QByteArray mimeFor(const QString& file)
{
	static QMimeDatabase db;
	const QString suffix = QFileInfo(file).suffix().toLower();
	if (suffix == QLatin1String("js"))
		return QByteArrayLiteral("application/javascript");
	if (suffix == QLatin1String("json"))
		return QByteArrayLiteral("application/json");
	if (suffix == QLatin1String("css"))
		return QByteArrayLiteral("text/css");
	if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
		return QByteArrayLiteral("text/html");
	if (suffix == QLatin1String("svg"))
		return QByteArrayLiteral("image/svg+xml");
	if (suffix == QLatin1String("wasm"))
		return QByteArrayLiteral("application/wasm");
	if (suffix == QLatin1String("bin"))
		return QByteArrayLiteral("application/octet-stream");
	return db.mimeTypeForFile(file, QMimeDatabase::MatchExtension).name().toLatin1();
}

// the person using SG Office, as the account names them (for comments and
// tracked changes): the full name in the password database, or the login
QString userDisplayName()
{
	static QString name = [] {
		const struct passwd* pw = getpwuid(getuid());
		QString n;
		if (pw && pw->pw_gecos)
			n = QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed();
		if (n.isEmpty() && pw && pw->pw_name)
			n = QString::fromLocal8Bit(pw->pw_name);
		return n.isEmpty() ? QStringLiteral("User") : n;
	}();
	return name;
}

QByteArray readBody(QWebEngineUrlRequestJob* job)
{
	QIODevice* body = job->requestBody();
	if (!body)
		return {};
	if (!body->isOpen())
		body->open(QIODevice::ReadOnly);
	return body->readAll();
}
}

void replyFile(QWebEngineUrlRequestJob* job, const QString& file)
{
	auto* f = new QFile(file, job);
	if (file.isEmpty() || !QFileInfo(file).isFile() || !f->open(QIODevice::ReadOnly))
	{
		if (qEnvironmentVariableIntValue("SG_OFFICE_LOG") >= 2)
			qWarning("sg-office: not found: %s", qPrintable(job->requestUrl().toString()));
		delete f;
		job->fail(QWebEngineUrlRequestJob::UrlNotFound);
		return;
	}
	job->reply(mimeFor(file), f);
}

void replyJson(QWebEngineUrlRequestJob* job, const QJsonObject& obj)
{
	auto* buf = new QBuffer(job);
	buf->setData(QJsonDocument(obj).toJson(QJsonDocument::Compact));
	buf->open(QIODevice::ReadOnly);
	job->reply(QByteArrayLiteral("application/json"), buf);
}

SchemeHandler::SchemeHandler(QObject* parent) : QWebEngineUrlSchemeHandler(parent) {}

void SchemeHandler::registerSchemes()
{
	QWebEngineUrlScheme app("sgoffice");
	app.setSyntax(QWebEngineUrlScheme::Syntax::Host);
	app.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme |
	             QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::CorsEnabled |
	             QWebEngineUrlScheme::FetchApiAllowed);
	QWebEngineUrlScheme::registerScheme(app);

	QWebEngineUrlScheme fonts("ascdesktop");
	fonts.setSyntax(QWebEngineUrlScheme::Syntax::Host);
	// local like sgoffice: (Chromium lets a local page fetch only local schemes)
	fonts.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme |
	               QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::CorsEnabled |
	               QWebEngineUrlScheme::FetchApiAllowed);
	QWebEngineUrlScheme::registerScheme(fonts);
}

void SchemeHandler::setHost(const QString& docId, DocumentHost* host)
{
	if (host)
		hosts().insert(docId, host);
	else
		hosts().remove(docId);
}

void SchemeHandler::requestStarted(QWebEngineUrlRequestJob* job)
{
	const QUrl url = job->requestUrl();
	if (url.scheme() == QLatin1String("ascdesktop"))
	{
		serveFont(job);
		return;
	}
	if (url.host() != QLatin1String("app"))
	{
		job->fail(QWebEngineUrlRequestJob::UrlInvalid);
		return;
	}
	const QString path = url.path();          // decoded
	if (path.startsWith(QLatin1String("/native/")))
		serveNative(job, path.mid(8));
	else if (path.startsWith(QLatin1String("/doc/")))
		serveDoc(job, path.mid(5));
	else
		serveApp(job, path);
}

void SchemeHandler::serveApp(QWebEngineUrlRequestJob* job, const QString& path)
{
	// this user's font tables, not the build machine's
	if (path == QLatin1String("/sdkjs/common/AllFonts.js"))
	{
		replyFile(job, FontCache::instance().allFontsJs());
		return;
	}
	if (path.startsWith(QLatin1String("/shell/")))
	{
		replyFile(job, QStringLiteral(":/shell/") + path.mid(7));
		return;
	}
	replyFile(job, resolveUnder(Paths::shareDir(), path));
}

void SchemeHandler::serveDoc(QWebEngineUrlRequestJob* job, const QString& path)
{
	const int slash = path.indexOf(QLatin1Char('/'));
	Document* doc = Document::byId(path.left(slash));
	if (!doc || slash < 0)
	{
		job->fail(QWebEngineUrlRequestJob::UrlNotFound);
		return;
	}
	replyFile(job, resolveUnder(doc->workDir(), path.mid(slash)));
}

void SchemeHandler::serveFont(QWebEngineUrlRequestJob* job)
{
	if (qEnvironmentVariableIntValue("SG_OFFICE_LOG") >= 3)
		qWarning("sg-office: font request: %s from %s", qPrintable(job->requestUrl().toString()),
		         qPrintable(job->initiator().toString()));
	job->setAdditionalResponseHeaders({{QByteArrayLiteral("Access-Control-Allow-Origin"), QByteArrayLiteral("*")}});
	const QUrl url = job->requestUrl();
	if (url.host() != QLatin1String("fonts"))
	{
		job->fail(QWebEngineUrlRequestJob::UrlNotFound);
		return;
	}
	// ascdesktop://fonts/ + an absolute path: "//usr/share/fonts/..."
	QString file = QDir::cleanPath(url.path());
	const QString name = QFileInfo(file).fileName();
	if (name.startsWith(QLatin1String("fonts_thumbnail")))
		file = FontCache::instance().dir() + QLatin1Char('/') + name;
	else if (!FontCache::instance().isKnownFont(file))
	{
		if (qEnvironmentVariableIntValue("SG_OFFICE_LOG") >= 2)
			qWarning("sg-office: font refused: %s", qPrintable(url.toString()));
		job->fail(QWebEngineUrlRequestJob::RequestDenied);
		return;
	}
	replyFile(job, file);
}

void SchemeHandler::serveNative(QWebEngineUrlRequestJob* job, const QString& call)
{
	const QUrlQuery q(job->requestUrl());
	const QString docId = q.queryItemValue(QStringLiteral("doc"));
	Document* doc = Document::byId(docId);
	DocumentHost* host = hosts().value(docId, nullptr);
	if (!doc || !host)
	{
		job->fail(QWebEngineUrlRequestJob::RequestDenied);
		return;
	}
	QPointer<QWebEngineUrlRequestJob> pj(job);
	auto later = [pj](const QJsonObject& o) {
		if (pj)
			replyJson(pj, o);
	};

	if (call == QLatin1String("state"))
	{
		replyJson(job, QJsonObject{
			{QStringLiteral("id"), doc->id()},
			{QStringLiteral("title"), doc->title()},
			{QStringLiteral("path"), doc->path()},
			{QStringLiteral("folder"), doc->path().isEmpty() ? QString() : QFileInfo(doc->path()).absolutePath()},
			{QStringLiteral("kind"), Formats::kindName(doc->kind())},
			{QStringLiteral("format"), doc->format() ? doc->format()->code : 0},
			{QStringLiteral("saved"), !doc->isModified()},
			{QStringLiteral("changes"), doc->changesCount()},
			{QStringLiteral("url"), QStringLiteral("sgoffice://app/doc/") + doc->id()},
			{QStringLiteral("debug"), qEnvironmentVariableIntValue("SG_OFFICE_LOG") >= 2},
			{QStringLiteral("user"), userDisplayName()},
		});
	}
	else if (call == QLatin1String("open"))
	{
		doc->open([later, doc](bool ok, const QString& err) {
			later(QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("error"), err},
			                  {QStringLiteral("url"), QStringLiteral("sgoffice://app/doc/") + doc->id()}});
		});
	}
	else if (call == QLatin1String("changes"))
	{
		const QJsonObject o = QJsonDocument::fromJson(readBody(job)).object();
		QByteArrayList entries;
		for (const QJsonValue& v : o.value(QStringLiteral("entries")).toArray())
			entries.append(v.toString().toUtf8());
		doc->addChanges(entries, o.value(QStringLiteral("deleteIndex")).toInt(-1));
		replyJson(job, QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("count"), doc->changesCount()}});
	}
	else if (call == QLatin1String("save"))
	{
		const QJsonObject o = QJsonDocument::fromJson(readBody(job)).object();
		host->hostSave(o.value(QStringLiteral("saveAs")).toBool(), o.value(QStringLiteral("fileType")).toInt(),
		               o.value(QStringLiteral("json")).toString().toUtf8(), later);
	}
	else if (call == QLatin1String("modified"))
	{
		host->hostModified(q.queryItemValue(QStringLiteral("value")) == QLatin1String("1"));
		replyJson(job, QJsonObject{{QStringLiteral("ok"), true}});
	}
	else if (call == QLatin1String("command"))
	{
		const QJsonObject o = QJsonDocument::fromJson(readBody(job)).object();
		host->hostCommand(o.value(QStringLiteral("cmd")).toString(), o.value(QStringLiteral("param")).toString());
		replyJson(job, QJsonObject{{QStringLiteral("ok"), true}});
	}
	else if (call == QLatin1String("dialog-open"))
	{
		host->hostOpenDialog(q.queryItemValue(QStringLiteral("filter")),
		                     q.queryItemValue(QStringLiteral("multi")) == QLatin1String("1"), later);
	}
	else if (call == QLatin1String("image"))
	{
		// a local picture the user chose: copied into the document's media
		// folder, named as the editor will address it
		const QString src = q.queryItemValue(QStringLiteral("path"));
		const QString ext = QFileInfo(src).suffix().toLower();
		static const QStringList images = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
		                                   QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("svg"),
		                                   QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("webp")};
		QString name;
		if (images.contains(ext) && QFileInfo(src).isFile())
		{
			QDir().mkpath(doc->workDir() + QStringLiteral("/media"));
			name = QStringLiteral("image_") + QUuid::createUuid().toString(QUuid::Id128).left(12) + QLatin1Char('.') + ext;
			if (!QFile::copy(src, doc->workDir() + QStringLiteral("/media/") + name))
				name.clear();
		}
		replyJson(job, QJsonObject{{QStringLiteral("name"), name}});
	}
	else if (call == QLatin1String("exists"))
	{
		const QString f = resolveUnder(doc->workDir(), q.queryItemValue(QStringLiteral("name")));
		replyJson(job, QJsonObject{{QStringLiteral("exists"), !f.isEmpty() && QFileInfo(f).isFile()}});
	}
	else if (call == QLatin1String("log"))
	{
		host->hostLog(QString::fromUtf8(readBody(job)));
		replyJson(job, QJsonObject{{QStringLiteral("ok"), true}});
	}
	else
		job->fail(QWebEngineUrlRequestJob::UrlNotFound);
}

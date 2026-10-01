/*
 * SG Office -- how the editors reach the files and the program.
 *
 * Everything the editors load comes from one origin, sgoffice://app:
 *   sgoffice://app/web-apps/..., /sdkjs/...     the editors (shareDir)
 *   sgoffice://app/sdkjs/common/AllFonts.js      this user's font tables
 *   sgoffice://app/doc/<id>/Editor.bin, media/   an open document's folder
 *   sgoffice://app/shell/...                     our pages (resources)
 *   sgoffice://app/native/<call>?doc=<id>        the program (bridge.js)
 * and fonts come from ascdesktop://fonts/<path>, the address the engine
 * asks for them at; only fonts the font tables list are served.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QPointer>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlSchemeHandler>
#include <functional>

class Document;

// What the window hosting a document does for its editor.
class DocumentHost
{
public:
	virtual ~DocumentHost() = default;
	using Reply = std::function<void(const QJsonObject&)>;
	virtual void hostSave(bool saveAs, int fileType, const QByteArray& jsonParams, Reply reply) = 0;
	virtual void hostModified(bool modified) = 0;
	virtual void hostCommand(const QString& cmd, const QString& param) = 0;
	virtual void hostOpenDialog(const QString& filter, bool multi, Reply reply) = 0;
	virtual void hostLog(const QString& message) = 0;
	virtual void hostPrint(const QByteArray& json, Reply reply) = 0;
	// what the window adds to the document's state (the look, units, language)
	virtual QJsonObject hostState() = 0;
};

class SchemeHandler : public QWebEngineUrlSchemeHandler
{
	Q_OBJECT
public:
	explicit SchemeHandler(QObject* parent = nullptr);
	void requestStarted(QWebEngineUrlRequestJob* job) override;

	static void registerSchemes();                 // before QApplication
	static void setHost(const QString& docId, DocumentHost* host);

private:
	void serveApp(QWebEngineUrlRequestJob* job, const QString& path);
	void serveDoc(QWebEngineUrlRequestJob* job, const QString& path);
	void serveNative(QWebEngineUrlRequestJob* job, const QString& call);
	void serveFont(QWebEngineUrlRequestJob* job);
};

void replyFile(QWebEngineUrlRequestJob* job, const QString& file);
void replyJson(QWebEngineUrlRequestJob* job, const QJsonObject& obj);

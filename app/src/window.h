/*
 * SG Office -- a window with one document in its editor.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include "formats.h"
#include "schemehandler.h"

#include <QStringList>
#include <QVariant>
#include <QWidget>
#include <functional>

class Document;
class TitleBar;
class QWebEngineView;

class EditorWindow : public QWidget, public DocumentHost
{
	Q_OBJECT
public:
	// autopilot: SG_OFFICE_AUTOPILOT drives this window (the gates; only the
	// windows of the program's own start)
	EditorWindow(Document* doc, bool autopilot = false, QWidget* parent = nullptr);
	~EditorWindow() override;

	Document* document() const { return m_doc; }
	// in front, focused, not minimized (its file was opened again)
	void bringForward();

	// DocumentHost
	void hostSave(bool saveAs, int fileType, const QByteArray& jsonParams, Reply reply) override;
	void hostModified(bool modified) override;
	void hostCommand(const QString& cmd, const QString& param) override;
	void hostOpenDialog(const QString& filter, bool multi, Reply reply) override;
	void hostLog(const QString& message) override;
	void hostPrint(const QByteArray& json, Reply reply) override;
	QJsonObject hostState() override;

protected:
	bool event(QEvent* e) override;
	void closeEvent(QCloseEvent*) override;
	bool eventFilter(QObject* o, QEvent* e) override;

private:
	void updateTitle();
	void runInEditor(const QString& js, const std::function<void(const QVariant&)>& done = {});
	QString askSaveAsPath(int preferredCode, const Format** chosen);
	void autopilotStep();
	void sendRecents();
	void setWindowClass();
	QString windowClass();
	void applyLook(bool dark);

	Document* m_doc;
	TitleBar* m_title;
	QWebEngineView* m_view;
	bool m_ready = false;
	bool m_closeAfterSave = false;
	bool m_forceClose = false;
	QStringList m_autopilot;          // SG_OFFICE_AUTOPILOT: the gate drives the editor
};

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
#include <QWidget>

class Document;
class TitleBar;
class QWebEngineView;

class EditorWindow : public QWidget, public DocumentHost
{
	Q_OBJECT
public:
	EditorWindow(Document* doc, QWidget* parent = nullptr);
	~EditorWindow() override;

	// DocumentHost
	void hostSave(bool saveAs, int fileType, const QByteArray& jsonParams, Reply reply) override;
	void hostModified(bool modified) override;
	void hostCommand(const QString& cmd, const QString& param) override;
	void hostOpenDialog(const QString& filter, bool multi, Reply reply) override;
	void hostLog(const QString& message) override;

protected:
	void closeEvent(QCloseEvent*) override;
	bool eventFilter(QObject* o, QEvent* e) override;

private:
	void updateTitle();
	void runInEditor(const QString& js);
	QString askSaveAsPath(int preferredCode, const Format** chosen);
	void autopilotStep();

	Document* m_doc;
	TitleBar* m_title;
	QWebEngineView* m_view;
	bool m_ready = false;
	bool m_closeAfterSave = false;
	bool m_forceClose = false;
	QStringList m_autopilot;          // SG_OFFICE_AUTOPILOT: the gate drives the editor
};

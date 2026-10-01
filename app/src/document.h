/*
 * SG Office -- one open document: its working folder, the conversions in and
 * out of the editor's format, and the editor's log of changes.
 *
 * Opening converts the file with x2t into the folder (Editor.bin + media/),
 * which the editor loads. While editing, the editor sends its changes here
 * and they are kept in changes/changes0.json, in the form x2t reads. Saving
 * runs x2t again: Editor.bin + the changes -> the file, written beside the
 * target and renamed over it only once complete.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include "formats.h"

#include <QByteArrayList>
#include <QObject>
#include <QString>
#include <functional>

class Document : public QObject
{
	Q_OBJECT
public:
	// sourcePath empty: a new, untitled document of this kind
	Document(const QString& sourcePath, Kind kind, QObject* parent = nullptr);
	~Document() override;

	static Document* byId(const QString& id);

	QString id() const { return m_id; }
	Kind kind() const { return m_kind; }
	QString workDir() const { return m_workDir; }
	QString path() const { return m_path; }           // empty while untitled
	QString title() const;                             // file name, or "Document1"
	const Format* format() const { return m_format; }  // null while untitled
	bool isModified() const { return m_modified; }
	void setModified(bool modified);

	// x2t: the file (or the new-document template) -> Editor.bin
	void open(std::function<void(bool ok, const QString& error)> done);

	// The editor's changes, appended after dropping any from deleteIndex on
	// (the editor undid past what it had sent).
	void addChanges(const QByteArrayList& entries, int deleteIndex);
	int changesCount() const { return m_changes.size(); }

	// x2t: Editor.bin + changes -> target in format (0 = the document's own).
	// On success the document is now target/format (unless it was an export:
	// PDF, or keepIdentity).
	void save(const QString& target, int formatCode, const QByteArray& jsonParams,
	          std::function<void(bool ok, const QString& error)> done);

signals:
	void modifiedChanged(bool modified);
	void identityChanged();                            // path/title after Save As

private:
	void runX2t(const QString& paramsXml, std::function<void(int code, const QString& log)> done);
	void writeChanges();

	QString m_id;
	Kind m_kind;
	QString m_path;
	const Format* m_format = nullptr;
	QString m_workDir;
	QByteArrayList m_changes;
	bool m_modified = false;
	int m_untitledNumber = 0;
	int m_textEncoding = 46;                          // a CSV/text file's, as read (UTF-8)
	int m_csvDelimiter = 4;                           // a CSV's, as read (comma)
};

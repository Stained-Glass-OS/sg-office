/*
 * SG Office -- a window with one document in its editor.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "window.h"
#include "appicon.h"
#include "document.h"
#include "office.h"
#include "printing.h"
#include "titlebar.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWebEngineFrame>
#include <QWebEnginePage>
#include <QWebEnginePermission>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QWindow>
#include <cstdio>
#include <xcb/xcb.h>

namespace
{
constexpr int kResizeMargin = 5;

void debugLog(const QString& line)
{
	static const bool on = !qEnvironmentVariableIsEmpty("SG_OFFICE_LOG");
	if (on)
		std::fprintf(stderr, "sg-office: %s\n", qPrintable(line));
}

QColor accentOf(Kind k)
{
	switch (k)
	{
	case Kind::Word: return QColor(0x2F, 0x6F, 0xD8);
	case Kind::Cell: return QColor(0x23, 0x9A, 0x5E);
	case Kind::Slide: return QColor(0xEE, 0x74, 0x36);
	}
	return QColor(0x70, 0x30, 0xC0);
}

// The editors' page: our origin only. A link to anywhere else opens outside.
class EditorPage : public QWebEnginePage
{
public:
	using QWebEnginePage::QWebEnginePage;

protected:
	bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override
	{
		if (url.scheme() == QLatin1String("sgoffice") || url.scheme() == QLatin1String("about")
		    || url.scheme() == QLatin1String("data") || url.scheme() == QLatin1String("blob"))
			return true;
		if (type == NavigationTypeLinkClicked && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")))
			QDesktopServices::openUrl(url);
		Q_UNUSED(isMainFrame);
		return false;
	}
	void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString& message, int line,
	                              const QString& source) override
	{
		if (level == ErrorMessageLevel || qEnvironmentVariableIntValue("SG_OFFICE_LOG") >= 2)
			debugLog(QStringLiteral("console error: %1 (%2:%3)").arg(message, source).arg(line));
	}
};
}

EditorWindow::EditorWindow(Document* doc, bool autopilot, QWidget* parent)
	: QWidget(parent, Qt::Window | Qt::FramelessWindowHint), m_doc(doc)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setMouseTracking(true);
	setMinimumSize(640, 420);
	resize(1280, 820);

	m_title = new TitleBar(this);
	m_title->setAccent(accentOf(doc->kind()));
	// the program's icon: the window's (the taskbar's button) and the title bar's
	setWindowIcon(AppIcon::of(doc->kind()));
	m_title->setIcon(AppIcon::of(doc->kind()));
	connect(m_title, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
	connect(m_title, &TitleBar::maximizeRequested, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
	connect(m_title, &TitleBar::closeRequested, this, &QWidget::close);

	auto* page = new EditorPage(QWebEngineProfile::defaultProfile(), this);
	m_view = new QWebEngineView(this);
	m_view->setPage(page);
	m_view->setContextMenuPolicy(Qt::NoContextMenu);
	auto* settings = page->settings();
	settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
	settings->setAttribute(QWebEngineSettings::JavascriptCanPaste, true);
	settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
	settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
	connect(page, &QWebEnginePage::permissionRequested, page, [](QWebEnginePermission permission) {
		// the clipboard, for our own pages; nothing else (camera, location, ...)
		const bool ok = permission.origin().scheme() == QLatin1String("sgoffice")
		    && permission.permissionType() == QWebEnginePermission::PermissionType::ClipboardReadWrite;
		ok ? permission.grant() : permission.deny();
	});

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(kResizeMargin, kResizeMargin, kResizeMargin, kResizeMargin);
	layout->setSpacing(0);
	layout->addWidget(m_title);
	layout->addWidget(m_view, 1);

	SchemeHandler::setHost(doc->id(), this);
	connect(doc, &Document::modifiedChanged, this, &EditorWindow::updateTitle);
	connect(doc, &Document::identityChanged, this, &EditorWindow::updateTitle);
	updateTitle();

	if (autopilot)
		m_autopilot = qEnvironmentVariable("SG_OFFICE_AUTOPILOT").split(QLatin1Char(';'), Qt::SkipEmptyParts);

	applyLook(Office::instance().dark());
	connect(&Office::instance(), &Office::darkChanged, this, [this](bool dark) {
		applyLook(dark);
		// the editors' own theme, while they run
		runInEditor(QStringLiteral("window.on_native_message && window.on_native_message('theme:changed', '%1');")
		            .arg(dark ? QStringLiteral("theme-dark") : QStringLiteral("theme-sg-light")));
	});
	connect(&Office::instance(), &Office::recentsChanged, this, &EditorWindow::sendRecents);

	QUrl url(QStringLiteral("sgoffice://app/shell/editor.html"));
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("doc"), doc->id());
	q.addQueryItem(QStringLiteral("doctype"), Formats::kindName(doc->kind()));
	q.addQueryItem(QStringLiteral("title"), doc->title());
	q.addQueryItem(QStringLiteral("filetype"), doc->format() ? doc->format()->ext
	               : QString::fromLatin1(doc->kind() == Kind::Word ? "docx" : doc->kind() == Kind::Cell ? "xlsx" : "pptx"));
	url.setQuery(q);
	m_view->load(url);
	qApp->installEventFilter(this);
}

EditorWindow::~EditorWindow()
{
	qApp->removeEventFilter(this);
	SchemeHandler::setHost(m_doc->id(), nullptr);
	delete m_doc;
}

// The window's X class: sg-office-documents (-spreadsheets,
// -presentations). The taskbar shows a Linux program's window with the icon
// filed under its class (wine-sg 0612; Office::installTaskbarIcons writes
// SG Office's), so each program gets its own icon, not one for all three.
// No spaces: the compositor's window list (XWINDOWS) ends a class at one.
void EditorWindow::setWindowClass()
{
#ifndef SG_MUTANT_NO_TASKBAR_ICON
	auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
	if (!x11 || !windowHandle())
		return;
	QByteArray cls = "sg-office";
	cls.append('\0');
	cls.append(Formats::programId(m_doc->kind()).toUtf8());
	cls.append('\0');
	xcb_change_property(x11->connection(), XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(winId()), XCB_ATOM_WM_CLASS,
	                    XCB_ATOM_STRING, 8, static_cast<uint32_t>(cls.size()), cls.constData());
	xcb_flush(x11->connection());
#endif
}

QString EditorWindow::windowClass()
{
	auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
	if (!x11)
		return {};
	xcb_get_property_reply_t* r = xcb_get_property_reply(x11->connection(),
		xcb_get_property(x11->connection(), 0, static_cast<xcb_window_t>(winId()), XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 64), nullptr);
	if (!r)
		return {};
	const QByteArray v(static_cast<const char*>(xcb_get_property_value(r)), xcb_get_property_value_length(r));
	free(r);
	return QString::fromUtf8(v).replace(QLatin1Char('\0'), QLatin1Char('|'));
}

bool EditorWindow::event(QEvent* e)
{
	// Qt names the X window afresh whenever it makes one
	if (e->type() == QEvent::PlatformSurface
	    && static_cast<QPlatformSurfaceEvent*>(e)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
	{
		const bool r = QWidget::event(e);
		setWindowClass();
		return r;
	}
	return QWidget::event(e);
}

void EditorWindow::bringForward()
{
	if (isMinimized())
		showNormal();
	show();
	raise();
	activateWindow();
}

// What the editor's page needs to know beyond the document: the look, the
// units and the language of this account (a US English account measures in
// inches, as Office does there).
QJsonObject EditorWindow::hostState()
{
	QLocale loc = QLocale::system();
	if (loc.language() == QLocale::C)
		loc = QLocale(QLocale::English, QLocale::UnitedStates);   // no locale set: Office's default
	const bool inches = loc.measurementSystem() != QLocale::MetricSystem;
	return QJsonObject{
		{QStringLiteral("dark"), Office::instance().dark()},
#ifndef SG_MUTANT_UNITS_CM
		{QStringLiteral("unit"), inches ? QStringLiteral("inch") : QStringLiteral("cm")},
#else
		{QStringLiteral("unit"), QStringLiteral("cm")},
#endif
		{QStringLiteral("region"), loc.name().replace(QLatin1Char('_'), QLatin1Char('-'))},
		{QStringLiteral("lang"), loc.name().section(QLatin1Char('_'), 0, 0)},
	};
}

void EditorWindow::applyLook(bool dark)
{
	m_title->setDark(dark);
	setStyleSheet(dark ? QStringLiteral("EditorWindow { background: #202020; }") : QStringLiteral("EditorWindow { background: #ffffff; }"));
}

void EditorWindow::sendRecents()
{
#ifdef SG_MUTANT_NO_RECENTS
	return;
#endif
	if (!m_ready)
		return;
	const QByteArray list = QJsonDocument(Office::instance().recents()).toJson(QJsonDocument::Compact);
	runInEditor(QStringLiteral("window.onupdaterecents && window.onupdaterecents(%1);").arg(QString::fromUtf8(list)));
}

void EditorWindow::updateTitle()
{
	const QString t = m_doc->title() + QStringLiteral(" - ") + Formats::productName(m_doc->kind());
	setWindowTitle(t);
	m_title->setTitle(m_doc->isModified() ? QStringLiteral("• ") + t : t);
}

void EditorWindow::runInEditor(const QString& js, const std::function<void(const QVariant&)>& done)
{
	// the editor runs in DocsAPI's frame; ours is the page around it
	std::function<void(QWebEngineFrame)> visit = [&](QWebEngineFrame f) {
		if (f.url().path().contains(QLatin1String("/main/index.html")))
		{
			if (done)
				f.runJavaScript(js, done);
			else
				f.runJavaScript(js);
		}
		for (const QWebEngineFrame& c : f.children())
			visit(c);
	};
	visit(m_view->page()->mainFrame());
}

QString EditorWindow::askSaveAsPath(int preferredCode, const Format** chosen)
{
	const QList<const Format*> choices = Formats::saveChoices(m_doc->kind());
	QStringList filters;
	QString selected;
	const Format* pref = preferredCode ? Formats::byCode(preferredCode) : m_doc->format();
	for (const Format* f : choices)
	{
		const QString filter = QStringLiteral("%1 (*.%2)").arg(f->label, f->ext);
		filters << filter;
		if (selected.isEmpty() && pref && f->code == pref->code && f->writable)
			selected = filter;
	}
	if (selected.isEmpty())
		selected = filters.first();          // the Microsoft format of this kind
	QString dir = m_doc->path().isEmpty()
		? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
		: QFileInfo(m_doc->path()).absolutePath();
	const QString base = QFileInfo(m_doc->title()).completeBaseName();
	QString sel = selected;
	QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save As"), dir + QLatin1Char('/') + base,
	                                            filters.join(QStringLiteral(";;")), &sel);
	if (path.isEmpty())
		return {};
	const int idx = filters.indexOf(sel);
	const Format* f = choices.value(idx >= 0 ? idx : 0);
	if (QFileInfo(path).suffix().toLower() != f->ext)
		path += QLatin1Char('.') + f->ext;
	*chosen = f;
	return path;
}

void EditorWindow::hostSave(bool saveAs, int fileType, const QByteArray& jsonParams, Reply reply)
{
	// the editor is mid-request: ask for a name after it returns to the loop
	QTimer::singleShot(0, this, [this, saveAs, fileType, jsonParams, reply] {
		const Format* fmt = m_doc->format();
		QString target = m_doc->path();
		const bool needName = saveAs || fileType || target.isEmpty() || !fmt || !fmt->writable
		                      || qEnvironmentVariableIsSet("SG_OFFICE_SAVE_AS");
		if (needName)
		{
			const QString forced = qEnvironmentVariable("SG_OFFICE_SAVE_AS");   // the gate's answer to the dialog
			if (!forced.isEmpty())
			{
				target = forced;
				fmt = Formats::byExt(QFileInfo(forced).suffix());
				if (!fmt && QFileInfo(forced).suffix().toLower() == QLatin1String("pdf"))
					fmt = nullptr;
			}
			else
				target = askSaveAsPath(fileType, &fmt);
			if (target.isEmpty())
			{
				reply(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("cancelled"), true}});
				m_closeAfterSave = false;
				return;
			}
		}
		const int code = fmt ? fmt->code : (QFileInfo(target).suffix().toLower() == QLatin1String("pdf") ? 513 : 0);
		// a PDF is a copy of the document, not the document saved: it stays
		// as modified as it was (its window still asks to save it on close)
#ifndef SG_MUTANT_EXPORT_SAVES
		const bool exported = code == 513;
#else
		const bool exported = false;
#endif
		const bool wasModified = m_doc->isModified();
		m_doc->save(target, code, jsonParams, [this, reply, exported, wasModified](bool ok, const QString& err) {
			if (ok && exported)
			{
				m_doc->setModified(wasModified);
				hostLog(QStringLiteral("exported a copy"));
				reply(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("exported"), true}});
				m_closeAfterSave = false;
				autopilotStep();
				return;
			}
			if (ok)
			{
				m_doc->setModified(false);
				Office::instance().addRecent(m_doc->path(), m_doc->format() ? m_doc->format()->code : 0);
			}
			else
			{
				hostLog(QStringLiteral("save failed: ") + err);
				if (qEnvironmentVariableIsEmpty("SG_OFFICE_AUTOPILOT"))
					QMessageBox::warning(this, Formats::productName(m_doc->kind()),
					                     QStringLiteral("The document could not be saved.\n\n%1").arg(err));
			}
			reply(QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("error"), err}});
			hostLog(ok ? QStringLiteral("saved ") + m_doc->path() : QStringLiteral("not saved"));
			if (ok && m_closeAfterSave)
			{
				m_forceClose = true;
				close();
			}
			else
				autopilotStep();
		});
	});
}

void EditorWindow::hostModified(bool modified)
{
	m_doc->setModified(modified);
}

void EditorWindow::hostCommand(const QString& cmd, const QString& param)
{
	debugLog(QStringLiteral("command %1 %2").arg(cmd, param.left(120)));
	// the editor is mid-call: act when it has returned
	QTimer::singleShot(0, this, [this, cmd, param] {
		if (cmd == QLatin1String("create:new"))
		{
			// File > New (and Create from template): a new document of this kind
			const QString k = param.section(QLatin1Char(':'), -1);
			Office::instance().create(k == QLatin1String("cell") ? Kind::Cell : k == QLatin1String("slide") ? Kind::Slide
			                          : k == QLatin1String("word") ? Kind::Word : m_doc->kind());
		}
		else if (cmd == QLatin1String("open:recent"))
		{
			const QString path = QJsonDocument::fromJson(param.toUtf8()).object().value(QStringLiteral("path")).toString();
			if (!path.isEmpty())
				Office::instance().open(path);
		}
		else if (cmd == QLatin1String("editor:event"))
		{
			const QString action = QJsonDocument::fromJson(param.toUtf8()).object().value(QStringLiteral("action")).toString();
			if (action == QLatin1String("file:open"))
				Office::instance().openDialog(this, m_doc->kind());
			else if (action == QLatin1String("file:close"))
				close();
		}
		else if (cmd == QLatin1String("sg:recents"))
			sendRecents();
	});
}

void EditorWindow::hostPrint(const QByteArray& json, Reply reply)
{
	QTimer::singleShot(0, this, [this, json, reply] {
		const QJsonObject o = QJsonDocument::fromJson(json).object();
		const QJsonObject native = o.value(QStringLiteral("nativeOptions")).toObject();
		Printing::Job job = Printing::fromEditor(native, m_doc->title());
		// Quick Print: the default printer, no questions; else the dialog
		if (!native.value(QStringLiteral("quickPrint")).toBool() && !Printing::ask(this, &job))
		{
			reply(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("cancelled"), true}});
			hostLog(QStringLiteral("print: cancelled"));
			return;
		}
		// the PDF the printer gets: the document as it is now, laid out for print
		QJsonObject params = o;
		params.remove(QStringLiteral("nativeOptions"));
		QJsonObject layout = params.value(QStringLiteral("documentLayout")).toObject();
		layout.insert(QStringLiteral("isPrint"), true);
		params.insert(QStringLiteral("documentLayout"), layout);
		if (m_doc->kind() == Kind::Cell && !params.contains(QStringLiteral("spreadsheetLayout")))
			params.insert(QStringLiteral("spreadsheetLayout"), QJsonObject{{QStringLiteral("fitToWidth"), 0}, {QStringLiteral("fitToHeight"), 0}});
		const QString dir = m_doc->workDir() + QStringLiteral("/print");
		QDir(dir).removeRecursively();
		QDir().mkpath(dir);
		const QString pdf = dir + QStringLiteral("/") + QFileInfo(m_doc->title()).completeBaseName() + QStringLiteral(".pdf");
		const bool wasModified = m_doc->isModified();
		m_doc->save(pdf, 513, QJsonDocument(params).toJson(QJsonDocument::Compact),
		            [this, job, pdf, reply, wasModified](bool ok, const QString& err) {
			m_doc->setModified(wasModified);
			if (!ok)
			{
				hostLog(QStringLiteral("print: no PDF: ") + err);
				QMessageBox::warning(this, Formats::productName(m_doc->kind()),
				                     QStringLiteral("The document could not be prepared for printing.\n\n%1").arg(err));
				reply(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), err}});
				return;
			}
			hostLog(QStringLiteral("print: %1 %2").arg(job.toFile.isEmpty() ? QStringLiteral("lp") : QStringLiteral("to ") + job.toFile,
			                                            Printing::lpArguments(job, pdf).join(QLatin1Char(' '))));
			Printing::submit(job, pdf, [this, reply](bool ok, const QString& error) {
				hostLog(ok ? QStringLiteral("print: sent") : QStringLiteral("print: failed: ") + error);
				if (!ok && qEnvironmentVariableIsEmpty("SG_OFFICE_AUTOPILOT"))
					QMessageBox::warning(this, Formats::productName(m_doc->kind()),
					                     QStringLiteral("The document could not be printed.\n\n%1").arg(error));
				reply(QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("error"), error}});
				autopilotStep();
			});
		});
	});
}

void EditorWindow::hostOpenDialog(const QString& filter, bool multi, Reply reply)
{
	QTimer::singleShot(0, this, [this, filter, multi, reply] {
		QString f;
		if (filter.contains(QLatin1String("image"), Qt::CaseInsensitive))
			f = QStringLiteral("Pictures (*.png *.jpg *.jpeg *.gif *.bmp *.svg *.tif *.tiff *.webp)");
		const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
		QStringList files = multi ? QFileDialog::getOpenFileNames(this, QStringLiteral("Open"), dir, f)
		                          : QStringList{QFileDialog::getOpenFileName(this, QStringLiteral("Open"), dir, f)};
		files.removeAll(QString());
		reply(QJsonObject{{QStringLiteral("files"), QJsonArray::fromStringList(files)}});
	});
}

void EditorWindow::hostLog(const QString& message)
{
	debugLog(message);
	if (message == QLatin1String("ready") && !m_ready)
	{
		m_ready = true;
		// the file is open: it is a recent file now (the list goes to every window)
		if (!m_doc->path().isEmpty() && m_doc->format())
			Office::instance().addRecent(m_doc->path(), m_doc->format()->code);
		sendRecents();
		QTimer::singleShot(500, this, &EditorWindow::autopilotStep);
	}
}

// SG_OFFICE_AUTOPILOT="type:Hello;key:Return;save;quit" -- the gate drives
// the editor the way a person does: real key events into the view.
void EditorWindow::autopilotStep()
{
	if (m_autopilot.isEmpty() || !m_ready)
		return;
	const QString step = m_autopilot.takeFirst();
	debugLog(QStringLiteral("autopilot: ") + step);
	QWidget* target = m_view->focusProxy() ? m_view->focusProxy() : m_view;
	m_view->setFocus();
	if (step.startsWith(QLatin1String("type:")))
	{
		for (const QChar c : step.mid(5))
		{
			QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, 0, Qt::NoModifier, QString(c)));
			QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, 0, Qt::NoModifier, QString(c)));
		}
		QTimer::singleShot(1500, this, &EditorWindow::autopilotStep);
	}
	else if (step.startsWith(QLatin1String("key:")))
	{
		// key:Return, key:Alt+F4, key:Ctrl+W ...
		QString k = step.mid(4);
		Qt::KeyboardModifiers mods;
		if (k.startsWith(QLatin1String("Alt+"))) { mods |= Qt::AltModifier; k = k.mid(4); }
		if (k.startsWith(QLatin1String("Ctrl+"))) { mods |= Qt::ControlModifier; k = k.mid(5); }
		const int key = k == QLatin1String("Return") ? Qt::Key_Return : k == QLatin1String("Tab") ? Qt::Key_Tab
		              : k == QLatin1String("Down") ? Qt::Key_Down : k == QLatin1String("Home") ? Qt::Key_Home
		              : k == QLatin1String("End") ? Qt::Key_End : k == QLatin1String("F4") ? Qt::Key_F4
		              : k.size() == 1 ? k.at(0).toUpper().unicode() : 0;
		const QString text = mods ? QString() : key == Qt::Key_Return ? QStringLiteral("\r") : key == Qt::Key_Tab ? QStringLiteral("\t") : QString();
		QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, key, mods, text));
		QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, key, mods, text));
		QTimer::singleShot(800, this, &EditorWindow::autopilotStep);
	}
	else if (step.startsWith(QLatin1String("eval:")))
	{
		// the editor's answer to a JavaScript expression, in the log
		runInEditor(step.mid(5), [this](const QVariant& v) {
			hostLog(QStringLiteral("eval ") + v.toString());
			QTimer::singleShot(200, this, &EditorWindow::autopilotStep);
		});
	}
	else if (step == QLatin1String("state"))
	{
		hostLog(QStringLiteral("state modified=%1 path=%2").arg(m_doc->isModified() ? 1 : 0).arg(m_doc->path()));
		QTimer::singleShot(200, this, &EditorWindow::autopilotStep);
	}
	else if (step == QLatin1String("wmclass"))
	{
		hostLog(QStringLiteral("wmclass ") + windowClass());
		QTimer::singleShot(200, this, &EditorWindow::autopilotStep);
	}
	else if (step == QLatin1String("windows"))
	{
		QStringList docs;
		for (EditorWindow* w : Office::instance().windows())
			docs << (w->document()->path().isEmpty() ? w->document()->title() : w->document()->path());
		hostLog(QStringLiteral("windows %1: %2").arg(docs.size()).arg(docs.join(QStringLiteral(" | "))));
		QTimer::singleShot(200, this, &EditorWindow::autopilotStep);
	}
	else if (step.startsWith(QLatin1String("print")))
	{
		// print / print:quick -- File > Print's Print button, or Quick Print
		const QString native = step == QLatin1String("print:quick") ? QStringLiteral("{quickPrint: true}")
		                     : QStringLiteral("{pages: 'all', copies: 2, sides: 'both-long'}");
		runInEditor(QStringLiteral("(function(){var a=new Asc.asc_CAdjustPrint();a.asc_setNativeOptions(%1);"
		                           "var o=new Asc.asc_CDownloadOptions();o.asc_setAdvancedOptions(a);"
		                           "(window.Asc && Asc.editor || window.editor).asc_Print(o);})();").arg(native));
	}
	else if (step == QLatin1String("exportpdf"))
		// File > Download as > PDF (and Print to PDF): a copy, SG_OFFICE_SAVE_AS names it
		runInEditor(QStringLiteral("(window.Asc && Asc.editor || window.editor).asc_DownloadAs(new Asc.asc_CDownloadOptions(Asc.c_oAscFileType.PDF));"));
	else if (step == QLatin1String("quitall"))
	{
		for (EditorWindow* w : Office::instance().windows())
		{
			w->m_forceClose = true;
			if (w != this)
				w->close();
		}
		close();
	}
	else if (step.startsWith(QLatin1String("click:")))
	{
		// a click at x,y of the editor view (as a fraction of its size when < 1)
		const QStringList xy = step.mid(6).split(QLatin1Char(','));
		double x = xy.value(0).toDouble(), y = xy.value(1).toDouble();
		if (x < 1 && y < 1)
		{
			x *= m_view->width();
			y *= m_view->height();
		}
		const QPointF local(x, y);
		const QPointF global = m_view->mapToGlobal(local.toPoint());
		for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease})
			QCoreApplication::postEvent(target, new QMouseEvent(type, local, global, Qt::LeftButton,
			                            type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier));
		QTimer::singleShot(800, this, &EditorWindow::autopilotStep);
	}
	else if (step.startsWith(QLatin1String("pixel:")))
	{
		// the colour at x,y of the window, for the look's gate
		const QStringList xy = step.mid(6).split(QLatin1Char(','));
		const QColor c = grab().toImage().pixelColor(xy.value(0).toInt(), xy.value(1).toInt());
		hostLog(QStringLiteral("pixel %1,%2 %3").arg(xy.value(0), xy.value(1), c.name()));
		QTimer::singleShot(200, this, &EditorWindow::autopilotStep);
	}
	else if (step.startsWith(QLatin1String("shot:")))
	{
		grab().save(step.mid(5));                  // the whole window, title bar included
		QTimer::singleShot(200, this, &EditorWindow::autopilotStep);
	}
	else if (step == QLatin1String("save"))
		runInEditor(QStringLiteral("(window.Asc && Asc.editor || window.editor).asc_Save(false);"));
	else if (step == QLatin1String("saveas"))
		runInEditor(QStringLiteral("(window.Asc && Asc.editor || window.editor).asc_Save(false, true);"));
	else if (step.startsWith(QLatin1String("wait:")))
		QTimer::singleShot(step.mid(5).toInt(), this, &EditorWindow::autopilotStep);
	else if (step == QLatin1String("quit"))
	{
		m_forceClose = true;
		close();
	}
}

void EditorWindow::closeEvent(QCloseEvent* e)
{
	if (m_forceClose || !m_doc->isModified())
	{
		e->accept();
		return;
	}
	const auto answer = QMessageBox::question(
		this, Formats::productName(m_doc->kind()),
		QStringLiteral("Do you want to save changes to %1?").arg(m_doc->title()),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	if (answer == QMessageBox::Discard)
	{
		e->accept();
		return;
	}
	e->ignore();
	if (answer == QMessageBox::Save)
	{
		m_closeAfterSave = true;
		runInEditor(QStringLiteral("(window.Asc && Asc.editor || window.editor).asc_Save(false);"));
	}
}

// Resizing a frameless window: the few pixels around the content hand the
// drag to the compositor.
bool EditorWindow::eventFilter(QObject* o, QEvent* e)
{
	// Alt+F4 closes the window, as every window in the session (the
	// compositor leaves it to the program), and Ctrl+W the document -- caught
	// on their way to the editor, which would otherwise take them
#ifndef SG_MUTANT_NO_ALTF4
	if (e->type() == QEvent::KeyPress)
	{
		auto* ke = static_cast<QKeyEvent*>(e);
		auto* w = qobject_cast<QWidget*>(o);
		const auto mods = ke->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
		if (w && w->window() == this && !ke->isAutoRepeat()
		    && ((ke->key() == Qt::Key_F4 && mods == Qt::AltModifier) || (ke->key() == Qt::Key_W && mods == Qt::ControlModifier)))
		{
			QTimer::singleShot(0, this, &QWidget::close);
			return true;
		}
	}
#endif
	if (o != this && o != windowHandle())
		return QWidget::eventFilter(o, e);
	if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseMove)
	{
		auto* me = static_cast<QMouseEvent*>(e);
		const QPoint p = mapFromGlobal(me->globalPosition().toPoint());
		Qt::Edges edges;
		if (p.x() < kResizeMargin) edges |= Qt::LeftEdge;
		if (p.x() >= width() - kResizeMargin) edges |= Qt::RightEdge;
		if (p.y() < kResizeMargin) edges |= Qt::TopEdge;
		if (p.y() >= height() - kResizeMargin) edges |= Qt::BottomEdge;
		if (e->type() == QEvent::MouseMove)
		{
			const bool diagA = edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge);
			const bool diagB = edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge);
			setCursor(diagA ? Qt::SizeFDiagCursor : diagB ? Qt::SizeBDiagCursor
			          : (edges & (Qt::LeftEdge | Qt::RightEdge)) ? Qt::SizeHorCursor
			          : edges ? Qt::SizeVerCursor : Qt::ArrowCursor);
		}
		else if (edges && me->button() == Qt::LeftButton && windowHandle() && !isMaximized())
		{
			windowHandle()->startSystemResize(edges);
			return true;
		}
	}
	return QWidget::eventFilter(o, e);
}

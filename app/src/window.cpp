/*
 * SG Office -- a window with one document in its editor.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "window.h"
#include "document.h"
#include "titlebar.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QKeyEvent>
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
#include <QWindow>
#include <cstdio>

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

EditorWindow::EditorWindow(Document* doc, QWidget* parent)
	: QWidget(parent, Qt::Window | Qt::FramelessWindowHint), m_doc(doc)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setMouseTracking(true);
	setMinimumSize(640, 420);
	resize(1280, 820);

	m_title = new TitleBar(this);
	m_title->setAccent(accentOf(doc->kind()));
	m_title->setIcon(QIcon::fromTheme(QStringLiteral("sg-office-") +
	    QString::fromLatin1(doc->kind() == Kind::Word ? "documents" : doc->kind() == Kind::Cell ? "spreadsheets" : "presentations")));
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

	m_autopilot = qEnvironmentVariable("SG_OFFICE_AUTOPILOT").split(QLatin1Char(';'), Qt::SkipEmptyParts);

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

void EditorWindow::updateTitle()
{
	const QString t = m_doc->title() + QStringLiteral(" - ") + Formats::productName(m_doc->kind());
	setWindowTitle(t);
	m_title->setTitle(m_doc->isModified() ? QStringLiteral("• ") + t : t);
}

void EditorWindow::runInEditor(const QString& js)
{
	// the editor runs in DocsAPI's frame; ours is the page around it
	std::function<void(QWebEngineFrame)> visit = [&](QWebEngineFrame f) {
		if (f.url().path().contains(QLatin1String("/main/index.html")))
			f.runJavaScript(js);
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
		m_doc->save(target, code, jsonParams, [this, reply](bool ok, const QString& err) {
			if (ok)
				m_doc->setModified(false);
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
		const QString k = step.mid(4);
		const int key = k == QLatin1String("Return") ? Qt::Key_Return : k == QLatin1String("Tab") ? Qt::Key_Tab
		              : k == QLatin1String("Down") ? Qt::Key_Down : k == QLatin1String("Home") ? Qt::Key_Home
		              : k == QLatin1String("End") ? Qt::Key_End : 0;
		const QString text = key == Qt::Key_Return ? QStringLiteral("\r") : key == Qt::Key_Tab ? QStringLiteral("\t") : QString();
		QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier, text));
		QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier, text));
		QTimer::singleShot(800, this, &EditorWindow::autopilotStep);
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

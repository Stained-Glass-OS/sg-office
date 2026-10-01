/*
 * SG Office -- the title bar SG Office draws itself (the window has no frame
 * from the compositor): the program's icon, the document's title, and the
 * minimise, maximise and close buttons, in Stained Glass OS's light style.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <QColor>
#include <QIcon>
#include <QWidget>

class TitleBar : public QWidget
{
	Q_OBJECT
public:
	explicit TitleBar(QWidget* parent = nullptr);
	void setTitle(const QString& title);
	void setIcon(const QIcon& icon);
	void setAccent(const QColor& accent);
	void setDark(bool dark);           // the session's dark look
	QSize sizeHint() const override;

signals:
	void minimizeRequested();
	void maximizeRequested();
	void closeRequested();

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void leaveEvent(QEvent*) override;

private:
	enum Button { None = -1, Minimize, Maximize, Close };
	QRect buttonRect(Button b) const;
	Button buttonAt(const QPoint& p) const;

	QString m_title;
	QIcon m_icon;
	QColor m_accent = QColor(0x70, 0x30, 0xC0);
	bool m_dark = false;
	Button m_hover = None;
	Button m_pressed = None;
};

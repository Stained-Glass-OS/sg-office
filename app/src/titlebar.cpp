/*
 * SG Office -- the title bar SG Office draws itself.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "titlebar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

namespace
{
constexpr int kHeight = 32;
constexpr int kButtonWidth = 46;
}

TitleBar::TitleBar(QWidget* parent) : QWidget(parent)
{
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setFixedHeight(kHeight);
}

QSize TitleBar::sizeHint() const
{
	return {400, kHeight};
}

void TitleBar::setTitle(const QString& title)
{
	m_title = title;
	update();
}

void TitleBar::setIcon(const QIcon& icon)
{
	m_icon = icon;
	update();
}

void TitleBar::setAccent(const QColor& accent)
{
	m_accent = accent;
	update();
}

void TitleBar::setDark(bool dark)
{
	m_dark = dark;
	update();
}

QRect TitleBar::buttonRect(Button b) const
{
	const int i = 2 - static_cast<int>(b);      // close at the right edge
	return {width() - (i + 1) * kButtonWidth, 0, kButtonWidth, kHeight};
}

TitleBar::Button TitleBar::buttonAt(const QPoint& p) const
{
	for (Button b : {Minimize, Maximize, Close})
		if (buttonRect(b).contains(p))
			return b;
	return None;
}

void TitleBar::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const bool active = window()->isActiveWindow();
	const QColor fg = m_dark ? (active ? QColor(0xF0, 0xF0, 0xF0) : QColor(0x90, 0x90, 0x90))
	                         : (active ? QColor(0, 0, 0) : QColor(150, 150, 150));
	p.fillRect(rect(), m_dark ? QColor(0x20, 0x20, 0x20) : QColor(Qt::white));
	// the program's colour as a thin line along the top, like its icon
	p.fillRect(QRect(0, 0, width(), 2), m_accent);

	const int iconSize = 16;
	if (!m_icon.isNull())
		m_icon.paint(&p, QRect(10, (kHeight - iconSize) / 2, iconSize, iconSize));

	p.setPen(fg);
	QFont f = font();
	f.setPointSizeF(9.5);
	p.setFont(f);
	const QRect textRect(36, 0, width() - 36 - 3 * kButtonWidth - 8, kHeight);
	p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
	           p.fontMetrics().elidedText(m_title, Qt::ElideMiddle, textRect.width()));

	for (Button b : {Minimize, Maximize, Close})
	{
		const QRect r = buttonRect(b);
		if (m_hover == b)
			p.fillRect(r, b == Close ? QColor(0xE8, 0x11, 0x23) : m_dark ? QColor(0x3A, 0x3A, 0x3A) : QColor(0xE5, 0xE5, 0xE5));
		p.setPen(QPen(m_hover == b && b == Close ? QColor(Qt::white) : fg, 1));
		const QPoint c = r.center();
		switch (b)
		{
		case Minimize:
			p.drawLine(c.x() - 5, c.y(), c.x() + 5, c.y());
			break;
		case Maximize:
			if (window()->isMaximized())
			{
				p.drawRect(c.x() - 5, c.y() - 3, 8, 8);
				p.drawLine(c.x() - 3, c.y() - 5, c.x() + 5, c.y() - 5);
				p.drawLine(c.x() + 5, c.y() - 5, c.x() + 5, c.y() + 3);
			}
			else
				p.drawRect(c.x() - 5, c.y() - 5, 10, 10);
			break;
		case Close:
			p.setRenderHint(QPainter::Antialiasing);
			p.drawLine(c.x() - 5, c.y() - 5, c.x() + 5, c.y() + 5);
			p.drawLine(c.x() - 5, c.y() + 5, c.x() + 5, c.y() - 5);
			p.setRenderHint(QPainter::Antialiasing, false);
			break;
		default:
			break;
		}
	}
}

void TitleBar::mousePressEvent(QMouseEvent* e)
{
	if (e->button() != Qt::LeftButton)
		return;
	m_pressed = buttonAt(e->position().toPoint());
	if (m_pressed == None && window()->windowHandle())
		window()->windowHandle()->startSystemMove();   // the compositor moves it
}

void TitleBar::mouseMoveEvent(QMouseEvent* e)
{
	const Button h = buttonAt(e->position().toPoint());
	if (h != m_hover)
	{
		m_hover = h;
		update();
	}
}

void TitleBar::mouseReleaseEvent(QMouseEvent* e)
{
	const Button b = buttonAt(e->position().toPoint());
	if (e->button() == Qt::LeftButton && b != None && b == m_pressed)
	{
		if (b == Minimize)
			emit minimizeRequested();
		else if (b == Maximize)
			emit maximizeRequested();
		else
			emit closeRequested();
	}
	m_pressed = None;
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* e)
{
	if (buttonAt(e->position().toPoint()) == None)
		emit maximizeRequested();
}

void TitleBar::leaveEvent(QEvent*)
{
	m_hover = None;
	update();
}

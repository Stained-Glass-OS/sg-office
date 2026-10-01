/*
 * SG Office -- its programs' icons, drawn here (the same pictures sg-shell's
 * office/gen-icons.py draws for the Start menu: a rounded tile in the
 * program's colour with lines of text, a grid or a screen with a chart).
 * Drawn, not shipped as files: the window's icon, the taskbar's button and
 * the title bar all take it from the window.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "appicon.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace
{
constexpr int S = 1024;     // drawn as gen-icons.py draws, then scaled

struct Colours
{
	QColor top, bottom;
};

Colours coloursOf(Kind kind)
{
	switch (kind)
	{
	case Kind::Word: return {QColor(0x2F, 0x6F, 0xD8), QColor(0x1B, 0x4B, 0xA6)};
	case Kind::Cell: return {QColor(0x23, 0x9A, 0x5E), QColor(0x12, 0x6E, 0x40)};
	case Kind::Slide: return {QColor(0xEE, 0x74, 0x36), QColor(0xC8, 0x4B, 0x16)};
	}
	return {QColor(0x70, 0x30, 0xC0), QColor(0x4B, 0x1F, 0x82)};
}

void glyph(QPainter& p, Kind kind, const QRectF& box)
{
	const qreal x0 = box.left(), y0 = box.top(), x1 = box.right(), y1 = box.bottom();
	const qreal w = box.width(), h = box.height();
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	if (kind == Kind::Word)
	{
		const qreal lh = h / 6.2;
		const qreal widths[] = {1.0, 0.86, 1.0, 0.94, 0.62};
		for (int i = 0; i < 5; ++i)
		{
			const qreal y = y0 + i * lh * 1.25;
			p.drawRoundedRect(QRectF(x0, y, w * widths[i], lh * 0.62), lh * 0.31, lh * 0.31);
		}
	}
	else if (kind == Kind::Cell)
	{
		const int cols = 3, rows = 4;
		const qreal t = qMax<qreal>(6, w * 0.055);
		QPen pen(Qt::white, t);
		p.setBrush(Qt::NoBrush);
		p.setPen(pen);
		p.drawRoundedRect(box, w * 0.08, w * 0.08);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(QRectF(x0, y0, w, h / rows + t), w * 0.08, w * 0.08);   // the header row
		p.setPen(pen);
		for (int c = 1; c < cols; ++c)
			p.drawLine(QPointF(x0 + w * c / cols, y0), QPointF(x0 + w * c / cols, y1));
		for (int r = 2; r < rows; ++r)
			p.drawLine(QPointF(x0, y0 + h * r / rows), QPointF(x1, y0 + h * r / rows));
	}
	else
	{
		const qreal t = qMax<qreal>(6, w * 0.05);
		const qreal sy1 = y0 + h * 0.74;
		QPen pen(Qt::white, t);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(QRectF(x0, y0, w, sy1 - y0), w * 0.06, w * 0.06);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		const qreal bw = w * 0.13;
		const qreal f[] = {0.35, 0.6, 0.85};
		for (int i = 0; i < 3; ++i)
		{
			const qreal bx = x0 + w * 0.2 + i * bw * 1.7;
			const qreal top = sy1 - t - (sy1 - y0 - 2 * t) * f[i] * 0.8;
			p.drawRect(QRectF(bx, top, bw, (sy1 - t * 1.6) - top));
		}
		p.setPen(pen);
		const qreal cx = (x0 + x1) / 2;
		p.drawLine(QPointF(cx, sy1), QPointF(cx, y1));
		p.drawLine(QPointF(cx - w * 0.2, y1 - t / 2), QPointF(cx + w * 0.2, y1 - t / 2));
	}
}

QImage tile(Kind kind)
{
	const Colours c = coloursOf(kind);
	QImage im(S, S, QImage::Format_ARGB32_Premultiplied);
	im.fill(Qt::transparent);
	QPainter p(&im);
	p.setRenderHint(QPainter::Antialiasing);
	const qreal m = 72, r = 190;
	const QRectF body(m, m, S - 2 * m, S - 2 * m);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 70));                           // the shadow
	p.drawRoundedRect(body.translated(10, 26), r, r);
	QLinearGradient g(0, 0, 0, S);
	g.setColorAt(0, c.top);
	g.setColorAt(1, c.bottom);
	p.setBrush(g);
	p.drawRoundedRect(body, r, r);
	// a lighter sheen across the top, as glass catches light
	QPainterPath clip;
	clip.addRoundedRect(body, r, r);
	p.setClipPath(clip);
	p.setBrush(QColor(255, 255, 255, 34));
	p.drawRoundedRect(QRectF(m, m, S - 2 * m, S / 2 - m), r, r);
	p.setClipping(false);
	glyph(p, kind, QRectF(m + 170, m + 190, S - 2 * m - 340, S - 2 * m - 360));
	return im;
}
}

QIcon AppIcon::of(Kind kind)
{
	static QIcon cache[3];
	QIcon& icon = cache[static_cast<int>(kind)];
	if (icon.isNull())
	{
		const QImage big = tile(kind);
		for (int s : {16, 20, 24, 32, 40, 48, 64, 128, 256})
			icon.addPixmap(QPixmap::fromImage(big.scaled(s, s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
	}
	return icon;
}

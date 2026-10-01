/*
 * SG Office -- its Open and Save As dialogs: Qt's, with the account's
 * folders (Desktop, Documents, Downloads, Pictures, Music, Videos) in the
 * places list, as File Explorer has them under Quick access.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace Dialogs
{
QStringList open(QWidget* parent, const QString& title, const QString& dir, const QString& filters,
                 QString* selectedFilter, bool multi);
QString save(QWidget* parent, const QString& title, const QString& path, const QString& filters,
             QString* selectedFilter);
}

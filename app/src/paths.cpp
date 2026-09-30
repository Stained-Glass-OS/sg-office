/*
 * SG Office -- where the engine, the editors and the per-user caches live.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "paths.h"

#include <QDir>
#include <QStandardPaths>

namespace
{
QString fromEnv(const char* name, const QString& fallback)
{
	const QByteArray v = qgetenv(name);
	return v.isEmpty() ? fallback : QDir::cleanPath(QString::fromLocal8Bit(v));
}

QString cacheRoot()
{
	QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
	return base + QStringLiteral("/sg-office");
}
}

namespace Paths
{
QString engineDir()
{
	return fromEnv("SG_OFFICE_ENGINE", QStringLiteral("/usr/lib/sg-office/engine"));
}

QString shareDir()
{
	return fromEnv("SG_OFFICE_SHARE", QStringLiteral("/usr/share/sg-office"));
}

QString fontCacheDir()
{
	return cacheRoot() + QStringLiteral("/fonts");
}

QString documentsDir()
{
	return cacheRoot() + QStringLiteral("/documents");
}
}

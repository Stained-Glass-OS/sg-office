/*
 * SG Office -- where the engine, the editors and the per-user caches live.
 *
 * Copyright (C) 2026 Stained Glass OS contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <QString>

namespace Paths
{
// The native engine: x2t, allfontsgen and their libraries (SG_OFFICE_ENGINE,
// else /usr/lib/sg-office/engine).
QString engineDir();
// The editors: web-apps/ and sdkjs/ (SG_OFFICE_SHARE, else /usr/share/sg-office).
QString shareDir();
// Per-user caches: fonts ($XDG_CACHE_HOME/sg-office/fonts).
QString fontCacheDir();
// Per-document working folders ($XDG_CACHE_HOME/sg-office/documents).
QString documentsDir();
}

// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Prototype for issue #72, option 2 (a StoragePartition per Workspace).
// Not built, not in patches/series. See prototypes/workspaces/README.md.
//
// The one place that names the Workspace partition domain, for code that cannot
// include chrome/ (components/permissions, the network context setup).

#ifndef CONTENT_PUBLIC_BROWSER_AERIE_WORKSPACE_PARTITION_H_
#define CONTENT_PUBLIC_BROWSER_AERIE_WORKSPACE_PARTITION_H_

#include "base/files/file_path.h"

namespace content {

// The partition domain is [a-z]* (see StoragePartitionConfig::Create).
inline constexpr char kAerieWorkspacePartitionDomain[] = "aerieworkspace";

// True when `relative_partition_path` (as ConfigureNetworkContextParams gets
// it) is <Storage>/<ext>/aerieworkspace/<hash>: a component equals the domain.
inline bool IsAerieWorkspacePartitionPath(
    const base::FilePath& relative_partition_path) {
  const base::FilePath::StringType domain =
      base::FilePath::FromUTF8Unsafe(kAerieWorkspacePartitionDomain).value();
  for (const auto& component : relative_partition_path.GetComponents()) {
    if (component == domain) {
      return true;
    }
  }
  return false;
}

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_AERIE_WORKSPACE_PARTITION_H_

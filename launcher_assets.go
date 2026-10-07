// SPDX-License-Identifier: GPL-3.0-only
package assets

import "embed"

// Files contains the reviewed sandbox profile and its redistribution notices.
//
//go:embed packaging/docker/seccomp.json packaging/docker/MOBY-PROFILES-LICENSE LICENSE
var Files embed.FS

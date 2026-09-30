#pragma once

// M5-12 (DEC-032): the apply engine — atomic switch and rollback over a
// verified staged update. The engine is filesystem-only (no network): the
// caller stages the verified files first, then applies. Every failure
// converges on a rollback of the touched files (backups are taken before
// the first replacement), so the target directory ends either fully at the
// new state or fully at the old one — never mixed (DEC-006 决策 5 原子切换
// 与回滚).

#include <optional>
#include <string>
#include <vector>

#include <mirage/runtime/update/update_manifest.hpp>

namespace mirage::runtime::update {

/// Outcome of an apply run.
enum class ApplyStatus {
    Applied,    ///< every file switched atomically; backups removed
    RolledBack, ///< a failure occurred; every touched file was restored
    Failed,     ///< a failure occurred AND the rollback could not restore
                ///< (e.g. the target file vanished mid-apply); diagnostic set
};

struct ApplyReport {
    ApplyStatus status = ApplyStatus::Failed;
    std::string diagnostic;           ///< meaningful when status != Applied
    std::vector<std::string> applied; ///< file names switched to the new state
};

/// Applies `manifest.files` from `staging_dir` into `target_dir`.
///
/// Fail-closed order per file: the staged file's size and sha256 are
/// re-checked against the manifest before anything is touched (a corrupted
/// staging area is a rollback, not a partial switch). Replacement is
/// atomic per file (write-aside + rename over the target). A backup of
/// each replaced target is kept in `target_dir/.mirage-update-backup` and
/// removed only after the whole manifest applied; on the first failure all
/// backed-up files are restored in reverse order.
ApplyReport apply_staged_update(const UpdateManifest &manifest, const std::string &staging_dir,
                                const std::string &target_dir);

} // namespace mirage::runtime::update

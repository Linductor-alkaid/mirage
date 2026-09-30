#include <mirage/runtime/update/update_apply.hpp>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include <fstream>

#include <mirage/runtime/update/update_crypto.hpp>

namespace mirage::runtime::update {
namespace {

namespace fs = std::filesystem;

struct Backup {
    std::string name;     ///< manifest file name
    fs::path backup_path; ///< where the old content was stashed
};

/// Best-effort single-file atomic replace: write-aside + rename over the
/// target. False when any step fails.
bool atomic_replace(const fs::path &staged, const fs::path &target, std::error_code &error) {
    const fs::path aside =
        target.parent_path() / ("." + target.filename().string() + ".mirage-update-aside");
    fs::copy_file(staged, aside, fs::copy_options::overwrite_existing, error);
    if (error) {
        return false;
    }
    fs::rename(aside, target, error);
    return !error;
}

} // namespace

ApplyReport apply_staged_update(const UpdateManifest &manifest, const std::string &staging_dir,
                                const std::string &target_dir) {
    ApplyReport report;
    const fs::path staging(staging_dir);
    const fs::path target(target_dir);
    const fs::path backup_dir = target / ".mirage-update-backup";

    std::error_code error;

    // Staging and target must exist; the target may be fresh (first apply
    // into an empty directory — the backup dir is then never used).
    if (!fs::is_directory(staging, error)) {
        report.diagnostic = "staging directory is missing";
        return report;
    }
    fs::create_directories(target, error);
    if (error) {
        report.diagnostic = "cannot create target directory: " + error.message();
        return report;
    }

    // Fail-closed pre-pass: every staged file must exist and match its
    // manifest size and sha256 BEFORE the first replacement (a corrupted
    // staging area is a refusal, never a partial switch — RULE-07 posture).
    for (const auto &file : manifest.files) {
        const fs::path staged = staging / file.name;
        if (!fs::is_regular_file(staged, error)) {
            report.diagnostic = "staged file missing: " + file.name;
            return report;
        }
        const auto size = fs::file_size(staged, error);
        if (error || size != file.size) {
            report.diagnostic = "staged size mismatch: " + file.name;
            return report;
        }
        // Read and digest the staged bytes.
        std::ifstream in(staged, std::ios::binary);
        if (!in) {
            report.diagnostic = "cannot open staged file: " + file.name;
            return report;
        }
        const std::string bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
        if (sha256_hex(bytes) != file.sha256) {
            report.diagnostic = "staged sha256 mismatch: " + file.name;
            return report;
        }
    }

    // Apply: per-file atomic replace with rollback backups, oldest first.
    std::vector<Backup> backups;
    // Manifest files that did NOT exist before the apply (nothing to back
    // up): on rollback they are REMOVED so the target converges on the old
    // state instead of a mixed old/new set (DEC-032 invariant).
    std::vector<std::string> added;
    auto rollback = [&]() {
        for (auto it = added.rbegin(); it != added.rend(); ++it) {
            std::error_code remove_error;
            fs::remove(target / *it, remove_error);
        }
        for (auto it = backups.rbegin(); it != backups.rend(); ++it) {
            const fs::path target_file = target / it->name;
            std::error_code restore_error;
            fs::copy_file(it->backup_path, target_file, fs::copy_options::overwrite_existing,
                          restore_error);
            std::error_code remove_error;
            fs::remove(it->backup_path, remove_error);
        }
        std::error_code remove_error;
        fs::remove_all(backup_dir, remove_error);
    };

    for (const auto &file : manifest.files) {
        const fs::path staged = staging / file.name;
        const fs::path target_file = target / file.name;
        const bool existed = fs::is_regular_file(target_file, error);
        if (existed) {
            if (!fs::is_directory(backup_dir, error)) {
                fs::create_directories(backup_dir, error);
                if (error) {
                    report.diagnostic = "cannot create backup dir: " + error.message();
                    report.status = ApplyStatus::Failed;
                    rollback();
                    return report;
                }
            }
            const fs::path backup_path = backup_dir / file.name;
            fs::copy_file(target_file, backup_path, fs::copy_options::overwrite_existing, error);
            if (error) {
                report.diagnostic = "cannot back up " + file.name + ": " + error.message();
                report.status = ApplyStatus::Failed;
                rollback();
                return report;
            }
            backups.push_back({file.name, backup_path});
        }
        if (!atomic_replace(staged, target_file, error)) {
            report.diagnostic = "cannot switch " + file.name + ": " + error.message();
            report.status =
                backups.empty() && added.empty() ? ApplyStatus::Failed : ApplyStatus::RolledBack;
            rollback();
            return report;
        }
        if (!existed) {
            added.push_back(file.name);
        }
        report.applied.push_back(file.name);
    }

    // Every file switched: the backups are now obsolete.
    std::error_code remove_error;
    fs::remove_all(backup_dir, remove_error);
    report.status = ApplyStatus::Applied;
    return report;
}

} // namespace mirage::runtime::update
